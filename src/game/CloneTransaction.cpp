#include "CloneTransaction.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cppfm::clone_transaction {

std::optional<StagedCloneView> StagedCloneView::fromLegacyVectors(
    const std::vector<World::BlockUpdate>& replacementBlocks,
    const std::vector<World::BlockUpdate>& originalBlocks,
    const std::vector<BlockEntityStore::NbtReplacement>&
        replacementEntities) {
    if (replacementBlocks.size() != originalBlocks.size() ||
        replacementBlocks.size() != replacementEntities.size())
        return std::nullopt;

    std::vector<StagedChangeView> changes;
    changes.reserve(replacementBlocks.size());
    for (std::size_t i = 0; i < replacementBlocks.size(); ++i) {
        const auto& replacementBlock = replacementBlocks[i];
        const auto& originalBlock = originalBlocks[i];
        const auto& replacementEntity = replacementEntities[i];
        const auto samePosition = [&](std::int32_t x, std::int32_t y,
                                      std::int32_t z) {
            return replacementBlock.x == x && replacementBlock.y == y &&
                   replacementBlock.z == z;
        };
        if (!samePosition(originalBlock.x, originalBlock.y, originalBlock.z) ||
            !samePosition(replacementEntity.x, replacementEntity.y,
                          replacementEntity.z))
            return std::nullopt;

        changes.push_back({
            std::cref(originalBlock), std::cref(replacementBlock),
            std::cref(replacementEntity),
            {replacementEntity.x, replacementEntity.y, replacementEntity.z,
             std::nullopt}});
    }
    return StagedCloneView(std::move(changes));
}

