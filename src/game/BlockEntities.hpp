#ifndef CPPFM_GAME_BLOCK_ENTITIES_HPP_INCLUDED
#define CPPFM_GAME_BLOCK_ENTITIES_HPP_INCLUDED
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <type_traits>
#include <new>
#include <thread>
#include "../core/NBTValue.hpp"
#include "../generated/BlockStates.hpp"
#include "Items.hpp"
#include "MutationGateLease.hpp"
#include "BlockEntityData.hpp"
#include "BlockEntityNbtReplacement.hpp"
#include "BlockEntityChunkIndex.hpp"
#include "BlockEntityChunkCodec.hpp"
#include "BlockEntityNbtCodec.hpp"
#include "BlockEntityPosition.hpp"

namespace cppfm {

class BlockEntityStore {
public:
    using Owner = BlockEntityOwner;
    using DirtyCallback = std::function<void(std::int32_t, std::int32_t)>;
    using NbtReplacement = BlockEntityNbtReplacement;
    using SnapshotOverride = BlockEntityChunkCodec::SnapshotOverride;
    static constexpr std::size_t kMaxChunkBlockEntityRecords = 16384;
    static constexpr std::size_t kMaxChunkBlockEntityNbtFootprint =
        8u * 1024u * 1024u;
    static constexpr std::size_t kMaxStoreBlockEntityRecords = 32768;
    static constexpr std::size_t kMaxStoreRawNbtFootprint =
        64u * 1024u * 1024u;

    explicit BlockEntityStore(std::int32_t minY = -64,
                              std::int32_t maxYExclusive = 320,
                              std::size_t maxChunkRecords =
                                  kMaxChunkBlockEntityRecords,
                              std::size_t maxChunkNbtFootprint =
                                  kMaxChunkBlockEntityNbtFootprint,
                              std::size_t maxStoreRecords =
                                  kMaxStoreBlockEntityRecords,
                              std::size_t maxStoreRawNbtFootprint =
                                  kMaxStoreRawNbtFootprint,
                              // Non-owning; this resource must outlive the
                              // store and every operation on its tombstones.
                              std::pmr::memory_resource* tombstoneResource =
                                  std::pmr::get_default_resource())
        : chunkIndex_(maxChunkRecords, maxStoreRecords),
          removed_(tombstoneResource ? tombstoneResource
                                     : std::pmr::get_default_resource()),
          minY_(minY), maxYExclusive_(maxYExclusive),
          maxChunkRecords_(maxChunkRecords),
          maxChunkNbtFootprint_(maxChunkNbtFootprint),
          maxStoreRecords_(maxStoreRecords),
          maxStoreRawNbtFootprint_(maxStoreRawNbtFootprint) {
        if (minY_ >= maxYExclusive_ || minY_ < -2048 ||
            maxYExclusive_ > 2048)
            throw std::invalid_argument(
                "block-entity vertical bounds exceed packed-position range");
        if (maxChunkRecords_ == 0 || maxChunkNbtFootprint_ == 0 ||
            maxStoreRecords_ == 0 || maxStoreRawNbtFootprint_ == 0 ||
            maxChunkRecords_ > maxStoreRecords_ ||
            maxChunkNbtFootprint_ > maxStoreRawNbtFootprint_)
            throw std::invalid_argument("invalid block-entity resource limits");
    }

    // Own the entity and its state lock for the whole mutable slot operation.
    // A raw ItemStack* must never escape without this lifetime/lock guard.
    struct SlotRef {
        Owner owner;
        ItemStack* stack = nullptr;
        std::size_t slotIndex = 0;
        std::unique_lock<std::recursive_mutex> lock;

        explicit operator bool() const noexcept { return stack != nullptr; }
        ItemStack& operator*() const noexcept { return *stack; }
        ItemStack* operator->() const noexcept { return stack; }
    };

    // Shared owners keep an entity alive if it is removed from the index.
    // They do not serialize field access; lock `*owner->stateMtx` before any
    // mutable read or write that can overlap with a server tick or menu.
    Owner getShared(std::int64_t key) const {
        std::lock_guard lock(mutex_);
        const auto it = map_.find(key);
        return it == map_.end() ? Owner{} : it->second;
    }

    // Build and admit a detached candidate while the live owner is locked,
    // publish its dirty generation, then commit the candidate. A failed dirty
    // notification leaves the original owner untouched. `mutate` returns
    // false when the live owner is no longer suitable; `admit` receives the
    // same SnapshotOverride shape consumed by persistence.
    template <typename Mutate, typename Admit>
    bool applyCandidateIfAdmitted(std::int64_t key, const Owner& owner,
                                  Mutate&& mutate, Admit&& admit) {
        if (!owner) return false;
        std::lock_guard entityLock(*owner->stateMtx);
        std::shared_ptr<const DirtyCallback> callback;
        {
            std::lock_guard lock(mutex_);
            const auto current = map_.find(key);
            if (current == map_.end() || current->second != owner)
                return false;
            callback = dirtyCallback_;
        }

        BlockEntity candidate = *owner;
        if (!std::invoke(std::forward<Mutate>(mutate), candidate))
            return false;
        SnapshotOverride replacement{key, std::move(candidate)};
        if (!std::invoke(std::forward<Admit>(admit), replacement))
            return false;

        // Existing-owner snapshots also take stateMtx, so an early dirty
        // generation cannot be consumed against the old entity image: a save
        // either captured an older dirty version, or waits for this commit.
        // If persistence rejects the notification, do not publish the edit.
        if (callback) {
            try {
                if (!invokeDirtyCallbackForKey(key, callback, true))
                    return false;
            } catch (...) {
                return false;
            }
        }

        {
            // Eviction and explicit removal use the store lock. Revalidate
            // under it so an owner detached during preflight is never updated
            // as though it were still the live block entity.
            std::lock_guard lock(mutex_);
            const auto current = map_.find(key);
            if (current == map_.end() || current->second != owner)
                return false;
            *owner = replacement.entity;
        }
        return true;
    }

    struct CandidateInstallResult {
        Owner owner;
        bool inserted = false;
        explicit operator bool() const noexcept { return static_cast<bool>(owner); }
    };

    // For a block entity that has not yet been materialized (sign edits are
    // the current user), check a fully initialized detached value before it
    // becomes visible or dirty. If a concurrent creator wins, return its owner
    // without changing it so the caller can run applyCandidateIfAdmitted.
    template <typename Admit>
    CandidateInstallResult tryCreateSharedCandidateIfAdmitted(
        std::int64_t key, const BlockEntity& candidate, Admit&& admit) {
        if (!containsWorldY(posKeyUnpackY(key))) return {};
        std::shared_ptr<const DirtyCallback> callback;
        {
            std::lock_guard lock(mutex_);
            const auto current = map_.find(key);
            if (current != map_.end() && current->second)
                return {current->second, false};
            if (hasRawRecordLocked(key)) return {};
            callback = dirtyCallback_;
        }

        SnapshotOverride replacement{key, candidate};
        if (!std::invoke(std::forward<Admit>(admit), replacement))
            return {};

        auto staged = std::make_shared<BlockEntity>(candidate);
        staged->stateMtx = std::make_shared<std::recursive_mutex>();
        // Persistence notification is part of admission. Call it before the
        // owner becomes visible so an allocation failure cannot leave a live
        // sign (or other candidate) without a dirty generation. Callers must
        // hold the World's mutation gate across this operation so snapshots
        // cannot observe the conservative early dirty mark before publication.
        if (callback && !invokeDirtyCallbackForKey(key, callback, true))
            return {};
        {
            std::lock_guard lock(mutex_);
            const auto current = map_.find(key);
            if (current != map_.end() && current->second)
                return {current->second, false};
            if (hasRawRecordLocked(key)) return {};
            if (!hasChunkIndexKeyLocked(key) && !canAddRecordLocked(key))
                return {};

            try {
                reserveChunkIndexForKeyLocked(key);
                map_.reserve(map_.size() + 1);
                const auto insertion = map_.find(key);
                if (insertion == map_.end())
                    map_.emplace(key, staged);
                else
                    insertion->second = staged;
                addKeyToChunkIndexLocked(key);
                eraseRawNbtLocked(key);
                removed_.erase(key);
            } catch (...) {
                cancelChunkIndexReservationLocked(key);
                throw;
            }
        }
        return {std::move(staged), true};
    }