ApplyResult applyStagedClone(
    World& world, BlockEntityStore& entities,
    const std::vector<World::BlockUpdate>& blockUpdates,
    const std::vector<World::BlockUpdate>& originalBlockStates,
    const std::vector<BlockEntityStore::NbtReplacement>& entityUpdates,
    const ReplaceEntity& replaceEntity,
    const AdmitAnvilProjection& admitAnvilProjection) {
    std::optional<StagedCloneView> staged;
    try {
        staged = StagedCloneView::fromLegacyVectors(
            blockUpdates, originalBlockStates, entityUpdates);
    } catch (...) {
        return ApplyResult::SnapshotFailed;
    }
    if (!staged) return ApplyResult::Rejected;

    auto mutationGate = world.lockMutationGate();
    try {
        if (!entities.canReplaceNbtBatch(entityUpdates))
            return ApplyResult::Rejected;
    } catch (...) {
        return ApplyResult::SnapshotFailed;
    }

    std::vector<std::pair<std::int32_t, std::int32_t>> chunks;
    std::unordered_set<std::int64_t> affectedKeys;
    try {
        chunks.reserve(entityUpdates.size());
        affectedKeys.reserve(entityUpdates.size());
        for (const auto& change : staged->changes()) {
            const auto& originalBlock = change.originalBlock.get();
            const auto& replacementBlock = change.replacementBlock.get();
            chunks.emplace_back(replacementBlock.x >> 4,
                                replacementBlock.z >> 4);
            affectedKeys.insert(posKey(originalBlock.x, originalBlock.y,
                                       originalBlock.z));
        }
        std::sort(chunks.begin(), chunks.end());
        chunks.erase(std::unique(chunks.begin(), chunks.end()), chunks.end());

        std::unordered_map<std::int64_t, nbt::Value> oldEntities;
        oldEntities.reserve(entityUpdates.size());
        for (const auto& [cx, cz] : chunks) {
            auto serialized = nbt::Value::makeList(nbt::Compound);
            if (!entities.writeChunkNbt(cx, cz, serialized, {}, true))
                return ApplyResult::SnapshotFailed;
            for (auto& entity : serialized.list) {
                const auto* x = entity.get("x");
                const auto* y = entity.get("y");
                const auto* z = entity.get("z");
                if (!x || x->tag != nbt::Int || !y || y->tag != nbt::Int ||
                    !z || z->tag != nbt::Int)
                    continue;
                const auto key = posKey(x->i, y->i, z->i);
                if (affectedKeys.contains(key))
                    oldEntities.insert_or_assign(key, std::move(entity));
            }
        }

        for (auto& change : staged->changes_) {
            const auto& originalBlock = change.originalBlock.get();
            if (const auto found = oldEntities.find(
                    posKey(originalBlock.x, originalBlock.y,
                           originalBlock.z));
                found != oldEntities.end()) {
                change.originalEntity = {
                    originalBlock.x, originalBlock.y, originalBlock.z,
                    std::move(found->second)};
            }
        }

        // Test the complete post-clone chunk images before publishing either
        // block states or raw block-entity NBT.
        if (!admitAnvilProjection(blockUpdates, entityUpdates))
            return ApplyResult::AnvilRejected;

        const auto replaceSafely = [&](
            const BlockEntityStore::NbtReplacement& replacement) {
            try {
                return replaceEntity(replacement);
            } catch (...) {
                return false;
            }
        };

        std::vector<World::BlockChange> committedBlockChanges;
        bool blockBatchCommitted = false;
        bool applied = false;
        try {
            blockBatchCommitted = world.setBlockBatchDeferred(
                blockUpdates, committedBlockChanges);
            applied = blockBatchCommitted;
            if (applied) {
                // Remove entries that disappear before adding replacements.
                // Keep present-to-present updates in place so typed owners
                // retain the identity held by open menus when their block
                // type itself did not change.
                for (const auto& change : staged->changes()) {
                    const auto& update = change.replacementEntity.get();
                    if (update.entity) continue;
                    const auto& replacementBlock =
                        change.replacementBlock.get();
                    const BlockEntityStore::NbtReplacement clear{
                        replacementBlock.x, replacementBlock.y,
                        replacementBlock.z, std::nullopt};
                    if (!replaceSafely(clear)) {
                        applied = false;
                        break;
                    }
                }
                if (applied) {
                    for (const auto& change : staged->changes()) {
                        const auto& update = change.replacementEntity.get();
                        if (update.entity && !replaceSafely(update)) {
                            applied = false;
                            break;
                        }
                    }
                }
            }
        } catch (...) {
            applied = false;
        }
        if (applied) {
            return world.publishBlockBatch(committedBlockChanges)
                ? ApplyResult::Applied
                : ApplyResult::AppliedWithNotificationFailure;
        }
        if (!blockBatchCommitted)
            return ApplyResult::ApplyFailedRolledBack;

        bool restored = true;
        const auto restoreEntities = [&](bool clearAll) {
            bool entityStateRestored = true;
            for (const auto& change : staged->changes()) {
                const auto& original = change.originalEntity;
                if (!clearAll && original.entity) continue;
                const auto& originalBlock = change.originalBlock.get();
                const BlockEntityStore::NbtReplacement clear{
                    originalBlock.x, originalBlock.y, originalBlock.z,
                    std::nullopt};
                if (!replaceSafely(clear)) entityStateRestored = false;
            }
            for (const auto& change : staged->changes()) {
                if (!change.originalEntity.entity) continue;
                if (!replaceSafely(change.originalEntity))
                    entityStateRestored = false;
            }
            return entityStateRestored;
        };
        if (!restoreEntities(false)) {
            // If in-place restoration is temporarily over budget, clearing
            // the affected set first recreates the preflighted old footprint.
            if (!restoreEntities(true)) restored = false;
        }
        // No block observers ran during the failed attempt, so restore the
        // previous block image silently as well. The world mutation gate stays
        // held throughout both entity and block restoration; listeners see
        // neither half of a failed clone transaction.
        try {
            std::vector<World::BlockChange> discardedChanges;
            if (!world.setBlockBatchDeferred(originalBlockStates,
                                             discardedChanges))
                restored = false;
        } catch (...) {
            restored = false;
        }
        return restored ? ApplyResult::ApplyFailedRolledBack
                        : ApplyResult::RollbackFailed;
    } catch (...) {
        // All throwing snapshot/preflight work precedes the first mutation.
        return ApplyResult::SnapshotFailed;
    }
}

} // namespace cppfm::clone_transaction