    bool hasCurrentKind(std::int64_t key, BlockEntity::Kind kind) const {
        Owner owner;
        {
            std::lock_guard lock(mutex_);
            const auto it = map_.find(key);
            if (it == map_.end() || !it->second) return false;
            owner = it->second;
        }
        std::lock_guard entityLock(*owner->stateMtx);
        std::lock_guard lock(mutex_);
        const auto it = map_.find(key);
        return it != map_.end() && it->second == owner &&
               owner->kind == kind;
    }

    Owner get(std::int64_t key) const {
        return getShared(key);
    }
    Owner getAt(std::int32_t x, std::int32_t y, std::int32_t z) const {
        if (!isPackedBlockPosition(x, y, z)) return {};
        return get(posKey(x, y, z));
    }

    // Return one detached, identifying-data-style block-entity record for
    // gameplay predicates. This intentionally avoids the persistence-only
    // keepPacked marker and snapshots only the requested owner rather than
    // serializing every block entity in the containing chunk.
    std::optional<nbt::Value> snapshotNbtAt(
        std::int32_t x, std::int32_t y, std::int32_t z,
        std::optional<BlockEntity::Kind> expectedKind = std::nullopt) const {
        if (!isPackedBlockPosition(x, y, z) || !containsWorldY(y))
            return std::nullopt;

        const auto key = posKey(x, y, z);
        for (;;) {
            Owner owner;
            std::optional<nbt::Value> rawTemplate;
            {
                std::lock_guard lock(mutex_);
                if (const auto current = map_.find(key); current != map_.end())
                    owner = current->second;
                if (!owner) {
                    if (const auto raw = rawNbt_.find(key);
                        raw != rawNbt_.end() && raw->second.tag != nbt::End)
                        rawTemplate = raw->second;
                }
            }

            if (!owner) {
                // Opaque saved records are not live Minecraft BlockEntities.
                // Predicates must not be authorized using stale/unvalidated
                // disk NBT when no typed runtime owner exists.
                return std::nullopt;
            }
            if (!owner->stateMtx)
                throw std::logic_error(
                    "block-entity owner has no state mutex");

            BlockEntityState detached;
            {
                std::lock_guard entityLock(*owner->stateMtx);
                detached = *owner;
                if (expectedKind && detached.kind != *expectedKind)
                    return std::nullopt;
                std::lock_guard storeLock(mutex_);
                const auto current = map_.find(key);
                if (current == map_.end() || current->second != owner)
                    continue;

                rawTemplate.reset();
                if (const auto raw = rawNbt_.find(key);
                    raw != rawNbt_.end() && raw->second.tag != nbt::End)
                    rawTemplate = raw->second;
            }

            auto record = rawTemplate.value_or(nbt::Value::makeCompound());
            if (detached.kind == BlockEntity::Kind::MovingPiston) {
                std::vector<BlockEntityChunkCodec::TypedRecord> moving;
                moving.push_back({key, std::move(detached)});
                auto records = nbt::Value::makeList(nbt::Compound);
                (void)BlockEntityChunkCodec::appendMovingPistons(
                    x >> 4, z >> 4, moving, records, minY_, maxYExclusive_,
                    maxChunkRecords_, maxChunkNbtFootprint_);
                if (records.list.empty()) return std::nullopt;
                record = std::move(records.list.front());
            } else if (!BlockEntityNbtCodec::encodeRecord(
                           record, detached, x, y, z)) {
                return std::nullopt;
            }

            record.comp.erase(
                std::remove_if(record.comp.begin(), record.comp.end(),
                    [](const auto& field) {
                        return field.first == "keepPacked";
                    }),
                record.comp.end());
            return record;
        }
    }

    bool canCreateAt(std::int64_t key) const {
        if (!containsWorldY(posKeyUnpackY(key))) return false;
        std::lock_guard lock(mutex_);
        if (const auto typed = map_.find(key);
            typed != map_.end() && typed->second)
            return true;
        if (hasRawRecordLocked(key)) return false;
        if (hasChunkIndexKeyLocked(key)) return true;
        return canAddRecordLocked(key);
    }

    // Preflight insertion before a world mutation whose block callback would
    // remove an existing typed or opaque record at this position.
    bool canInsertNewAt(std::int64_t key) const {
        if (!containsWorldY(posKeyUnpackY(key))) return false;
        std::lock_guard lock(mutex_);
        const auto typed = map_.find(key);
        if (typed != map_.end() && typed->second) return false;
        if (hasRawRecordLocked(key)) return false;
        if (hasChunkIndexKeyLocked(key)) return true;
        return canAddRecordLocked(key);
    }

    // Unknown block entities are retained as raw NBT even when this build
    // cannot decode their runtime type. Callers making destructive decisions
    // must treat both typed and opaque entries as present.
    bool containsAt(std::int32_t x, std::int32_t y, std::int32_t z) const {
        if (!isPackedBlockPosition(x, y, z)) return false;
        const auto key = posKey(x, y, z);
        std::lock_guard lock(mutex_);
        const auto typed = map_.find(key);
        return (typed != map_.end() && static_cast<bool>(typed->second)) ||
               hasRawRecordLocked(key);
    }

    bool hasOpaqueNbtAt(std::int64_t key) const {
        std::lock_guard lock(mutex_);
        return hasRawRecordLocked(key);
    }

    // Return an empty owner when the saved record is opaque and cannot be
    // represented by this build. Interactive callers can reject opening it
    // without turning an expected compatibility boundary into a session error.
    // If a persistence dirty callback is installed, callers must hold the
    // owning World's mutation gate through this call. The callback runs before
    // commit for exception safety; the gate prevents a save snapshot from
    // consuming that dirty generation before the owner becomes visible.
    Owner tryCreateShared(std::int64_t key, BlockEntity::Kind kind) {
        if (!containsWorldY(posKeyUnpackY(key))) return {};
        for (;;) {
            Owner owner;
            std::shared_ptr<const DirtyCallback> callback;
            const auto invokeDirtyCallback = [&]() {
                if (!callback) return true;
                return invokeDirtyCallbackForKey(key, callback, true);
            };
            {
                std::lock_guard lock(mutex_);
                const auto indexed = map_.find(key);
                if (indexed == map_.end() || !indexed->second) {
                    if (hasRawRecordLocked(key))
                        return {};
                    if (!hasChunkIndexKeyLocked(key) &&
                        !canAddRecordLocked(key))
                        return {};
                    // The dirty hook is part of admitting this new owner. Run
                    // it before publication: if it throws, no caller can have
                    // observed an owner which the store failed to mark dirty.
                    callback = dirtyCallback_;
                } else {
                    owner = indexed->second;
                }
            }

            if (!owner) {
                // Fully initialize the detached owner before invoking user
                // code. The callback may re-enter this store, so both it and
                // the final revalidation/publication run without a lock held
                // across the callback. Same-key recursive admission is
                // rejected because it cannot complete before this callback.
                auto staged = std::make_shared<BlockEntity>();
                staged->kind = kind;
                if (!invokeDirtyCallback()) return {};

                bool published = false;
                {
                    std::lock_guard lock(mutex_);
                    const auto indexed = map_.find(key);
                    if (indexed != map_.end() && indexed->second) {
                        // A concurrent or re-entrant creator won while the
                        // dirty callback ran. Leave its owner untouched and
                        // resolve it through the normal kind/idempotency path.
                        owner = indexed->second;
                    } else {
                        // A callback can re-enter and change either opaque
                        // data or the resource budget; repeat admission before
                        // publishing the staged owner.
                        if (hasRawRecordLocked(key) ||
                            (!hasChunkIndexKeyLocked(key) &&
                             !canAddRecordLocked(key)))
                            return {};

                        try {
                            map_.reserve(map_.size() + 1);
                            reserveChunkIndexForKeyLocked(key);
                            const auto insertion = map_.find(key);
                            if (insertion == map_.end())
                                map_.emplace(key, staged);
                            else
                                insertion->second = staged;
                            addKeyToChunkIndexLocked(key);
                            eraseRawNbtLocked(key);
                            removed_.erase(key);
                        } catch (...) {
                            cancelChunkIndexReservationLocked(key);
                            throw;
                        }
                        published = true;
                    }
                }
                if (published) return staged;
                continue;
            }

            {
                // Never wait for an entity while holding the store index lock:
                // forEach callbacks hold the entity lock and may call markDirty
                // or create another block entity. Revalidate after acquiring
                // the entity lock because remove() may have detached this owner
                // while we waited.
                std::lock_guard entityLock(*owner->stateMtx);
                std::lock_guard lock(mutex_);
                const auto indexed = map_.find(key);
                if (indexed == map_.end() || indexed->second != owner)
                    continue;

                // This is a get-or-create operation, not an unconditional
                // reset. Menus, ticks, and placement paths can race to ensure
                // the same entity; repeated same-kind calls must preserve its
                // inventory and opaque NBT template.
                if (owner->kind == kind) return owner;

                // Preserve identity so menus already holding this owner remain
                // valid when the block changes kind. A type change is the one
                // case in which clearing the old data and NBT is intentional.
                // Defer that destructive change until the dirty hook has
                // succeeded; a throwing hook must leave both the owner and
                // its opaque persistence template intact.
                callback = dirtyCallback_;
            }

            BlockEntity replacement;
            replacement.kind = kind;
            if (!invokeDirtyCallback()) return {};

            {
                std::lock_guard entityLock(*owner->stateMtx);
                std::lock_guard lock(mutex_);
                const auto indexed = map_.find(key);
                if (indexed == map_.end() || indexed->second != owner)
                    continue;
                if (owner->kind == kind) return owner;

                // swapState is noexcept and preserves the shared owner lock.
                // The dirty hook has succeeded and the owner is still live,
                // so the type-change transaction can now commit.
                owner->swapState(replacement);
                eraseRawNbtLocked(key);
                removed_.erase(key);
            }
            return owner;
        }
    }

    // Atomically create a set of previously absent typed records. Capacity
    // accounting is performed for the whole batch before any owner is
    // published, so callers can stage multi-block operations without a
    // late per-record budget failure leaving a partial store update.
    // Duplicate keys, existing typed/opaque records, out-of-range positions,
    // or either record cap reject the entire batch without changing the
    // indexed records. Owner and index capacity allocations are staged before
    // publication. Dirty callbacks are staged before publication so a throwing
    // persistence hook cannot leave records live without their world blocks.
    // When connected to a World persistence callback, callers must hold that
    // World's mutation gate through this call and the matching block commit.
    std::optional<std::vector<Owner>> tryCreateSharedBatch(
        const std::vector<std::int64_t>& keys, BlockEntity::Kind kind) {
        std::vector<Owner> owners;
        owners.reserve(keys.size());
        std::unordered_map<std::int64_t, Owner> staged;
        staged.reserve(keys.size());
        std::unordered_set<std::int64_t> uniqueKeys;
        uniqueKeys.reserve(keys.size());
        std::unordered_map<std::int64_t, std::size_t> additionsByChunk;
        additionsByChunk.reserve(keys.size());
        std::vector<std::pair<std::int32_t, std::int32_t>> dirtyChunks;
        dirtyChunks.reserve(keys.size());

        for (const auto key : keys) {
            if (!containsWorldY(posKeyUnpackY(key)) ||
                !uniqueKeys.insert(key).second)
                return std::nullopt;

            auto owner = std::make_shared<BlockEntity>();
            owner->kind = kind;
            owners.push_back(owner);
            staged.emplace(key, std::move(owner));
            ++additionsByChunk[chunkIndexKey(key)];
            dirtyChunks.emplace_back(posKeyUnpackX(key) >> 4,
                                     posKeyUnpackZ(key) >> 4);
        }
        std::sort(dirtyChunks.begin(), dirtyChunks.end());
        dirtyChunks.erase(std::unique(dirtyChunks.begin(), dirtyChunks.end()),
                          dirtyChunks.end());

        std::shared_ptr<const DirtyCallback> callback;
        const auto preflightLocked = [&] {
            for (const auto key : keys) {
                // A batch is create-only: do not replace typed owners or
                // promote opaque NBT, and reject inconsistent indexed keys.
                const auto typed = map_.find(key);
                if ((typed != map_.end() && typed->second) ||
                    hasRawRecordLocked(key) ||
                    hasChunkIndexKeyLocked(key))
                    return false;
            }
            for (const auto& [chunk, addition] : additionsByChunk) {
                const auto current = chunkRecordCountLocked(chunk);
                if (current > maxChunkRecords_ ||
                    addition > maxChunkRecords_ - current)
                    return false;
            }
            const auto liveRecords = chunkIndex_.liveRecordCount();
            return liveRecords <= maxStoreRecords_ &&
                   keys.size() <= maxStoreRecords_ - liveRecords;
        };
        const auto reserveLocked = [&] {
            reserveChunkIndexForKeysLocked(uniqueKeys);
            map_.reserve(map_.size() + staged.size());
        };
        {
            std::lock_guard lock(mutex_);
            if (!preflightLocked()) return std::nullopt;
            callback = dirtyCallback_;
        }

        // Persistence hooks may allocate, throw, or re-enter this store. Run
        // them before publication and without the index lock. If a hook or a
        // reentrant writer changes the target set, the second preflight below
        // rejects this batch; an extra dirty mark is harmless and conservative.
        if (callback) {
            if (!hasRequiredMutationGate()) return std::nullopt;
            for (const auto& [cx, cz] : dirtyChunks) {
                if (!invokeDirtyCallbackForChunk(cx, cz, callback, true))
                    return std::nullopt;
            }
        }

        {
            std::lock_guard lock(mutex_);
            if (!preflightLocked()) return std::nullopt;
            try {
                reserveLocked();
            } catch (...) {
                cancelChunkIndexReservationsLocked(uniqueKeys);
                throw;
            }
            for (const auto key : keys) {
                if (const auto tombstone = map_.find(key);
                    tombstone != map_.end() && !tombstone->second)
                    map_.erase(tombstone);
                eraseRawNbtLocked(key);
            }
            // All record-budget checks and allocations/reservations precede
            // node transfer. Callback failures and callback reentry have
            // already been handled before the all-at-once store publication.
            map_.merge(staged);
            for (const auto key : keys) {
                (void)addKeyToChunkIndexLocked(key);
                removed_.erase(key);
            }
        }
        return owners;
    }

    Owner createShared(std::int64_t key, BlockEntity::Kind kind) {
        auto owner = tryCreateShared(key, kind);
        if (!owner)
            throw std::runtime_error(
                "block entity is opaque or the configured store budget is exhausted");
        return owner;
    }
    bool remove(std::int64_t key) { return removeImpl(key, false); }

    // A block-state replacement is already committed when its World hook
    // runs. Even if persistence cannot admit the dirty notification, retaining
    // the old BE would serialize data that no longer belongs to the block.
    // Remove it in memory and report the failed notification so the World edit
    // can still attempt its own chunk-dirty callback.
    bool removeAfterWorldBlockReplacement(std::int64_t key) {
        return removeImpl(key, true);
    }

private:
    bool removeImpl(std::int64_t key,
                    bool removeAfterDirtyCallbackFailure) {
        std::shared_ptr<const DirtyCallback> callback;
        Owner expectedOwner;
        bool hadTypedOwner = false;
        bool hadOpaqueEntry = false;
        {
            std::lock_guard lock(mutex_);
            const auto it = map_.find(key);
            const auto raw = rawNbt_.find(key);
            hadTypedOwner = it != map_.end() && static_cast<bool>(it->second);
            hadOpaqueEntry = raw != rawNbt_.end() &&
                             raw->second.tag != nbt::End;
            // A null owner or TAG_End raw value is a fallback tombstone from
            // an earlier allocation failure. Leave it in place until the
            // chunk is durably saved or forgotten; it still blocks stale NBT
            // from a racing chunk load.
            if (!hadTypedOwner && !hadOpaqueEntry) return true;
            if (hadTypedOwner) expectedOwner = it->second;
            callback = dirtyCallback_;
        }

        // Admit the dirty generation before removing the live record. Explicit
        // removals keep the owner intact when admission fails; an already
        // committed World block replacement instead removes stale entity data
        // and reports the failed notification so World can still dirty its
        // chunk. World-coupled callers hold the mutation gate throughout.
        bool dirtyAccepted = true;
        bool fallbackTombstone = false;
        if (callback) {
            if (!removeAfterDirtyCallbackFailure) {
                // Preserve the existing explicit-removal contract: a failed
                // dirty callback leaves the owner live for a later retry.
                if (!invokeDirtyCallbackForKey(key, callback, true))
                    return false;
            } else {
                try {
                    dirtyAccepted = invokeDirtyCallbackForKey(
                        key, callback, true);
                } catch (...) {
                    dirtyAccepted = false;
                }
            }
        }

        {
            std::lock_guard lock(mutex_);
            const auto it = map_.find(key);
            const auto raw = rawNbt_.find(key);
            const bool hasTypedOwner =
                it != map_.end() && static_cast<bool>(it->second);
            const bool hasOpaqueEntry = raw != rawNbt_.end() &&
                                        raw->second.tag != nbt::End;
            if (hadTypedOwner != hasTypedOwner ||
                (hadTypedOwner && it->second != expectedOwner) ||
                hadOpaqueEntry != hasOpaqueEntry)
                return false;
            if (!hadTypedOwner && !hadOpaqueEntry) return true;

            std::size_t rawFootprint = 0;
            const auto chunk = chunkIndexKey(key);
            auto chunkFootprint = rawNbtFootprintByChunk_.end();
            if (hadOpaqueEntry) {
                accountNbtFootprint(raw->second, rawFootprint,
                                    maxChunkNbtFootprint_);
                chunkFootprint = rawNbtFootprintByChunk_.find(chunk);
                if (rawNbtBytes_ < rawFootprint ||
                    chunkFootprint == rawNbtFootprintByChunk_.end() ||
                    chunkFootprint->second < rawFootprint)
                    throw std::logic_error(
                        "block-entity NBT footprint accounting is inconsistent");
            }

            const bool indexed = hasChunkIndexKeyLocked(key);
            // The ordinary tombstone set provides a compact removal journal.
            // If it cannot allocate, reuse the already allocated typed-owner
            // or raw-NBT node as an in-memory tombstone. That fallback is not
            // serialized, but it still prevents a detached chunk-load commit
            // from resurrecting the removed record.
            try {
                removed_.insert(key);
            } catch (const std::bad_alloc&) {
                fallbackTombstone = true;
            }
            if (hadTypedOwner) {
                if (fallbackTombstone)
                    it->second.reset();
                else
                    map_.erase(it);
            }
            if (hadOpaqueEntry) {
                rawNbtBytes_ -= rawFootprint;
                chunkFootprint->second -= rawFootprint;
                if (chunkFootprint->second == 0)
                    rawNbtFootprintByChunk_.erase(chunkFootprint);
                if (fallbackTombstone)
                    raw->second = nbt::Value{};
                else
                    rawNbt_.erase(raw);
            }
            if (indexed) removeKeyFromChunkIndexLocked(key);
        }
        return dirtyAccepted;
    }

public:
    void setDirtyCallback(std::function<void(std::int32_t, std::int32_t)> callback) {
        std::shared_ptr<const DirtyCallback> staged;
        if (callback)
            staged = std::make_shared<DirtyCallback>(std::move(callback));
        std::lock_guard lock(mutex_);
        dirtyCallback_ = std::move(staged);
    }

    // Production stores are bound to their owning World before callbacks are
    // installed. Standalone stores remain useful for isolated persistence
    // and unit tests. A bound store requires its World's lease for dirty
    // pre-admission paths that publish a new owner or replace/remove live
    // state; post-mutation dirty notifications remain available to tick code.
    bool bindMutationGate(const void* identity) {
        if (!identity) return false;
        std::lock_guard lock(mutex_);
        if (dirtyCallback_ || mutationGateIdentity_ || !map_.empty() ||
            !rawNbt_.empty())
            return false;
        mutationGateIdentity_ = identity;
        return true;
    }

    bool tryMarkDirty(std::int64_t key) {
        std::shared_ptr<const DirtyCallback> callback;
        {
            std::lock_guard lock(mutex_);
            callback = dirtyCallback_;
        }
        return !callback || invokeDirtyCallbackForKey(key, callback, true);
    }

    void markDirty(std::int64_t key) {
        std::shared_ptr<const DirtyCallback> callback;
        {
            std::lock_guard lock(mutex_);
            callback = dirtyCallback_;
        }
        if (callback) (void)invokeDirtyCallbackForKey(key, callback);
    }
    bool empty() const {
        std::lock_guard lock(mutex_);
        const bool hasTyped = std::any_of(
            map_.begin(), map_.end(),
            [](const auto& entry) { return static_cast<bool>(entry.second); });
        return !hasTyped && std::none_of(
            rawNbt_.begin(), rawNbt_.end(),
            [](const auto& entry) { return entry.second.tag != nbt::End; });
    }
    std::size_t size() const {
        std::lock_guard lock(mutex_);
        std::size_t count = 0;
        for (const auto& [key, raw] : rawNbt_) {
            (void)key;
            if (raw.tag != nbt::End) ++count;
        }
        for (const auto& [key, owner] : map_) {
            if (owner && !hasRawRecordLocked(key)) ++count;
        }
        return count;
    }
    // Exposes contention as a diagnostic counter and lets concurrency tests
    // prove they reached the snapshot retry path rather than racing its start.
    std::size_t snapshotContentionCount() const noexcept {
        return snapshotContentions_.load(std::memory_order_relaxed);
    }
    void clearRemovalTombstonesForChunk(std::int32_t cx, std::int32_t cz) {
        std::lock_guard lock(mutex_);
        for (auto it = removed_.begin(); it != removed_.end();) {
            if ((posKeyUnpackX(*it) >> 4) == cx &&
                (posKeyUnpackZ(*it) >> 4) == cz)
                it = removed_.erase(it);
            else
                ++it;
        }
        clearFallbackTombstonesForChunkLocked(cx, cz);
    }

    // Call only after the corresponding World chunk has been successfully
    // evicted while gameplay mutation is serialized. The persisted chunk is
    // authoritative at this point, so typed owners, opaque templates, index
    // entries, and removal tombstones can all be released together.
    void forgetChunk(std::int32_t cx, std::int32_t cz) {
        std::lock_guard lock(mutex_);
        const auto chunk = chunkIndexKey(cx, cz);
        if (const auto indexed = chunkIndex_.takeChunk(cx, cz)) {
            for (const auto key : *indexed) {
                map_.erase(key);
                if (const auto raw = rawNbt_.find(key); raw != rawNbt_.end())
                    eraseRawNbtLocked(key);
            }
        }
        // Removed records are not in the live index. Always clear their
        // tombstones, even when another indexed record made takeChunk succeed.
        for (auto it = removed_.begin(); it != removed_.end();) {
            if (chunkIndexKey(*it) == chunk) it = removed_.erase(it);
            else ++it;
        }
        clearFallbackTombstonesForChunkLocked(cx, cz);
        rawNbtFootprintByChunk_.erase(chunk);
    }

    template <typename Fn> void forEach(Fn fn) {
        std::vector<std::pair<std::int64_t, Owner>> snapshot;
        {
            std::lock_guard lock(mutex_);
            snapshot.reserve(map_.size());
            for (const auto& [key, owner] : map_)
                snapshot.emplace_back(key, owner);
        }
        // Invoke user code outside the map lock.  Besides avoiding a long
        // critical section, this permits callbacks to remove/create entries
        // without invalidating the iteration.  The shared owners keep the
        // pointed-to values alive for the callback duration.
        for (auto& [key, owner] : snapshot)
            if (owner) {
                std::lock_guard entityLock(*owner->stateMtx);
                fn(key, *owner);
            }
    }

    bool writeChunkNbt(
        std::int32_t cx, std::int32_t cz, nbt::Value& outList,
        const std::vector<SnapshotOverride>& overrides = {},
        bool returnIfContended = false) const {
        BlockEntityChunkCodec::Snapshot snapshot;
        if (!snapshotChunkNbt(cx, cz, snapshot, returnIfContended))
            return false;
        return BlockEntityChunkCodec::writeChunk(
            cx, cz, outList, snapshot, overrides, minY_, maxYExclusive_,
            maxChunkRecords_, maxChunkNbtFootprint_);
    }

    // Initial chunk packets need transient moving-piston state even though it
    // is deliberately excluded from durable Anvil snapshots. Keep the two
    // projections separate so a server stop cannot persist a half-finished
    // piston transaction as if it were a stable block entity.
    bool writeChunkInitialNbt(std::int32_t cx, std::int32_t cz,
                              nbt::Value& outList,
                              bool returnIfContended = false) const {
        BlockEntityChunkCodec::Snapshot snapshot;
        if (!snapshotChunkNbt(cx, cz, snapshot, returnIfContended))
            return false;
        return BlockEntityChunkCodec::writeInitialChunk(
            cx, cz, outList, snapshot, minY_, maxYExclusive_,
            maxChunkRecords_, maxChunkNbtFootprint_);
    }

    // Replace one block entity from a detached serialized snapshot. Clone and
    // move commands capture these snapshots before changing any blocks, so
    // inventory/component NBT is not lost when a destination setter resets its
    // current block entity. Typed owners keep their identity when possible so
    // menus holding a live container do not silently become detached.
    bool replaceNbtAt(std::int32_t x, std::int32_t y, std::int32_t z,
                      std::optional<nbt::Value> serializedEntity) {
        return replaceNbtAtIfAdmitted(
            x, y, z, std::move(serializedEntity),
            [](const NbtReplacement&) { return true; });
    }

    // Stage and admit a complete serialized replacement before publishing it.
    // The dirty callback is deliberately invoked before publication. Callers
    // that need atomicity with the corresponding World mutation must hold that
    // World's mutation gate through this call; persistence snapshots use the
    // same gate, so they cannot consume the early dirty generation against an
    // old block/entity image.
    template <typename Admit>
    bool replaceNbtAtIfAdmitted(
        std::int32_t x, std::int32_t y, std::int32_t z,
        std::optional<nbt::Value> serializedEntity, Admit&& admit) {
        if (!containsWorldY(y) || !isPackedBlockPosition(x, y, z))
            return false;
        const auto key = posKey(x, y, z);
        if (!serializedEntity) {
            return remove(key);
        }
        if (serializedEntity->tag != nbt::Compound) return false;
        serializedEntity->set("x", nbt::Value::makeInt(x));
        serializedEntity->set("y", nbt::Value::makeInt(y));
        serializedEntity->set("z", nbt::Value::makeInt(z));
        const auto* id = serializedEntity->get("id");
        if (!id || id->tag != nbt::String) return false;

        NbtReplacement replacement{x, y, z, std::move(serializedEntity)};
        if (!std::invoke(std::forward<Admit>(admit), replacement))
            return false;
        serializedEntity = std::move(replacement.entity);

        auto decoded = BlockEntityNbtCodec::decodeRecord(*serializedEntity);
        if (decoded && decoded->first != key) return false;
        std::size_t newRawFootprint = 0;
        try {
            accountNbtFootprint(*serializedEntity, newRawFootprint,
                                maxChunkNbtFootprint_);
        } catch (const std::length_error&) {
            return false;
        }
        std::unordered_map<std::int64_t, Owner> stagedOwners;
        if (decoded) stagedOwners.emplace(key, std::move(decoded->second));
        std::unordered_map<std::int64_t, nbt::Value> stagedRaw;
        stagedRaw.emplace(key, std::move(*serializedEntity));

        std::shared_ptr<const DirtyCallback> callback;
        {
            std::lock_guard lock(mutex_);
            callback = dirtyCallback_;
        }
        if (callback && !invokeDirtyCallbackForKey(key, callback, true))
            return false;

        for (;;) {
            Owner current;
            {
                std::lock_guard lock(mutex_);
                if (const auto it = map_.find(key); it != map_.end())
                    current = it->second;
            }

            if (current) {
                std::lock_guard entityLock(*current->stateMtx);
                std::lock_guard lock(mutex_);
                const auto indexed = map_.find(key);
                if (indexed == map_.end() || indexed->second != current)
                    continue;
                if (!canReplaceRawNbtLocked(key, newRawFootprint))
                    return false;
                std::size_t oldRawFootprint = 0;
                if (const auto oldRaw = rawNbt_.find(key);
                    oldRaw != rawNbt_.end() &&
                    oldRaw->second.tag != nbt::End)
                    accountNbtFootprint(oldRaw->second, oldRawFootprint,
                                        maxChunkNbtFootprint_);
                const auto chunk = chunkIndexKey(key);
                rawNbtFootprintByChunk_.reserve(
                    rawNbtFootprintByChunk_.size() + 1);
                rawNbt_.reserve(rawNbt_.size() + 1);
                auto footprint = rawNbtFootprintByChunk_.end();
                bool insertedFootprint = false;
                try {
                    const auto [entry, inserted] =
                        rawNbtFootprintByChunk_.try_emplace(chunk, 0);
                    footprint = entry;
                    insertedFootprint = inserted;
                    if (stagedOwners.empty()) {
                        map_.erase(indexed);
                    } else {
                        *current = *stagedOwners.begin()->second;
                    }
                    rawNbt_.erase(key);
                    rawNbt_.merge(stagedRaw);
                    rawNbtBytes_ = rawNbtBytes_ - oldRawFootprint +
                                   newRawFootprint;
                    footprint->second = footprint->second - oldRawFootprint +
                                        newRawFootprint;
                    removed_.erase(key);
                } catch (...) {
                    if (insertedFootprint && footprint->second == 0)
                        rawNbtFootprintByChunk_.erase(footprint);
                    throw;
                }
                break;
            }

            {
                std::lock_guard lock(mutex_);
                if (const auto existing = map_.find(key);
                    existing != map_.end() && existing->second)
                    continue;
                if (!canReplaceRawNbtLocked(
                        key, newRawFootprint,
                        hasChunkIndexKeyLocked(key) ? 0 : 1))
                    return false;
                std::size_t oldRawFootprint = 0;
                if (const auto oldRaw = rawNbt_.find(key);
                    oldRaw != rawNbt_.end() &&
                    oldRaw->second.tag != nbt::End)
                    accountNbtFootprint(oldRaw->second, oldRawFootprint,
                                        maxChunkNbtFootprint_);
                const auto chunk = chunkIndexKey(key);
                rawNbtFootprintByChunk_.reserve(
                    rawNbtFootprintByChunk_.size() + 1);
                if (!stagedOwners.empty()) map_.reserve(map_.size() + 1);
                rawNbt_.reserve(rawNbt_.size() + 1);
                auto footprint = rawNbtFootprintByChunk_.end();
                bool insertedFootprint = false;
                try {
                    reserveChunkIndexForKeyLocked(key);
                    const auto [entry, inserted] =
                        rawNbtFootprintByChunk_.try_emplace(chunk, 0);
                    footprint = entry;
                    insertedFootprint = inserted;
                    if (const auto existing = map_.find(key);
                        existing != map_.end() && !existing->second)
                        map_.erase(existing);
                    map_.merge(stagedOwners);
                    rawNbt_.erase(key);
                    rawNbt_.merge(stagedRaw);
                    rawNbtBytes_ = rawNbtBytes_ - oldRawFootprint +
                                   newRawFootprint;
                    footprint->second = footprint->second - oldRawFootprint +
                                        newRawFootprint;
                    removed_.erase(key);
                    addKeyToChunkIndexLocked(key);
                } catch (...) {
                    cancelChunkIndexReservationLocked(key);
                    if (insertedFootprint && footprint->second == 0)
                        rawNbtFootprintByChunk_.erase(footprint);
                    throw;
                }
                break;
            }
        }
        return true;
    }

    // Check a complete replacement set against the same per-chunk and
    // store-wide accounting used by replaceNbtAt, without publishing any of
    // it. Callers that then perform the mutations must serialize competing
    // world/block-entity publication for the duration of preflight + commit.
    bool canReplaceNbtBatch(
        const std::vector<NbtReplacement>& replacements) const {
        struct Prepared {
            std::int64_t key = 0;
            std::int64_t chunk = 0;
            bool present = false;
            std::size_t footprint = 0;
        };
        std::vector<Prepared> prepared;
        prepared.reserve(replacements.size());
        std::unordered_set<std::int64_t> uniqueKeys;
        uniqueKeys.reserve(replacements.size());
        for (const auto& replacement : replacements) {
            if (!containsWorldY(replacement.y) ||
                !isPackedBlockPosition(replacement.x, replacement.y,
                                       replacement.z))
                return false;
            const auto key = posKey(replacement.x, replacement.y,
                                    replacement.z);
            if (!uniqueKeys.insert(key).second) return false;
            Prepared item;
            item.key = key;
            item.chunk = chunkIndexKey(key);
            if (replacement.entity) {
                if (replacement.entity->tag != nbt::Compound) return false;
                auto entity = *replacement.entity;
                entity.set("x", nbt::Value::makeInt(replacement.x));
                entity.set("y", nbt::Value::makeInt(replacement.y));
                entity.set("z", nbt::Value::makeInt(replacement.z));
                const auto* id = entity.get("id");
                if (!id || id->tag != nbt::String) return false;
                try {
                    accountNbtFootprint(entity, item.footprint,
                                        maxChunkNbtFootprint_);
                } catch (const std::length_error&) {
                    return false;
                }
                item.present = true;
            }
            prepared.push_back(item);
        }

        std::lock_guard lock(mutex_);
        std::unordered_map<std::int64_t, std::int64_t> recordDelta;
        std::unordered_map<std::int64_t, std::int64_t> rawDelta;
        recordDelta.reserve(prepared.size());
        rawDelta.reserve(prepared.size());
        std::int64_t totalRecordDelta = 0;
        std::int64_t totalRawDelta = 0;
        for (const auto& item : prepared) {
            const bool oldRecord = hasChunkIndexKeyLocked(item.key);
            const auto oldRaw = rawNbt_.find(item.key);
            std::size_t oldFootprint = 0;
            if (oldRaw != rawNbt_.end() && oldRaw->second.tag != nbt::End) {
                try {
                    accountNbtFootprint(oldRaw->second, oldFootprint,
                                        maxChunkNbtFootprint_);
                } catch (const std::length_error&) {
                    return false;
                }
            }
            const bool newRecord = item.present;
            const std::int64_t recordChange =
                static_cast<std::int64_t>(newRecord) -
                static_cast<std::int64_t>(oldRecord);
            const std::int64_t rawChange =
                static_cast<std::int64_t>(item.footprint) -
                static_cast<std::int64_t>(oldFootprint);
            recordDelta[item.chunk] += recordChange;
            rawDelta[item.chunk] += rawChange;
            totalRecordDelta += recordChange;
            totalRawDelta += rawChange;
        }

        const auto projectedCountFits = [&](std::size_t current,
                                            std::int64_t delta,
                                            std::size_t limit) {
            if (delta < 0) {
                const auto decrease = static_cast<std::uint64_t>(-delta);
                return decrease <= current;
            }
            const auto increase = static_cast<std::uint64_t>(delta);
            return current <= limit && increase <= limit - current;
        };
        for (const auto& [chunk, delta] : recordDelta) {
            const auto current = chunkRecordCountLocked(chunk);
            if (!projectedCountFits(current, delta, maxChunkRecords_))
                return false;
        }
        if (!projectedCountFits(chunkIndex_.liveRecordCount(), totalRecordDelta,
                                maxStoreRecords_))
            return false;

        const auto projectedBytesFit = [&](std::size_t current,
                                           std::int64_t delta,
                                           std::size_t limit) {
            if (delta < 0) {
                const auto decrease = static_cast<std::uint64_t>(-delta);
                return decrease <= current;
            }
            const auto increase = static_cast<std::uint64_t>(delta);
            return current <= limit && increase <= limit - current;
        };
        for (const auto& [chunk, delta] : rawDelta) {
            const auto found = rawNbtFootprintByChunk_.find(chunk);
            const auto current = found == rawNbtFootprintByChunk_.end()
                ? std::size_t{0} : found->second;
            if (!projectedBytesFit(current, delta,
                                   maxChunkNbtFootprint_))
                return false;
        }
        return projectedBytesFit(rawNbtBytes_, totalRawDelta,
                                 maxStoreRawNbtFootprint_);
    }

    std::function<void()> prepareChunkNbt(const nbt::Value& root) {
        // Decode everything into detached owners first. If an allocation or a
        // malformed value fails, the live store stays untouched; publication
        // can then safely reject the corresponding chunk without later saving
        // a partially restored block-entity set over its source NBT.
        std::unordered_map<std::int64_t, Owner> decoded;
        std::unordered_map<std::int64_t, nbt::Value> rawDecoded;
        const auto* chunkXTag = root.get("xPos");
        const auto* chunkZTag = root.get("zPos");
        if ((chunkXTag == nullptr) != (chunkZTag == nullptr) ||
            (chunkXTag && chunkXTag->tag != nbt::Int) ||
            (chunkZTag && chunkZTag->tag != nbt::Int))
            throw std::runtime_error("invalid block-entity chunk coordinates");
        const std::optional<std::int32_t> chunkX = chunkXTag
            ? std::optional<std::int32_t>(chunkXTag->i) : std::nullopt;
        const std::optional<std::int32_t> chunkZ = chunkZTag
            ? std::optional<std::int32_t>(chunkZTag->i) : std::nullopt;
        std::unordered_set<std::int64_t> seenPositions;
        std::size_t recordCount = 0;
        std::size_t nbtFootprint = 0;
        for (auto& [k, v] : root.comp) {
            if (k != "block_entities" && k != "TileEntities") continue;
            if (v.tag != nbt::List)
                throw std::runtime_error(
                    "block-entity data field is not an NBT list");
            if (!v.list.empty() && v.listElement != nbt::Compound)
                throw std::runtime_error(
                    "block-entity NBT list does not contain compounds");
            for (const auto& e : v.list) {
                if (recordCount == maxChunkRecords_)
                    throw std::length_error(
                        "too many block entities in one chunk");
                ++recordCount;
                accountNbtFootprint(e, nbtFootprint,
                                    maxChunkNbtFootprint_);
                if (e.tag != nbt::Compound)
                    throw std::runtime_error(
                        "block-entity list contains a non-compound record");
                const auto key = BlockEntityNbtCodec::entityKey(e);
                const auto* id = e.get("id");
                if (!key || !id || id->tag != nbt::String)
                    throw std::runtime_error(
                        "invalid block-entity record or packed position");
                if (chunkX &&
                    ((posKeyUnpackX(*key) >> 4) != *chunkX ||
                     (posKeyUnpackZ(*key) >> 4) != *chunkZ))
                    throw std::runtime_error(
                        "block-entity position does not belong to its chunk");
                if (!containsWorldY(posKeyUnpackY(*key)))
                    throw std::runtime_error(
                        "block-entity Y coordinate is outside dimension bounds");
                if (!seenPositions.insert(*key).second)
                    throw std::runtime_error(
                        "duplicate block-entity position in chunk NBT");
                auto entry = BlockEntityNbtCodec::decodeRecord(e);
                if (entry)
                    decoded.try_emplace(entry->first,
                                        std::move(entry->second));
                rawDecoded.try_emplace(*key, e);
            }
        }
        return [this, decoded = std::move(decoded),
                rawDecoded = std::move(rawDecoded), chunkX, chunkZ]() mutable {
            std::lock_guard lock(mutex_);
            for (auto it = decoded.begin(); it != decoded.end();) {
                // Existing owners, opaque records, and allocation-fallback
                // tombstones all win over a delayed import. In particular, a
                // null owner kept after tombstone allocation failure must not
                // be re-added to the chunk index by this stale commit.
                if (removed_.count(it->first) || map_.contains(it->first) ||
                    rawNbt_.contains(it->first))
                    it = decoded.erase(it);
                else ++it;
            }
            for (auto it = rawDecoded.begin(); it != rawDecoded.end();) {
                if (removed_.count(it->first) || map_.count(it->first) ||
                    rawNbt_.count(it->first))
                    it = rawDecoded.erase(it);
                else
                    ++it;
            }

            std::unordered_set<std::int64_t> indexKeys;
            std::unordered_map<std::int64_t, std::size_t> recordAdditions;
            std::unordered_map<std::int64_t, std::size_t> rawAdditions;
            std::size_t totalRecordAdditions = 0;
            std::size_t totalRawAdditions = 0;
            indexKeys.reserve(decoded.size() + rawDecoded.size());
            for (const auto& [key, owner] : decoded) {
                (void)owner;
                const bool first = indexKeys.insert(key).second;
                if (first && !hasChunkIndexKeyLocked(key)) {
                    ++recordAdditions[chunkIndexKey(key)];
                    ++totalRecordAdditions;
                }
            }
            for (const auto& [key, value] : rawDecoded) {
                const bool first = indexKeys.insert(key).second;
                if (first && !hasChunkIndexKeyLocked(key)) {
                    ++recordAdditions[chunkIndexKey(key)];
                    ++totalRecordAdditions;
                }
                std::size_t footprint = 0;
                accountNbtFootprint(value, footprint,
                                    maxChunkNbtFootprint_);
                rawAdditions[chunkIndexKey(key)] += footprint;
                totalRawAdditions += footprint;
            }

            for (const auto& [chunk, addition] : recordAdditions) {
                const auto current = chunkRecordCountLocked(chunk);
                if (current > maxChunkRecords_ ||
                    addition > maxChunkRecords_ - current)
                    throw std::length_error(
                        "block-entity store chunk record limit exceeded");
            }
            for (const auto& [chunk, addition] : rawAdditions) {
                const auto current = rawNbtFootprintByChunk_.contains(chunk)
                    ? rawNbtFootprintByChunk_.at(chunk) : 0;
                if (current > maxChunkNbtFootprint_ ||
                    addition > maxChunkNbtFootprint_ - current)
                    throw std::length_error(
                        "block-entity store chunk NBT limit exceeded");
            }
            const auto liveRecords = chunkIndex_.liveRecordCount();
            if (liveRecords > maxStoreRecords_ ||
                totalRecordAdditions > maxStoreRecords_ - liveRecords)
                throw std::length_error(
                    "block-entity store record limit exceeded");
            if (rawNbtBytes_ > maxStoreRawNbtFootprint_ ||
                totalRawAdditions >
                    maxStoreRawNbtFootprint_ - rawNbtBytes_)
                throw std::length_error(
                    "block-entity store NBT memory limit exceeded");

            std::vector<std::int64_t> createdRawFootprintChunks;
            createdRawFootprintChunks.reserve(rawAdditions.size());
            try {
                reserveChunkIndexForKeysLocked(indexKeys);
                map_.reserve(map_.size() + decoded.size());
                rawNbt_.reserve(rawNbt_.size() + rawDecoded.size());
                rawNbtFootprintByChunk_.reserve(
                    rawNbtFootprintByChunk_.size() + rawAdditions.size());
                for (const auto& [chunk, addition] : rawAdditions) {
                    (void)addition;
                    const auto [entry, inserted] =
                        rawNbtFootprintByChunk_.try_emplace(chunk, 0);
                    (void)entry;
                    if (inserted) createdRawFootprintChunks.push_back(chunk);
                }
            } catch (...) {
                cancelChunkIndexReservationsLocked(indexKeys);
                for (const auto chunk : createdRawFootprintChunks) {
                    const auto entry = rawNbtFootprintByChunk_.find(chunk);
                    if (entry != rawNbtFootprintByChunk_.end() &&
                        entry->second == 0)
                        rawNbtFootprintByChunk_.erase(entry);
                }
                throw;
            }
            // Existing live entries win; node transfer avoids replacing them
            // and does not allocate after reserve has established capacity.
            map_.merge(decoded);
            rawNbt_.merge(rawDecoded);
            rawNbtBytes_ += totalRawAdditions;
            for (const auto& [chunk, addition] : rawAdditions)
                rawNbtFootprintByChunk_[chunk] += addition;
            for (const auto key : indexKeys) addKeyToChunkIndexLocked(key);
            if (chunkX && chunkZ) {
                for (auto it = removed_.begin(); it != removed_.end();) {
                    if ((posKeyUnpackX(*it) >> 4) == *chunkX &&
                        (posKeyUnpackZ(*it) >> 4) == *chunkZ)
                        it = removed_.erase(it);
                    else
                        ++it;
                }
            }
        };
    }

    void readChunkNbt(const nbt::Value& root) {
        auto commit = prepareChunkNbt(root);
        commit();
    }

    // Validate persisted block-entity data without publishing it into the
    // live store. Recovery uses this before promoting a staged region so a
    // malformed extras list cannot replace a readable primary chunk.
    static void validateChunkNbt(const nbt::Value& root,
                                 std::int32_t minY = -64,
                                 std::int32_t maxYExclusive = 320) {
        BlockEntityNbtCodec::validateInventoryPayloads(root);
        BlockEntityStore staged(minY, maxYExclusive);
        staged.readChunkNbt(root);
    }

private:
    bool snapshotChunkNbt(
        std::int32_t cx, std::int32_t cz,
        BlockEntityChunkCodec::Snapshot& snapshot,
        bool returnIfContended) const {
        struct OwnerReference {
            std::int64_t key = 0;
            Owner owner;
            bool mapEntryPresent = false;
        };

        std::size_t consecutiveContentions = 0;
        auto retryDelay = std::chrono::microseconds(50);
        for (;;) {
            std::vector<std::int64_t> indexedKeys;
            std::vector<OwnerReference> ownerReferences;
            {
                std::lock_guard lock(mutex_);
                indexedKeys = chunkIndex_.keysForChunk(cx, cz);
                ownerReferences.reserve(indexedKeys.size());
                for (const auto key : indexedKeys) {
                    const auto owner = map_.find(key);
                    ownerReferences.push_back({
                        key,
                        owner == map_.end() ? Owner{} : owner->second,
                        owner != map_.end()});
                }
            }

            std::vector<std::recursive_mutex*> stateMutexes;
            stateMutexes.reserve(ownerReferences.size());
            for (const auto& reference : ownerReferences) {
                if (!reference.owner) continue;
                if (!reference.owner->stateMtx)
                    throw std::logic_error(
                        "block-entity owner has no state mutex");
                stateMutexes.push_back(reference.owner->stateMtx.get());
            }
            std::sort(stateMutexes.begin(), stateMutexes.end(),
                      std::less<std::recursive_mutex*>{});
            stateMutexes.erase(
                std::unique(stateMutexes.begin(), stateMutexes.end()),
                stateMutexes.end());

            std::vector<std::unique_lock<std::recursive_mutex>> ownerLocks;
            ownerLocks.reserve(stateMutexes.size());
            bool retry = false;
            for (auto* stateMutex : stateMutexes) {
                std::unique_lock<std::recursive_mutex> lock(
                    *stateMutex, std::defer_lock);
                if (!lock.try_lock()) {
                    retry = true;
                    break;
                }
                ownerLocks.push_back(std::move(lock));
            }
            if (!retry) {
                std::unique_lock<std::recursive_mutex> storeLock(
                    mutex_, std::defer_lock);
                if (!storeLock.try_lock()) {
                    retry = true;
                } else {
                    const auto currentKeys = chunkIndex_.keysForChunk(cx, cz);
                    bool ownersUnchanged = currentKeys == indexedKeys;
                    for (const auto& reference : ownerReferences) {
                        const auto current = map_.find(reference.key);
                        const bool present = current != map_.end();
                        if (present != reference.mapEntryPresent ||
                            (present && current->second != reference.owner)) {
                            ownersUnchanged = false;
                            break;
                        }
                    }

                    if (ownersUnchanged) {
                        snapshot = {};
                        snapshot.typedRecords.reserve(indexedKeys.size());
                        snapshot.rawTemplates.reserve(indexedKeys.size());
                        snapshot.typedKeys.reserve(indexedKeys.size());
                        for (const auto key : indexedKeys) {
                            if (const auto owner = map_.find(key);
                                owner != map_.end()) {
                                snapshot.typedKeys.insert(key);
                                if (owner->second) {
                                    BlockEntityState detached = *owner->second;
                                    snapshot.typedRecords.push_back(
                                        {key, std::move(detached)});
                                }
                            }
                            if (const auto raw = rawNbt_.find(key);
                                raw != rawNbt_.end())
                                snapshot.rawTemplates.emplace(key, raw->second);
                        }
                        return true;
                    }
                    retry = true;
                }
            }

            ownerLocks.clear();
            snapshotContentions_.fetch_add(1, std::memory_order_relaxed);
            if (returnIfContended) return false;
            if (consecutiveContentions++ < 8) {
                std::this_thread::yield();
            } else {
                // Durable snapshots must eventually serialize rather than
                // omit records under load. Once the brief optimistic window
                // is exhausted, back off instead of hot-spinning against
                // active menu/tick writers. Admission snapshots still use
                // returnIfContended and fail immediately above.
                std::this_thread::sleep_for(retryDelay);
                retryDelay = std::min(retryDelay * 2,
                                      std::chrono::microseconds(4000));
            }
        }
    }

    bool containsWorldY(std::int32_t y) const noexcept {
        return y >= minY_ && y < maxYExclusive_;
    }

    static void accountNbtFootprint(const nbt::Value& value,
                                    std::size_t& used,
                                    std::size_t budget) {
        BlockEntityChunkCodec::accountNbtFootprint(value, used, budget);
    }

    static std::int64_t chunkIndexKey(std::int32_t cx,
                                      std::int32_t cz) noexcept {
        return BlockEntityChunkIndex::chunkKey(cx, cz);
    }
    static std::int64_t chunkIndexKey(std::int64_t entityKey) noexcept {
        return BlockEntityChunkIndex::chunkKey(entityKey);
    }
    bool hasRawRecordLocked(std::int64_t key) const {
        const auto raw = rawNbt_.find(key);
        return raw != rawNbt_.end() && raw->second.tag != nbt::End;
    }
    void clearFallbackTombstonesForChunkLocked(std::int32_t cx,
                                                std::int32_t cz) {
        for (auto it = map_.begin(); it != map_.end();) {
            if (!it->second && (posKeyUnpackX(it->first) >> 4) == cx &&
                (posKeyUnpackZ(it->first) >> 4) == cz)
                it = map_.erase(it);
            else
                ++it;
        }
        for (auto it = rawNbt_.begin(); it != rawNbt_.end();) {
            if (it->second.tag == nbt::End &&
                (posKeyUnpackX(it->first) >> 4) == cx &&
                (posKeyUnpackZ(it->first) >> 4) == cz)
                it = rawNbt_.erase(it);
            else
                ++it;
        }
    }
    bool hasChunkIndexKeyLocked(std::int64_t key) const {
        return chunkIndex_.contains(key);
    }
    std::size_t chunkRecordCountLocked(std::int64_t chunk) const {
        return chunkIndex_.recordCount(chunk);
    }
    bool canAddRecordLocked(std::int64_t key) const {
        return chunkIndex_.canAddRecord(key);
    }
    bool canReplaceRawNbtLocked(std::int64_t key,
                                std::size_t newFootprint,
                                std::size_t recordAddition = 0) const {
        const auto chunk = chunkIndexKey(key);
        const auto existingRaw = rawNbt_.find(key);
        std::size_t oldFootprint = 0;
        if (existingRaw != rawNbt_.end() &&
            existingRaw->second.tag != nbt::End) {
            accountNbtFootprint(existingRaw->second, oldFootprint,
                                maxChunkNbtFootprint_);
        }
        const auto chunkBytesIt = rawNbtFootprintByChunk_.find(chunk);
        const std::size_t chunkBytes = chunkBytesIt ==
                rawNbtFootprintByChunk_.end()
            ? 0 : chunkBytesIt->second;
        if (newFootprint > maxChunkNbtFootprint_ ||
            chunkBytes < oldFootprint ||
            chunkBytes - oldFootprint > maxChunkNbtFootprint_ ||
            newFootprint > maxChunkNbtFootprint_ -
                               (chunkBytes - oldFootprint) ||
            rawNbtBytes_ < oldFootprint ||
            rawNbtBytes_ - oldFootprint > maxStoreRawNbtFootprint_ ||
            newFootprint > maxStoreRawNbtFootprint_ -
                               (rawNbtBytes_ - oldFootprint))
            return false;
        if (recordAddition != 0) {
            const auto chunkRecords = chunkRecordCountLocked(chunk);
            if (chunkRecords > maxChunkRecords_ ||
                recordAddition > maxChunkRecords_ - chunkRecords ||
                chunkIndex_.liveRecordCount() > maxStoreRecords_ ||
                recordAddition > maxStoreRecords_ -
                    chunkIndex_.liveRecordCount())
                return false;
        }
        return true;
    }
    bool eraseRawNbtLocked(std::int64_t key) {
        const auto it = rawNbt_.find(key);
        if (it == rawNbt_.end()) return false;
        std::size_t footprint = 0;
        if (it->second.tag != nbt::End)
            accountNbtFootprint(it->second, footprint,
                                maxChunkNbtFootprint_);
        const auto chunk = chunkIndexKey(key);
        if (rawNbtBytes_ >= footprint) rawNbtBytes_ -= footprint;
        else rawNbtBytes_ = 0;
        if (const auto chunkBytes = rawNbtFootprintByChunk_.find(chunk);
            chunkBytes != rawNbtFootprintByChunk_.end()) {
            if (chunkBytes->second >= footprint)
                chunkBytes->second -= footprint;
            else
                chunkBytes->second = 0;
            if (chunkBytes->second == 0)
                rawNbtFootprintByChunk_.erase(chunkBytes);
        }
        rawNbt_.erase(it);
        return true;
    }
    void reserveChunkIndexForKeyLocked(std::int64_t key) {
        chunkIndex_.reserveForKey(key);
    }
    void cancelChunkIndexReservationLocked(std::int64_t key) {
        chunkIndex_.cancelReservation(key);
    }
    void reserveChunkIndexForKeysLocked(
        const std::unordered_set<std::int64_t>& keys) {
        chunkIndex_.reserveForKeys(keys);
    }
    void cancelChunkIndexReservationsLocked(
        const std::unordered_set<std::int64_t>& keys) {
        chunkIndex_.cancelReservations(keys);
    }
    bool addKeyToChunkIndexLocked(std::int64_t key) {
        return chunkIndex_.add(key);
    }
    void removeKeyFromChunkIndexLocked(std::int64_t key) {
        (void)chunkIndex_.remove(key);
    }

    using ActiveDirtyCallbackKey =
        std::pair<const BlockEntityStore*, std::int64_t>;

    static std::vector<ActiveDirtyCallbackKey>& activeDirtyCallbacks() {
        static thread_local std::vector<ActiveDirtyCallbackKey> active;
        return active;
    }

    bool invokeDirtyCallbackForChunk(
        std::int32_t chunkX, std::int32_t chunkZ,
        const std::shared_ptr<const DirtyCallback>& callback,
        bool requireMutationGate = false) const {
        if (!callback) return true;
        if (requireMutationGate && !hasRequiredMutationGate()) return false;
        const ActiveDirtyCallbackKey activeKey{
            this, chunkIndexKey(chunkX, chunkZ)};
        auto& activeCallbacks = BlockEntityStore::activeDirtyCallbacks();
        if (std::find(activeCallbacks.begin(), activeCallbacks.end(), activeKey) !=
            activeCallbacks.end())
            return false;
        activeCallbacks.push_back(activeKey);
        struct PopActiveKey {
            std::vector<ActiveDirtyCallbackKey>& callbacks;
            ~PopActiveKey() { callbacks.pop_back(); }
        } pop{activeCallbacks};
        (*callback)(chunkX, chunkZ);
        return true;
    }

    bool invokeDirtyCallbackForKey(
        std::int64_t key,
        const std::shared_ptr<const DirtyCallback>& callback,
        bool requireMutationGate = false) const {
        return invokeDirtyCallbackForChunk(
            posKeyUnpackX(key) >> 4, posKeyUnpackZ(key) >> 4,
            callback, requireMutationGate);
    }

    bool hasRequiredMutationGate() const noexcept {
        return !mutationGateIdentity_ ||
               MutationGateLease::heldByCurrentThread(mutationGateIdentity_);
    }

    mutable std::recursive_mutex mutex_;
    BlockEntityChunkIndex chunkIndex_;
    std::unordered_map<std::int64_t, Owner> map_;
    std::unordered_map<std::int64_t, nbt::Value> rawNbt_;
    std::pmr::unordered_set<std::int64_t> removed_;
    std::unordered_map<std::int64_t, std::size_t> rawNbtFootprintByChunk_;
    std::size_t rawNbtBytes_ = 0;
    mutable std::atomic<std::size_t> snapshotContentions_{0};
    std::shared_ptr<const DirtyCallback> dirtyCallback_;
    const void* mutationGateIdentity_ = nullptr;
    const std::int32_t minY_;
    const std::int32_t maxYExclusive_;
    const std::size_t maxChunkRecords_;
    const std::size_t maxChunkNbtFootprint_;
    const std::size_t maxStoreRecords_;
    const std::size_t maxStoreRawNbtFootprint_;
};

} // namespace cppfm
#endif // CPPFM_GAME_BLOCK_ENTITIES_HPP_INCLUDED
