#include "game/BlockEntities.hpp"
#include "game/BlockEntityKinds.hpp"
#include "game/BlockEntityChunkWire.hpp"
#include "game/BlockEntityDrops.hpp"
#include "game/DatapackManager.hpp"
#include "game/RegionFile.hpp"
#include "core/NBT.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <memory>
#include <memory_resource>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

static_assert(std::is_same_v<cppfm::BlockEntityStore::Owner,
                             cppfm::BlockEntityOwner>);

namespace {

int failures = 0;
int assertions = 0;
void check(bool condition, const char* description);

void testBlockEntityKindMapping() {
    using Kind = cppfm::BlockEntity::Kind;
    check(cppfm::blockEntityKindForBlockName("minecraft:chest") == Kind::Chest &&
              cppfm::blockEntityKindForBlockName("minecraft:trapped_chest") == Kind::Chest &&
              cppfm::blockEntityKindForBlockName("minecraft:ender_chest") == std::nullopt,
          "block inventory mapping distinguishes regular and ender chests");
    check(cppfm::blockEntityKindForBlockName("minecraft:barrel") == Kind::Barrel &&
              cppfm::blockEntityKindForBlockName("minecraft:blue_shulker_box") == Kind::ShulkerBox &&
              cppfm::blockEntityKindForBlockName("minecraft:hopper") == Kind::Hopper,
          "block inventory mapping covers barrel, shulker, and hopper owners");
    check(cppfm::blockEntityKindForBlockName("minecraft:dispenser") == Kind::Dispenser &&
              cppfm::blockEntityKindForBlockName("minecraft:dropper") == Kind::Dropper &&
              cppfm::blockEntityKindForBlockName("minecraft:smoker") == Kind::Furnace,
          "block inventory mapping covers automation and furnace variants");
    check(cppfm::blockEntityKindForBlockName("minecraft:brewing_stand") == Kind::Brewing &&
              cppfm::blockEntityKindForBlockName("minecraft:crafter") == Kind::Crafter &&
              cppfm::blockEntityKindForBlockName("minecraft:oak_sign") == Kind::Sign &&
              cppfm::blockEntityKindForBlockName("minecraft:oak_wall_hanging_sign") == Kind::Sign &&
              cppfm::blockEntityKindForBlockName("minecraft:stone") == std::nullopt,
          "block entity mapping covers sign variants, brewing/crafter, and rejects ordinary blocks");
}

class ToggleFailingMemoryResource final : public std::pmr::memory_resource {
public:
    bool failAllocations = false;

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (failAllocations) throw std::bad_alloc();
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* memory, std::size_t bytes,
                       std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment);
    }
    bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

void check(bool condition, const char* description) {
    ++assertions;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", description);
}

void testAdmissionSnapshotFailsFastOnCrossOwnerContention() {
    using namespace cppfm;
    BlockEntityStore store;
    const auto first = store.createShared(
        posKey(1, 64, 1), BlockEntity::Kind::Chest);
    const auto second = store.createShared(
        posKey(2, 64, 1), BlockEntity::Kind::Chest);
    auto preflight = nbt::Value::makeList(nbt::Compound);
    store.writeChunkNbt(0, 0, preflight);
    check(first->stateMtx != second->stateMtx && preflight.list.size() == 2,
          "cross-owner contention fixture contains two separately locked records");
    std::atomic<int> ownersLocked{0};
    std::atomic<bool> beginSnapshots{false};
    std::atomic<bool> firstSnapshotSucceeded{true};
    std::atomic<bool> secondSnapshotSucceeded{true};

    const auto snapshotWhileHolding = [&](const BlockEntityStore::Owner& owner,
                                          std::atomic<bool>& succeeded) {
        std::lock_guard ownerLock(*owner->stateMtx);
        ownersLocked.fetch_add(1, std::memory_order_release);
        while (!beginSnapshots.load(std::memory_order_acquire))
            std::this_thread::yield();
        auto serialized = nbt::Value::makeList(nbt::Compound);
        succeeded.store(
            store.writeChunkNbt(0, 0, serialized, {}, true),
            std::memory_order_release);
    };

    std::thread firstSession(snapshotWhileHolding,
                             std::cref(first),
                             std::ref(firstSnapshotSucceeded));
    std::thread secondSession(snapshotWhileHolding,
                              std::cref(second),
                              std::ref(secondSnapshotSucceeded));
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (ownersLocked.load(std::memory_order_acquire) != 2 &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool bothOwnersLocked =
        ownersLocked.load(std::memory_order_acquire) == 2;
    beginSnapshots.store(true, std::memory_order_release);
    firstSession.join();
    secondSession.join();

    check(bothOwnersLocked,
          "concurrent menu sessions hold distinct block-entity owners");
    check(!firstSnapshotSucceeded.load(std::memory_order_acquire) ||
              !secondSnapshotSucceeded.load(std::memory_order_acquire),
          "contended Anvil admission snapshot fails fast instead of waiting for a peer owner");
}

void testContendedSnapshotPreservesExistingOutput() {
    using namespace cppfm;
    BlockEntityStore store;
    const auto first = store.createShared(
        posKey(1, 64, 1), BlockEntity::Kind::Chest);
    const auto second = store.createShared(
        posKey(2, 64, 1), BlockEntity::Kind::Chest);
    std::atomic<bool> ownersLocked{false};
    std::atomic<bool> releaseOwners{false};
    std::thread holder([&] {
        std::unique_lock firstLock(*first->stateMtx, std::defer_lock);
        std::unique_lock secondLock(*second->stateMtx, std::defer_lock);
        std::lock(firstLock, secondLock);
        ownersLocked.store(true, std::memory_order_release);
        while (!releaseOwners.load(std::memory_order_acquire))
            std::this_thread::yield();
    });

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (!ownersLocked.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    auto output = nbt::Value::makeList(nbt::Compound);
    auto sentinel = nbt::Value::makeCompound();
    sentinel.set("x", nbt::Value::makeInt(123));
    output.list.push_back(std::move(sentinel));
    const bool serialized = store.writeChunkNbt(0, 0, output, {}, true);
    const bool unchanged = output.list.size() == 1 &&
        output.list.front().get("x") &&
        output.list.front().get("x")->i == 123;
    releaseOwners.store(true, std::memory_order_release);
    holder.join();

    check(ownersLocked.load(std::memory_order_acquire),
          "snapshot-contention fixture locks all owners before serialization");
    check(!serialized,
          "snapshot returns without waiting when all owners are locked elsewhere");
    check(unchanged,
          "contended snapshot leaves previously published NBT output unchanged");
}

void testDurableSnapshotBacksOffAndWaitsForConsistentOwnerState() {
    using namespace cppfm;
    BlockEntityStore store;
    const auto key = posKey(3, 64, 5);
    const auto owner = store.createShared(key, BlockEntity::Kind::Chest);
    {
        std::lock_guard lock(*owner->stateMtx);
        owner->chest.slots[0] = ItemStack::ofName("minecraft:stone", 1);
    }

    std::atomic<bool> mutationHoldingOwner{false};
    std::atomic<bool> releaseMutation{false};
    std::thread mutator([&] {
        std::lock_guard lock(*owner->stateMtx);
        owner->chest.slots[0] = ItemStack::ofName("minecraft:diamond", 3);
        mutationHoldingOwner.store(true, std::memory_order_release);
        while (!releaseMutation.load(std::memory_order_acquire))
            std::this_thread::yield();
    });

    const auto ownerDeadline = std::chrono::steady_clock::now() +
                               std::chrono::seconds(2);
    while (!mutationHoldingOwner.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < ownerDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    std::atomic<bool> snapshotStarted{false};
    std::atomic<bool> snapshotFinished{false};
    std::atomic<bool> snapshotSucceeded{false};
    const auto contentionCountBefore = store.snapshotContentionCount();
    nbt::Value serialized = nbt::Value::makeList(nbt::Compound);
    std::thread writer;
    if (mutationHoldingOwner.load(std::memory_order_acquire)) {
        writer = std::thread([&] {
            snapshotStarted.store(true, std::memory_order_release);
            snapshotSucceeded.store(store.writeChunkNbt(0, 0, serialized),
                                    std::memory_order_release);
            snapshotFinished.store(true, std::memory_order_release);
        });
    }

    const auto startDeadline = std::chrono::steady_clock::now() +
                               std::chrono::seconds(2);
    while (writer.joinable() &&
           !snapshotStarted.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < startDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto contentionDeadline = std::chrono::steady_clock::now() +
                                    std::chrono::seconds(2);
    while (writer.joinable() &&
           store.snapshotContentionCount() == contentionCountBefore &&
           std::chrono::steady_clock::now() < contentionDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool snapshotReachedContention =
        store.snapshotContentionCount() > contentionCountBefore;
    const bool snapshotWaitedForMutation =
        writer.joinable() &&
        snapshotStarted.load(std::memory_order_acquire) &&
        snapshotReachedContention &&
        !snapshotFinished.load(std::memory_order_acquire);

    releaseMutation.store(true, std::memory_order_release);
    mutator.join();
    if (writer.joinable()) writer.join();

    bool capturedCommittedState = false;
    if (serialized.tag == nbt::List && serialized.list.size() == 1) {
        const auto* items = serialized.list.front().get("Items");
        if (items && items->tag == nbt::List && items->list.size() == 1) {
            const auto* id = items->list.front().get("id");
            const auto* count = items->list.front().get("count");
            capturedCommittedState = id && id->tag == nbt::String &&
                id->str == "minecraft:diamond" && count &&
                count->tag == nbt::Int && count->i == 3;
        }
    }
    check(mutationHoldingOwner.load(std::memory_order_acquire),
          "snapshot contention fixture acquires the live owner lock");
    check(snapshotWaitedForMutation &&
              snapshotSucceeded.load(std::memory_order_acquire),
          "durable snapshot waits with backoff rather than dropping a contended record");
    check(capturedCommittedState,
          "detached block-entity snapshot observes the complete committed inventory state");
}

void testBlockEntityAssignmentPreservesOwnerLock() {
    using namespace cppfm;
    BlockEntity live;
    live.kind = BlockEntity::Kind::Chest;
    live.sign.front[0] = "old value";
    const auto ownerLock = live.stateMtx;

    BlockEntity candidate;
    candidate.kind = BlockEntity::Kind::Sign;
    candidate.sign.front[0] = "replacement";
    candidate.sign.back[3] = "back side";
    live = candidate;

    check(live.kind == BlockEntity::Kind::Sign &&
              live.sign.front[0] == "replacement" &&
              live.sign.back[3] == "back side" &&
              live.stateMtx == ownerLock,
          "block-entity copy assignment commits complete state without replacing the live owner lock");
}

void testPackedCoordinateOverflowCannotAliasExistingEntity() {
    using namespace cppfm;
    BlockEntityStore store;
    const auto wrappedKey = posKey(kMinPackedBlockCoordinate, 64, 0);
    const auto original = store.createShared(
        wrappedKey, BlockEntity::Kind::Chest);

    auto invalidEntity = nbt::Value::makeCompound();
    invalidEntity.set("id", nbt::Value::makeString("minecraft:chest"));
    const bool replaced = store.replaceNbtAt(
        kMaxPackedBlockCoordinate + 1, 64, 0, invalidEntity);

    BlockEntityStore::NbtReplacement batchReplacement{
        kMaxPackedBlockCoordinate + 1, 64, 0, invalidEntity};
    const bool batchAdmitted =
        store.canReplaceNbtBatch({batchReplacement});
    check(!replaced && !batchAdmitted &&
              store.get(wrappedKey) == original &&
              store.getAt(kMaxPackedBlockCoordinate + 1, 64, 0) == nullptr &&
              !store.containsAt(kMaxPackedBlockCoordinate + 1, 64, 0) &&
              store.size() == 1,
          "unrepresentable X coordinates cannot wrap around and replace a different block entity");
}

void testFallbackTombstoneDoesNotReenterChunkIndexOnDelayedImport() {
    using namespace cppfm;
    ToggleFailingMemoryResource tombstoneResource;
    BlockEntityStore store(
        -64, 320, 1,
        BlockEntityStore::kMaxChunkBlockEntityNbtFootprint,
        2, BlockEntityStore::kMaxStoreRawNbtFootprint,
        &tombstoneResource);
    const auto removedKey = posKey(1, 64, 1);
    nbt::Value root = nbt::Value::makeCompound();
    nbt::Value entities = nbt::Value::makeList(nbt::Compound);
    auto chest = nbt::Value::makeCompound();
    chest.set("id", nbt::Value::makeString("minecraft:chest"));
    chest.set("x", nbt::Value::makeInt(1));
    chest.set("y", nbt::Value::makeInt(64));
    chest.set("z", nbt::Value::makeInt(1));
    chest.set("Items", nbt::Value::makeList(nbt::Compound));
    entities.list.push_back(std::move(chest));
    root.set("block_entities", std::move(entities));
    store.readChunkNbt(root);
    auto staleImport = store.prepareChunkNbt(root);

    store.setDirtyCallback([](std::int32_t, std::int32_t) {});
    tombstoneResource.failAllocations = true;
    const bool removed =
        store.removeAfterWorldBlockReplacement(removedKey);
    tombstoneResource.failAllocations = false;
    staleImport();
    store.clearRemovalTombstonesForChunk(0, 0);
    const bool emptyAfterStaleImport = store.empty() && store.size() == 0;

    const auto recreated = store.tryCreateShared(
        posKey(2, 64, 1), BlockEntity::Kind::Chest);
    check(removed && emptyAfterStaleImport && recreated && store.size() == 1,
          "delayed import cannot leave an unowned chunk-index entry that consumes the final record slot");
}

void testMultiplePreparedImportsCannotResurrectRemovedEntity() {
    using namespace cppfm;
    BlockEntityStore store;
    const auto removedKey = posKey(1, 64, 1);

    auto root = nbt::Value::makeCompound();
    root.set("xPos", nbt::Value::makeInt(0));
    root.set("zPos", nbt::Value::makeInt(0));
    auto entities = nbt::Value::makeList(nbt::Compound);
    auto chest = nbt::Value::makeCompound();
    chest.set("id", nbt::Value::makeString("minecraft:chest"));
    chest.set("x", nbt::Value::makeInt(1));
    chest.set("y", nbt::Value::makeInt(64));
    chest.set("z", nbt::Value::makeInt(1));
    chest.set("Items", nbt::Value::makeList(nbt::Compound));
    entities.list.push_back(std::move(chest));
    root.set("block_entities", std::move(entities));

    store.readChunkNbt(root);
    auto firstStaleImport = store.prepareChunkNbt(root);
    auto secondStaleImport = store.prepareChunkNbt(root);
    const bool removed = store.removeAfterWorldBlockReplacement(removedKey);
    firstStaleImport();
    secondStaleImport();

    check(removed && store.empty() && store.size() == 0 &&
              !store.containsAt(1, 64, 1),
          "all prepared snapshots captured before removal remain blocked until the stale chunk is saved or forgotten");
}

void testWorldReplacementSurvivesTombstoneAllocationFailure() {
    using namespace cppfm;
    ToggleFailingMemoryResource tombstoneResource;
    BlockEntityStore store(-64, 320,
        BlockEntityStore::kMaxChunkBlockEntityRecords,
        BlockEntityStore::kMaxChunkBlockEntityNbtFootprint,
        BlockEntityStore::kMaxStoreBlockEntityRecords,
        BlockEntityStore::kMaxStoreRawNbtFootprint, &tombstoneResource);

    const auto typedKey = posKey(1, 64, 1);
    const auto opaqueKey = posKey(2, 64, 1);
    nbt::Value root = nbt::Value::makeCompound();
    nbt::Value entities = nbt::Value::makeList(nbt::Compound);
    nbt::Value chest = nbt::Value::makeCompound();
    chest.set("id", nbt::Value::makeString("minecraft:chest"));
    chest.set("x", nbt::Value::makeInt(1));
    chest.set("y", nbt::Value::makeInt(64));
    chest.set("z", nbt::Value::makeInt(1));
    chest.set("Items", nbt::Value::makeList(nbt::Compound));
    entities.list.push_back(std::move(chest));
    nbt::Value opaque = nbt::Value::makeCompound();
    opaque.set("id", nbt::Value::makeString("example:future_block_entity"));
    opaque.set("x", nbt::Value::makeInt(2));
    opaque.set("y", nbt::Value::makeInt(64));
    opaque.set("z", nbt::Value::makeInt(1));
    opaque.set("payload", nbt::Value::makeString("preserve until replaced"));
    entities.list.push_back(std::move(opaque));
    root.set("block_entities", std::move(entities));
    store.readChunkNbt(root);
    auto staleImport = store.prepareChunkNbt(root);

    store.setDirtyCallback([](std::int32_t, std::int32_t) {});
    (void)store.tryMarkDirty(posKey(3, 64, 1));
    tombstoneResource.failAllocations = true;
    const bool typedRemoved = store.removeAfterWorldBlockReplacement(typedKey);
    const bool opaqueRemoved = store.removeAfterWorldBlockReplacement(opaqueKey);
    tombstoneResource.failAllocations = false;

    staleImport();
    nbt::Value saved = nbt::Value::makeList(nbt::Compound);
    const bool serialized = store.writeChunkNbt(0, 0, saved);
    check(typedRemoved && opaqueRemoved && serialized && store.empty() &&
              store.size() == 0 && !store.containsAt(1, 64, 1) &&
              !store.containsAt(2, 64, 1) && saved.list.empty(),
          "world replacement uses in-place tombstones when tombstone allocation fails and blocks stale chunk imports");

    store.clearRemovalTombstonesForChunk(0, 0);
    const auto recreated = store.tryCreateShared(
        typedKey, BlockEntity::Kind::Chest);
    check(recreated && store.containsAt(1, 64, 1),
          "fallback tombstones are released after a successful chunk save so later placement can recreate the entity");
}

void testBatchDirtyCallbackFailureDoesNotPublishOwners() {
    using namespace cppfm;
    BlockEntityStore store;
    const std::vector<std::int64_t> keys{
        posKey(15, 64, 0), posKey(16, 64, 0)};
    int callbackCount = 0;
    bool callbackObservedUnpublishedState = true;
    store.setDirtyCallback([&](std::int32_t, std::int32_t) {
        ++callbackCount;
        callbackObservedUnpublishedState =
            callbackObservedUnpublishedState && store.empty();
        if (callbackCount == 2)
            throw std::runtime_error("injected dirty callback failure");
    });

    bool callbackFailurePropagated = false;
    try {
        (void)store.tryCreateSharedBatch(
            keys, BlockEntity::Kind::MovingPiston);
    } catch (const std::runtime_error&) {
        callbackFailurePropagated = true;
    }
    store.setDirtyCallback({});

    check(callbackFailurePropagated && callbackCount == 2 &&
              callbackObservedUnpublishedState && store.empty() &&
              !store.containsAt(15, 64, 0) && !store.containsAt(16, 64, 0),
          "a throwing batch dirty hook cannot publish moving block entities ahead of world updates");
}

void testBoundStoreRequiresOwningMutationGate() {
    using namespace cppfm;
    BlockEntityStore store;
    int worldIdentity = 0;
    int otherWorldIdentity = 0;
    int dirtyCalls = 0;
    check(store.bindMutationGate(&worldIdentity),
          "production block-entity store accepts one World gate binding");
    check(!store.bindMutationGate(&otherWorldIdentity),
          "block-entity store cannot be rebound to another World");
    store.setDirtyCallback([&](std::int32_t, std::int32_t) { ++dirtyCalls; });

    const auto key = posKey(1, 64, 1);
    check(!store.tryCreateShared(key, BlockEntity::Kind::Chest) &&
              dirtyCalls == 0 && store.empty(),
          "bound block-entity store rejects first publication without a mutation lease");

    std::recursive_mutex otherGate;
    {
        MutationGateLease wrongLease{
            &otherWorldIdentity,
            std::unique_lock<std::recursive_mutex>(otherGate)};
        check(!store.tryCreateShared(key, BlockEntity::Kind::Chest) &&
                  dirtyCalls == 0 && store.empty(),
              "a different World's mutation lease cannot admit the block entity");
    }

    std::recursive_mutex worldGate;
    {
        MutationGateLease matchingLease{
            &worldIdentity,
            std::unique_lock<std::recursive_mutex>(worldGate)};
        const auto owner = store.tryCreateShared(
            key, BlockEntity::Kind::Chest);
        check(owner && dirtyCalls == 1 && store.containsAt(1, 64, 1),
              "matching World lease permits dirty publication and owner commit");
        check(store.tryMarkDirty(key) && dirtyCalls == 2,
              "dirty admission remains available while the matching lease is held");
    }
    check(!store.tryMarkDirty(key) && dirtyCalls == 2,
          "bound store rejects later dirty admission after its World lease is released");

    BlockEntityStore batchStore;
    int batchWorld = 0;
    int batchDirtyCalls = 0;
    batchStore.bindMutationGate(&batchWorld);
    batchStore.setDirtyCallback(
        [&](std::int32_t, std::int32_t) { ++batchDirtyCalls; });
    const std::vector<std::int64_t> keys{posKey(2, 64, 2)};
    check(!batchStore.tryCreateSharedBatch(
              keys, BlockEntity::Kind::MovingPiston) &&
              batchDirtyCalls == 0,
          "batch publication also requires the owning mutation lease");
    std::recursive_mutex batchGate;
    {
        MutationGateLease matchingBatchLease{
            &batchWorld,
            std::unique_lock<std::recursive_mutex>(batchGate)};
        const auto batch = batchStore.tryCreateSharedBatch(
            keys, BlockEntity::Kind::MovingPiston);
        check(batch && batch->size() == 1 && batchDirtyCalls == 1,
              "batch publication succeeds under its bound World lease");
    }
}

void testTryCreateSharedDirtyCallbackFailureIsAtomic() {
    using namespace cppfm;
    const auto key = posKey(5, 64, 7);

    BlockEntityStore fresh;
    int createCallbackCalls = 0;
    bool createCallbackSawNoOwner = false;
    fresh.setDirtyCallback([&](std::int32_t, std::int32_t) {
        ++createCallbackCalls;
        createCallbackSawNoOwner = !fresh.getShared(key) &&
            !fresh.containsAt(5, 64, 7) && fresh.empty() && fresh.size() == 0;
        throw std::runtime_error("injected dirty callback failure");
    });
    bool createFailurePropagated = false;
    try {
        (void)fresh.tryCreateShared(key, BlockEntity::Kind::Chest);
    } catch (const std::runtime_error&) {
        createFailurePropagated = true;
    }
    fresh.setDirtyCallback({});
    const auto retriedOwner = fresh.tryCreateShared(
        key, BlockEntity::Kind::Chest);
    check(createFailurePropagated && createCallbackCalls == 1 &&
              createCallbackSawNoOwner && fresh.size() == 1 && retriedOwner,
          "a throwing first-create dirty hook leaves no published owner and a later retry can create it");

    // A recognized entity retains its original NBT as a forward-compatible
    // template. A kind replacement is destructive, so a failed dirty hook
    // must preserve both the typed state and this opaque serialized data.
    BlockEntityStore replacement;
    nbt::Value root = nbt::Value::makeCompound();
    nbt::Value entities = nbt::Value::makeList(nbt::Compound);
    nbt::Value savedChest = nbt::Value::makeCompound();
    savedChest.set("id", nbt::Value::makeString("minecraft:chest"));
    savedChest.set("x", nbt::Value::makeInt(5));
    savedChest.set("y", nbt::Value::makeInt(64));
    savedChest.set("z", nbt::Value::makeInt(7));
    savedChest.set("future_extension",
                   nbt::Value::makeString("must-survive-failed-replacement"));
    savedChest.set("Items", nbt::Value::makeList(nbt::Compound));
    entities.list.push_back(std::move(savedChest));
    root.set("block_entities", std::move(entities));
    replacement.readChunkNbt(root);
    const auto originalOwner = replacement.getShared(key);
    if (originalOwner) {
        std::lock_guard entityLock(*originalOwner->stateMtx);
        originalOwner->chest.slots[0] = ItemStack::ofName(
            "minecraft:diamond", 5);
    }

    const auto serializeChunk = [&replacement] {
        nbt::Value list = nbt::Value::makeList(nbt::Compound);
        const bool serialized = replacement.writeChunkNbt(0, 0, list);
        if (!serialized)
            throw std::runtime_error("unexpected block-entity serialization failure");
        nbt::Value chunk = nbt::Value::makeCompound();
        chunk.set("block_entities", std::move(list));
        WriteBuffer encoded;
        nbt::writeFileRoot(encoded, chunk);
        return encoded.data;
    };
    const auto before = serializeChunk();

    int replacementCallbackCalls = 0;
    bool replacementCallbackSawOriginal = false;
    replacement.setDirtyCallback([&](std::int32_t, std::int32_t) {
        ++replacementCallbackCalls;
        const auto current = replacement.getShared(key);
        if (current == originalOwner && current) {
            std::lock_guard entityLock(*current->stateMtx);
            replacementCallbackSawOriginal =
                current->kind == BlockEntity::Kind::Chest &&
                current->chest.slots[0].count == 5;
        }
        throw std::runtime_error("injected dirty callback failure");
    });
    bool replacementFailurePropagated = false;
    try {
        (void)replacement.tryCreateShared(key, BlockEntity::Kind::Hopper);
    } catch (const std::runtime_error&) {
        replacementFailurePropagated = true;
    }
    replacement.setDirtyCallback({});

    const auto after = serializeChunk();
    bool originalTypedStateSurvived = false;
    if (originalOwner) {
        std::lock_guard entityLock(*originalOwner->stateMtx);
        originalTypedStateSurvived =
            originalOwner->kind == BlockEntity::Kind::Chest &&
            originalOwner->chest.slots[0].count == 5;
    }
    check(replacementFailurePropagated && replacementCallbackCalls == 1 &&
              replacementCallbackSawOriginal &&
              replacement.getShared(key) == originalOwner &&
              originalTypedStateSurvived && before == after,
          "a throwing kind-replacement dirty hook preserves owner identity, inventory, and opaque NBT");
}

void testConcurrentTryCreateSharedPublication() {
    using namespace cppfm;
    BlockEntityStore store;
    const auto key = posKey(12, 64, 3);
    std::atomic<int> callbackEntries{0};
    std::atomic<bool> releaseCallbacks{false};
    store.setDirtyCallback([&](std::int32_t, std::int32_t) {
        callbackEntries.fetch_add(1, std::memory_order_release);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(2);
        while (!releaseCallbacks.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
    });

    BlockEntityStore::Owner first;
    BlockEntityStore::Owner second;
    std::thread firstCreator([&] {
        first = store.tryCreateShared(key, BlockEntity::Kind::Chest);
    });
    std::thread secondCreator([&] {
        second = store.tryCreateShared(key, BlockEntity::Kind::Chest);
    });
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (callbackEntries.load(std::memory_order_acquire) != 2 &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    const bool bothCallbacksRanBeforePublication =
        callbackEntries.load(std::memory_order_acquire) == 2;
    releaseCallbacks.store(true, std::memory_order_release);
    firstCreator.join();
    secondCreator.join();
    store.setDirtyCallback({});

    check(bothCallbacksRanBeforePublication && first && second &&
              first == second && store.size() == 1 &&
              first->kind == BlockEntity::Kind::Chest,
          "concurrent get-or-create callers converge on one fully initialized owner after detached dirty admission");
}

void testReentrantTryCreateSharedDirtyCallbackDoesNotRecurse() {
    using namespace cppfm;
    BlockEntityStore store;
    const auto key = posKey(6, 70, 9);
    int callbackCalls = 0;
    bool sameKeyReentryRejected = false;
    store.setDirtyCallback([&](std::int32_t, std::int32_t) {
        ++callbackCalls;
        sameKeyReentryRejected = !store.tryCreateShared(
            key, BlockEntity::Kind::Chest);
    });

    const auto owner = store.tryCreateShared(key, BlockEntity::Kind::Chest);
    store.setDirtyCallback({});
    check(owner && callbackCalls == 1 && sameKeyReentryRejected &&
              store.getShared(key) == owner && store.size() == 1,
          "same-key dirty-hook re-entry is rejected while the outer staged owner commits once");
}

void testReentrantTryCreateSharedBatchDirtyCallbackDoesNotRecurse() {
    using namespace cppfm;
    BlockEntityStore store;
    const std::vector<std::int64_t> keys{
        posKey(5, 70, 5), posKey(6, 71, 5)};
    int callbackCalls = 0;
    bool nestedBatchRejected = false;
    store.setDirtyCallback([&](std::int32_t, std::int32_t) {
        ++callbackCalls;
        nestedBatchRejected = !store.tryCreateSharedBatch(
            keys, BlockEntity::Kind::Chest).has_value();
    });

    const auto owners = store.tryCreateSharedBatch(
        keys, BlockEntity::Kind::Chest);
    store.setDirtyCallback({});
    check(owners && owners->size() == keys.size() && callbackCalls == 1 &&
              nestedBatchRejected && store.size() == keys.size(),
          "same-chunk dirty-hook re-entry cannot recurse or partially publish a batch");
}

void testExternalAnvilCandidateAdmissionPublishesLiveState() {
    using namespace cppfm;
    const auto key = posKey(1, 64, 1);
    BlockEntityStore store;
    nbt::Value root = nbt::Value::makeCompound();
    nbt::Value entities = nbt::Value::makeList(nbt::Compound);
    nbt::Value chest = nbt::Value::makeCompound();
    chest.set("id", nbt::Value::makeString("minecraft:chest"));
    chest.set("x", nbt::Value::makeInt(1));
    chest.set("y", nbt::Value::makeInt(64));
    chest.set("z", nbt::Value::makeInt(1));
    chest.set("future_extension",
              nbt::Value::makeString("opaque-template-must-survive"));
    auto& opaqueBytes = chest.set("future_bytes", {});
    opaqueBytes.tag = nbt::ByteArray;
    opaqueBytes.byteArray.resize(1'150'000);
    std::mt19937 random(0x769255u);
    for (auto& byte : opaqueBytes.byteArray)
        byte = static_cast<std::uint8_t>(random() >> 24);
    chest.set("Items", nbt::Value::makeList(nbt::Compound));
    entities.list.push_back(std::move(chest));
    root.set("block_entities", std::move(entities));
    store.readChunkNbt(root);

    const auto owner = store.getShared(key);
    const auto serializeLive = [&store] {
        nbt::Value serialized = nbt::Value::makeList(nbt::Compound);
        (void)store.writeChunkNbt(0, 0, serialized);
        nbt::Value chunk = nbt::Value::makeCompound();
        chunk.set("block_entities", std::move(serialized));
        WriteBuffer bytes;
        nbt::writeFileRoot(bytes, chunk);
        return bytes.data;
    };
    const auto before = serializeLive();
    std::atomic<int> dirtyCalls{0};
    store.setDirtyCallback([&dirtyCalls](std::int32_t, std::int32_t) {
        dirtyCalls.fetch_add(1, std::memory_order_relaxed);
    });

    bool admissionCalled = false;
    bool candidateAndOpaqueTemplateProjected = false;
    const bool applied = store.applyCandidateIfAdmitted(
        key, owner,
        [](BlockEntity& candidate) {
            candidate.chest.slots[0] = ItemStack::ofName("minecraft:diamond");
            return true;
        },
        [&](const BlockEntityStore::SnapshotOverride& replacement) {
            admissionCalled = true;
            nbt::Value projected = nbt::Value::makeList(nbt::Compound);
            const bool serialized = store.writeChunkNbt(
                0, 0, projected,
                std::vector<BlockEntityStore::SnapshotOverride>{replacement},
                true);
            if (!serialized || projected.list.size() != 1) return false;
            const auto* extension = projected.list.front().get("future_extension");
            const auto* opaquePayload = projected.list.front().get("future_bytes");
            const auto* items = projected.list.front().get("Items");
            const auto* itemId = items && !items->list.empty()
                ? items->list.front().get("id") : nullptr;
            const bool candidatePreservesOpaqueData =
                extension && extension->tag == nbt::String &&
                extension->str == "opaque-template-must-survive" &&
                opaquePayload && opaquePayload->tag == nbt::ByteArray &&
                opaquePayload->byteArray.size() == 1'150'000 &&
                itemId && itemId->tag == nbt::String &&
                itemId->str == "minecraft:diamond";
            nbt::Value root = nbt::Value::makeCompound();
            root.set("block_entities", std::move(projected));
            WriteBuffer encoded;
            nbt::writeFileRoot(encoded, root);
            const bool fitsAnvilStorage = RegionFile::canStoreBytes(encoded.data);
            candidateAndOpaqueTemplateProjected =
                candidatePreservesOpaqueData && fitsAnvilStorage &&
                RegionFile::wouldStoreExternally(encoded.data);
            return fitsAnvilStorage;
        });

    const auto after = serializeLive();
    bool liveSlotContainsCandidate = false;
    {
        std::lock_guard entityLock(*owner->stateMtx);
        liveSlotContainsCandidate =
            owner->chest.slots[0].itemId ==
            cppfm::gen::itemIdByName().at("minecraft:diamond");
    }
    check(applied && admissionCalled,
          "candidate transaction publishes after external Anvil storage is admitted");
    check(candidateAndOpaqueTemplateProjected,
          "candidate preflight preserves opaque NBT and selects the real external .mcc representation");
    check(before != after && liveSlotContainsCandidate && dirtyCalls.load() == 1,
          "admitted candidate publishes item state, serialized NBT, and one dirty notification");

    BlockEntityStore dirtyFailure;
    const auto dirtyFailureKey = posKey(8, 64, 8);
    const auto dirtyFailureOwner = dirtyFailure.createShared(
        dirtyFailureKey, BlockEntity::Kind::Chest);
    dirtyFailure.setDirtyCallback([](std::int32_t, std::int32_t) {
        throw std::runtime_error("injected dirty callback failure");
    });
    const bool dirtyFailureApplied = dirtyFailure.applyCandidateIfAdmitted(
        dirtyFailureKey, dirtyFailureOwner,
        [](BlockEntity& candidate) {
            candidate.chest.slots[0] = ItemStack::ofName("minecraft:diamond");
            return true;
        }, [](const BlockEntityStore::SnapshotOverride&) { return true; });
    bool dirtyFailureOwnerUnchanged = false;
    {
        std::lock_guard entityLock(*dirtyFailureOwner->stateMtx);
        dirtyFailureOwnerUnchanged =
            dirtyFailureOwner->chest.slots[0].empty();
    }
    check(!dirtyFailureApplied && dirtyFailureOwnerUnchanged,
          "candidate mutation remains unpublished when its dirty callback fails");
    bool dirtyAdmissionThrew = false;
    try {
        (void)dirtyFailure.tryMarkDirty(dirtyFailureKey);
    } catch (const std::runtime_error&) {
        dirtyAdmissionThrew = true;
    }
    check(dirtyAdmissionThrew,
          "dirty pre-admission exposes callback failure before a live menu mutation");

    BlockEntityStore absent;
    std::atomic<int> absentDirtyCalls{0};
    absent.setDirtyCallback([&absentDirtyCalls](std::int32_t, std::int32_t) {
        absentDirtyCalls.fetch_add(1, std::memory_order_relaxed);
    });
    BlockEntity sign;
    sign.kind = BlockEntity::Kind::Sign;
    sign.sign.front[0] = "not admitted";
    bool absentAdmissionSawCandidate = false;
    const auto rejectedInstall = absent.tryCreateSharedCandidateIfAdmitted(
        posKey(4, 64, 4), sign,
        [&absentAdmissionSawCandidate](
            const BlockEntityStore::SnapshotOverride& replacement) {
            absentAdmissionSawCandidate =
                replacement.entity.kind == BlockEntity::Kind::Sign &&
                replacement.entity.sign.front[0] == "not admitted";
            return false;
        });
    check(!rejectedInstall && absent.empty() &&
              absentDirtyCalls.load() == 0 && absentAdmissionSawCandidate,
          "rejected first sign candidate is neither inserted nor dirtied");

    BlockEntityStore callbackFailure;
    callbackFailure.setDirtyCallback(
        [](std::int32_t, std::int32_t) {
            throw std::runtime_error("injected dirty callback failure");
        });
    bool candidateCallbackThrew = false;
    try {
        (void)callbackFailure.tryCreateSharedCandidateIfAdmitted(
            posKey(8, 64, 8), sign,
            [](const BlockEntityStore::SnapshotOverride&) { return true; });
    } catch (const std::runtime_error&) {
        candidateCallbackThrew = true;
    }
    check(candidateCallbackThrew && callbackFailure.empty(),
          "failed persistence admission cannot leave a newly published sign unsaved");

    BlockEntityStore replacementCallbackFailure;
    replacementCallbackFailure.setDirtyCallback(
        [](std::int32_t, std::int32_t) {
            throw std::runtime_error("injected dirty callback failure");
        });
    auto opaqueReplacement = nbt::Value::makeCompound();
    opaqueReplacement.set("id", nbt::Value::makeString("example:machine"));
    bool replacementAdmissionCalled = false;
    bool replacementCallbackThrew = false;
    try {
        (void)replacementCallbackFailure.replaceNbtAtIfAdmitted(
            9, 64, 9, std::move(opaqueReplacement),
            [&replacementAdmissionCalled](
                const BlockEntityStore::NbtReplacement& candidate) {
                replacementAdmissionCalled = candidate.entity.has_value();
                return true;
            });
    } catch (const std::runtime_error&) {
        replacementCallbackThrew = true;
    }
    check(replacementCallbackThrew && replacementAdmissionCalled &&
              replacementCallbackFailure.empty(),
          "failed dirty callback rejects a detached NBT replacement before publication");
}

struct CopyControlledDirtyCallback {
    std::shared_ptr<std::atomic<bool>> failCopy;
    std::shared_ptr<std::atomic<int>> calls;
    explicit CopyControlledDirtyCallback(
        std::shared_ptr<std::atomic<bool>> flag,
        std::shared_ptr<std::atomic<int>> invocationCount)
        : failCopy(std::move(flag)), calls(std::move(invocationCount)) {}
    CopyControlledDirtyCallback(const CopyControlledDirtyCallback& other)
        : failCopy(other.failCopy), calls(other.calls) {
        if (failCopy->load(std::memory_order_relaxed))
            throw std::runtime_error("injected callback copy failure");
    }
    void operator()(std::int32_t, std::int32_t) const {
        calls->fetch_add(1, std::memory_order_relaxed);
    }
};

void testOpaqueBlockEntityRoundTrip() {
    cppfm::BlockEntityStore lifetimeStore;
    const auto lifetimeKey = cppfm::posKey(32, 64, -1);
    check(!lifetimeStore.containsAt(32, 64, -1),
          "block-entity presence is false before insertion");
    lifetimeStore.createShared(lifetimeKey, cppfm::BlockEntity::Kind::Chest);
    check(lifetimeStore.containsAt(32, 64, -1),
          "typed block entities are visible to destructive-operation guards");
    const auto retainedOwner = lifetimeStore.getAt(32, 64, -1);
    lifetimeStore.remove(lifetimeKey);
    check(retainedOwner &&
              retainedOwner->kind == cppfm::BlockEntity::Kind::Chest &&
              !lifetimeStore.getAt(32, 64, -1) &&
              !lifetimeStore.containsAt(32, 64, -1),
          "shared block-entity snapshots remain alive after store removal");

    cppfm::nbt::Value opaqueRoot = cppfm::nbt::Value::makeCompound();
    cppfm::nbt::Value opaqueEntities =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value unknownEntity = cppfm::nbt::Value::makeCompound();
    unknownEntity.set("id", cppfm::nbt::Value::makeString(
        "example:unregistered_block_entity"));
    unknownEntity.set("x", cppfm::nbt::Value::makeInt(33));
    unknownEntity.set("y", cppfm::nbt::Value::makeInt(64));
    unknownEntity.set("z", cppfm::nbt::Value::makeInt(-1));
    opaqueEntities.list.push_back(std::move(unknownEntity));
    opaqueRoot.set("block_entities", std::move(opaqueEntities));
    cppfm::BlockEntityStore opaqueStore;
    opaqueStore.readChunkNbt(opaqueRoot);
    check(opaqueStore.containsAt(33, 64, -1),
          "opaque block-entity NBT is visible to destructive-operation guards");
    check(!opaqueStore.canInsertNewAt(cppfm::posKey(33, 64, -1)) &&
              lifetimeStore.canInsertNewAt(cppfm::posKey(33, 64, -1)),
          "new block-entity placement preflight rejects opaque records but admits an unoccupied position");
    bool opaquePromotionRejected = false;
    try {
        (void)opaqueStore.createShared(cppfm::posKey(33, 64, -1),
                                       cppfm::BlockEntity::Kind::Chest);
    } catch (const std::runtime_error&) {
        opaquePromotionRejected = true;
    }
    cppfm::nbt::Value opaqueAfterPromotion =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    opaqueStore.writeChunkNbt(2, -1, opaqueAfterPromotion);
    const auto* preservedOpaqueId = opaqueAfterPromotion.list.empty()
        ? nullptr : opaqueAfterPromotion.list.front().get("id");
    check(opaquePromotionRejected && opaqueStore.containsAt(33, 64, -1) &&
              preservedOpaqueId && preservedOpaqueId->tag == cppfm::nbt::String &&
              preservedOpaqueId->str == "example:unregistered_block_entity",
          "opening an unknown block entity cannot replace or discard its saved record");

    cppfm::nbt::Value misplacedRoot = cppfm::nbt::Value::makeCompound();
    misplacedRoot.set("xPos", cppfm::nbt::Value::makeInt(0));
    misplacedRoot.set("zPos", cppfm::nbt::Value::makeInt(0));
    cppfm::nbt::Value misplacedEntities =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value misplacedEntity = cppfm::nbt::Value::makeCompound();
    misplacedEntity.set("id", cppfm::nbt::Value::makeString(
        "example:unregistered_block_entity"));
    misplacedEntity.set("x", cppfm::nbt::Value::makeInt(33));
    misplacedEntity.set("y", cppfm::nbt::Value::makeInt(64));
    misplacedEntity.set("z", cppfm::nbt::Value::makeInt(0));
    misplacedEntities.list.push_back(std::move(misplacedEntity));
    misplacedRoot.set("block_entities", std::move(misplacedEntities));
    cppfm::BlockEntityStore misplacedStore;
    bool misplacedRejected = false;
    try {
        misplacedStore.readChunkNbt(misplacedRoot);
    } catch (const std::runtime_error&) {
        misplacedRejected = true;
    }
    check(misplacedRejected && misplacedStore.empty(),
          "a block entity outside its source chunk is rejected before publication");

    cppfm::nbt::Value invalidItemRoot = cppfm::nbt::Value::makeCompound();
    invalidItemRoot.set("xPos", cppfm::nbt::Value::makeInt(2));
    invalidItemRoot.set("zPos", cppfm::nbt::Value::makeInt(-1));
    cppfm::nbt::Value invalidItemEntities =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value invalidItemChest = cppfm::nbt::Value::makeCompound();
    invalidItemChest.set("id", cppfm::nbt::Value::makeString(
        "minecraft:chest"));
    invalidItemChest.set("x", cppfm::nbt::Value::makeInt(33));
    invalidItemChest.set("y", cppfm::nbt::Value::makeInt(64));
    invalidItemChest.set("z", cppfm::nbt::Value::makeInt(-1));
    cppfm::nbt::Value invalidItems =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value zeroCountStone = cppfm::nbt::Value::makeCompound();
    zeroCountStone.set("id", cppfm::nbt::Value::makeString(
        "minecraft:stone"));
    zeroCountStone.set("Count", cppfm::nbt::Value::makeByte(0));
    zeroCountStone.set("Slot", cppfm::nbt::Value::makeByte(3));
    invalidItems.list.push_back(std::move(zeroCountStone));
    invalidItemChest.set("Items", std::move(invalidItems));
    invalidItemEntities.list.push_back(std::move(invalidItemChest));
    invalidItemRoot.set("block_entities", std::move(invalidItemEntities));
    cppfm::BlockEntityStore invalidItemStore;
    bool invalidKnownItemRejected = false;
    try {
        invalidItemStore.readChunkNbt(invalidItemRoot);
    } catch (const std::runtime_error&) {
        invalidKnownItemRejected = true;
    }
    cppfm::nbt::Value invalidItemSaved =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    invalidItemStore.writeChunkNbt(2, -1, invalidItemSaved);
    check(invalidKnownItemRejected && invalidItemStore.empty() &&
              invalidItemSaved.list.empty(),
          "invalid known inventory item rejects detached block-entity load before publication");

    cppfm::nbt::Value root = cppfm::nbt::Value::makeCompound();
    root.set("xPos", cppfm::nbt::Value::makeInt(2));
    root.set("zPos", cppfm::nbt::Value::makeInt(-1));
    cppfm::nbt::Value entities =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value beacon = cppfm::nbt::Value::makeCompound();
    beacon.set("id", cppfm::nbt::Value::makeString("minecraft:beacon"));
    beacon.set("x", cppfm::nbt::Value::makeInt(32));
    beacon.set("y", cppfm::nbt::Value::makeInt(64));
    beacon.set("z", cppfm::nbt::Value::makeInt(-1));
    cppfm::nbt::Value modData = cppfm::nbt::Value::makeCompound();
    modData.set("retained", cppfm::nbt::Value::makeString("opaque payload"));
    beacon.set("ModData", std::move(modData));
    entities.list.push_back(std::move(beacon));
    root.set("block_entities", std::move(entities));

    cppfm::BlockEntityStore store;
    store.readChunkNbt(root);
    check(store.size() == 1,
          "opaque block entities count once in the public store size");
    cppfm::nbt::Value saved =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    store.writeChunkNbt(2, -1, saved);
    const auto* savedId = saved.list.empty()
        ? nullptr : saved.list.front().get("id");
    const auto* savedModData = saved.list.empty()
        ? nullptr : saved.list.front().get("ModData");
    const auto* savedPayload = savedModData
        ? savedModData->get("retained") : nullptr;
    check(saved.list.size() == 1 && savedId &&
              savedId->tag == cppfm::nbt::String &&
              savedId->str == "minecraft:beacon" && savedPayload &&
              savedPayload->tag == cppfm::nbt::String &&
              savedPayload->str == "opaque payload",
          "unknown vanilla/modded block-entity NBT survives load and save");

    cppfm::nbt::Value chestRoot = cppfm::nbt::Value::makeCompound();
    chestRoot.set("xPos", cppfm::nbt::Value::makeInt(2));
    chestRoot.set("zPos", cppfm::nbt::Value::makeInt(-1));
    cppfm::nbt::Value chestEntities =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value chest = cppfm::nbt::Value::makeCompound();
    chest.set("id", cppfm::nbt::Value::makeString("minecraft:chest"));
    chest.set("x", cppfm::nbt::Value::makeInt(33));
    chest.set("y", cppfm::nbt::Value::makeInt(64));
    chest.set("z", cppfm::nbt::Value::makeInt(-1));
    chest.set("CustomName",
              cppfm::nbt::Value::makeString("preserve chest metadata"));
    cppfm::nbt::Value items =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value diamond = cppfm::nbt::Value::makeCompound();
    diamond.set("id", cppfm::nbt::Value::makeString("minecraft:diamond"));
    diamond.set("Count", cppfm::nbt::Value::makeByte(1));
    diamond.set("Slot", cppfm::nbt::Value::makeByte(0));
    cppfm::nbt::Value diamondComponents =
        cppfm::nbt::Value::makeCompound();
    diamondComponents.set("minecraft:damage", cppfm::nbt::Value::makeInt(2));
    diamond.set("components", std::move(diamondComponents));
    items.list.push_back(std::move(diamond));
    cppfm::nbt::Value pickaxe = cppfm::nbt::Value::makeCompound();
    pickaxe.set("id",
                cppfm::nbt::Value::makeString("minecraft:diamond_pickaxe"));
    pickaxe.set("Count", cppfm::nbt::Value::makeByte(1));
    pickaxe.set("Slot", cppfm::nbt::Value::makeByte(2));
    cppfm::nbt::Value pickaxeComponents = cppfm::nbt::Value::makeCompound();
    pickaxeComponents.set("minecraft:damage",
                          cppfm::nbt::Value::makeInt(3));
    pickaxe.set("components", std::move(pickaxeComponents));
    items.list.push_back(std::move(pickaxe));
    chest.set("Items", std::move(items));
    chestEntities.list.push_back(std::move(chest));
    chestRoot.set("block_entities", std::move(chestEntities));
    store.readChunkNbt(chestRoot);
    check(store.size() == 2,
          "typed entities with raw NBT templates are not double-counted");

    cppfm::nbt::Value savedWithChest =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    store.writeChunkNbt(2, -1, savedWithChest);
    const cppfm::nbt::Value* savedChest = nullptr;
    const cppfm::nbt::Value* savedBeacon = nullptr;
    for (const auto& entity : savedWithChest.list) {
        const auto* id = entity.get("id");
        if (!id || id->tag != cppfm::nbt::String) continue;
        if (id->str == "minecraft:chest") savedChest = &entity;
        if (id->str == "minecraft:beacon") savedBeacon = &entity;
    }
    const auto* chestName = savedChest ? savedChest->get("CustomName") : nullptr;
    const auto* savedItems = savedChest ? savedChest->get("Items") : nullptr;
    bool diamondDataPreserved = false;
    if (savedItems && savedItems->tag == cppfm::nbt::List) {
        for (const auto& item : savedItems->list) {
            const auto* id = item.get("id");
            if (!id || id->tag != cppfm::nbt::String) continue;
            if (id->str == "minecraft:diamond") {
                const auto* components = item.get("components");
                const auto* payload = components
                    ? components->get("minecraft:damage") : nullptr;
                diamondDataPreserved = payload &&
                    payload->tag == cppfm::nbt::Int && payload->i == 2;
            }
        }
    }
    check(savedChest && savedBeacon && chestName &&
              chestName->tag == cppfm::nbt::String &&
              chestName->str == "preserve chest metadata" &&
              diamondDataPreserved,
          "typed block entities retain extra NBT and supported item components");

    cppfm::nbt::Value moddedInventoryRoot = cppfm::nbt::Value::makeCompound();
    cppfm::nbt::Value moddedInventoryEntities =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value moddedChest = cppfm::nbt::Value::makeCompound();
    moddedChest.set("id", cppfm::nbt::Value::makeString("minecraft:chest"));
    moddedChest.set("x", cppfm::nbt::Value::makeInt(34));
    moddedChest.set("y", cppfm::nbt::Value::makeInt(64));
    moddedChest.set("z", cppfm::nbt::Value::makeInt(-1));
    cppfm::nbt::Value moddedItems =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value moddedGem = cppfm::nbt::Value::makeCompound();
    moddedGem.set("id", cppfm::nbt::Value::makeString("example:gem"));
    moddedGem.set("Count", cppfm::nbt::Value::makeByte(2));
    moddedGem.set("Slot", cppfm::nbt::Value::makeByte(0));
    moddedGem.set("custom_payload",
                  cppfm::nbt::Value::makeString("keep modded item"));
    moddedItems.list.push_back(std::move(moddedGem));
    moddedChest.set("Items", std::move(moddedItems));
    moddedInventoryEntities.list.push_back(std::move(moddedChest));
    moddedInventoryRoot.set("block_entities",
                            std::move(moddedInventoryEntities));
    cppfm::BlockEntityStore moddedInventoryStore;
    moddedInventoryStore.readChunkNbt(moddedInventoryRoot);
    const bool opaqueTryOpenRejected = !moddedInventoryStore.tryCreateShared(
        cppfm::posKey(34, 64, -1), cppfm::BlockEntity::Kind::Chest);
    bool opaqueInventoryOpenRejected = false;
    try {
        (void)moddedInventoryStore.createShared(
            cppfm::posKey(34, 64, -1), cppfm::BlockEntity::Kind::Chest);
    } catch (const std::runtime_error&) {
        opaqueInventoryOpenRejected = true;
    }
    cppfm::nbt::Value savedModdedInventory =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    moddedInventoryStore.writeChunkNbt(2, -1, savedModdedInventory);
    const auto* savedModdedItems = savedModdedInventory.list.empty()
        ? nullptr : savedModdedInventory.list.front().get("Items");
    const auto* savedModdedGem = savedModdedItems &&
            savedModdedItems->tag == cppfm::nbt::List &&
            !savedModdedItems->list.empty()
        ? &savedModdedItems->list.front() : nullptr;
    const auto* savedModdedPayload = savedModdedGem
        ? savedModdedGem->get("custom_payload") : nullptr;
    check(moddedInventoryStore.size() == 1 &&
              moddedInventoryStore.containsAt(34, 64, -1) &&
              !moddedInventoryStore.getShared(cppfm::posKey(34, 64, -1)) &&
              opaqueTryOpenRejected && opaqueInventoryOpenRejected &&
              savedModdedGem &&
              savedModdedPayload &&
              savedModdedPayload->tag == cppfm::nbt::String &&
              savedModdedPayload->str == "keep modded item",
          "unknown inventory items keep the full container opaque and uneditable");

    cppfm::nbt::Value opaqueComponentRoot = cppfm::nbt::Value::makeCompound();
    cppfm::nbt::Value opaqueComponentEntities =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value opaqueComponentChest = cppfm::nbt::Value::makeCompound();
    opaqueComponentChest.set("id",
        cppfm::nbt::Value::makeString("minecraft:chest"));
    opaqueComponentChest.set("x", cppfm::nbt::Value::makeInt(35));
    opaqueComponentChest.set("y", cppfm::nbt::Value::makeInt(64));
    opaqueComponentChest.set("z", cppfm::nbt::Value::makeInt(-1));
    cppfm::nbt::Value opaqueComponentItems =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value opaqueComponentDiamond = cppfm::nbt::Value::makeCompound();
    opaqueComponentDiamond.set("id",
        cppfm::nbt::Value::makeString("minecraft:diamond"));
    opaqueComponentDiamond.set("Count", cppfm::nbt::Value::makeByte(1));
    opaqueComponentDiamond.set("Slot", cppfm::nbt::Value::makeByte(0));
    cppfm::nbt::Value opaqueComponents = cppfm::nbt::Value::makeCompound();
    opaqueComponents.set("example:custom_data",
        cppfm::nbt::Value::makeString("keep unknown component"));
    opaqueComponentDiamond.set("components", std::move(opaqueComponents));
    opaqueComponentItems.list.push_back(std::move(opaqueComponentDiamond));
    opaqueComponentChest.set("Items", std::move(opaqueComponentItems));
    opaqueComponentEntities.list.push_back(std::move(opaqueComponentChest));
    opaqueComponentRoot.set("block_entities", std::move(opaqueComponentEntities));
    cppfm::BlockEntityStore opaqueComponentStore;
    opaqueComponentStore.readChunkNbt(opaqueComponentRoot);
    bool opaqueComponentOpenRejected = false;
    try {
        (void)opaqueComponentStore.createShared(
            cppfm::posKey(35, 64, -1), cppfm::BlockEntity::Kind::Chest);
    } catch (const std::runtime_error&) {
        opaqueComponentOpenRejected = true;
    }
    cppfm::nbt::Value savedOpaqueComponent =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    opaqueComponentStore.writeChunkNbt(2, -1, savedOpaqueComponent);
    const auto* opaqueSavedItems = savedOpaqueComponent.list.empty()
        ? nullptr : savedOpaqueComponent.list.front().get("Items");
    const auto* opaqueSavedStack = opaqueSavedItems &&
            opaqueSavedItems->tag == cppfm::nbt::List &&
            !opaqueSavedItems->list.empty()
        ? &opaqueSavedItems->list.front() : nullptr;
    const auto* opaqueSavedComponents = opaqueSavedStack
        ? opaqueSavedStack->get("components") : nullptr;
    const auto* opaqueSavedData = opaqueSavedComponents
        ? opaqueSavedComponents->get("example:custom_data") : nullptr;
    check(opaqueComponentStore.containsAt(35, 64, -1) &&
              !opaqueComponentStore.getShared(cppfm::posKey(35, 64, -1)) &&
              opaqueComponentOpenRejected && opaqueSavedData &&
              opaqueSavedData->tag == cppfm::nbt::String &&
              opaqueSavedData->str == "keep unknown component",
          "unknown item components keep a vanilla container opaque and lossless");

    const auto chestOwner = store.getShared(cppfm::posKey(33, 64, -1));
    check(static_cast<bool>(chestOwner),
          "typed chest inventory is available for item-movement checks");
    if (chestOwner) {
        std::lock_guard entityLock(*chestOwner->stateMtx);
        const auto retainedDiamond = chestOwner->chest.slots[0];
        const auto copiedDiamond = retainedDiamond;
        const auto plainDiamond = cppfm::ItemStack::of(
            cppfm::gen::itemIdByName().at("minecraft:diamond"), 1);
        check(retainedDiamond.sameItemData(copiedDiamond) &&
                  !retainedDiamond.sameItemData(plainDiamond),
              "known component identity follows stack copies but not a fresh same-ID item");
        auto& retainedPickaxe = chestOwner->chest.slots[2];
        const bool damageWasDecoded = retainedPickaxe.getDamage() == 3 &&
                                      retainedPickaxe.persistedNbt;
        retainedPickaxe.setDamage(9);
        check(damageWasDecoded && retainedPickaxe.getDamage() == 9 &&
                  retainedPickaxe.persistedNbt,
              "editing a stored tool patches runtime damage without discarding its NBT template");
    }

    cppfm::BlockEntityStore replacementStore;
    replacementStore.readChunkNbt(chestRoot);
    if (const auto replacementOwner = replacementStore.getShared(
            cppfm::posKey(33, 64, -1))) {
        std::lock_guard entityLock(*replacementOwner->stateMtx);
        replacementOwner->chest.slots[0] = cppfm::ItemStack::of(
            cppfm::gen::itemIdByName().at("minecraft:diamond"), 1);
    }
    cppfm::nbt::Value replacementOutput =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    replacementStore.writeChunkNbt(2, -1, replacementOutput);
    bool replacementInheritedOldComponents = false;
    for (const auto& entity : replacementOutput.list) {
        const auto* id = entity.get("id");
        if (!id || id->tag != cppfm::nbt::String ||
            id->str != "minecraft:chest")
            continue;
        const auto* replacedItems = entity.get("Items");
        if (!replacedItems || replacedItems->tag != cppfm::nbt::List) continue;
        for (const auto& item : replacedItems->list) {
            const auto* itemId = item.get("id");
            const auto* slot = item.get("Slot");
            if (!itemId || itemId->tag != cppfm::nbt::String ||
                itemId->str != "minecraft:diamond" || !slot ||
                slot->tag != cppfm::nbt::Byte || slot->b != 0)
                continue;
            replacementInheritedOldComponents = item.get("components") != nullptr;
        }
    }
    check(!replacementInheritedOldComponents,
          "replacing an item with the same ID cannot duplicate stale components");

    if (chestOwner) {
        std::lock_guard entityLock(*chestOwner->stateMtx);
        chestOwner->chest.slots[3] = chestOwner->chest.slots[0];
        chestOwner->chest.slots[0] = cppfm::ItemStack::air();
    }
    cppfm::nbt::Value movedItemOutput =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    store.writeChunkNbt(2, -1, movedItemOutput);
    bool movedDiamondDataPreserved = false;
    bool oldSlotCleared = true;
    bool editedPickaxePreserved = false;
    for (const auto& entity : movedItemOutput.list) {
        const auto* id = entity.get("id");
        if (!id || id->tag != cppfm::nbt::String ||
            id->str != "minecraft:chest")
            continue;
        const auto* movedItems = entity.get("Items");
        if (!movedItems || movedItems->tag != cppfm::nbt::List) continue;
        for (const auto& item : movedItems->list) {
            const auto* itemId = item.get("id");
            const auto* slot = item.get("Slot");
            if (!itemId || itemId->tag != cppfm::nbt::String) continue;
            if (itemId->str == "minecraft:diamond" && slot &&
                slot->tag == cppfm::nbt::Byte && slot->b == 0)
                oldSlotCleared = false;
            if (itemId->str == "minecraft:diamond" && slot &&
                slot->tag == cppfm::nbt::Byte && slot->b == 3) {
                const auto* components = item.get("components");
                const auto* payload = components
                    ? components->get("minecraft:damage") : nullptr;
                movedDiamondDataPreserved = payload &&
                    payload->tag == cppfm::nbt::Int && payload->i == 2;
            }
            if (itemId->str == "minecraft:diamond_pickaxe" && slot &&
                slot->tag == cppfm::nbt::Byte && slot->b == 2) {
                const auto* components = item.get("components");
                const auto* damage = components
                    ? components->get("minecraft:damage") : nullptr;
                editedPickaxePreserved = damage &&
                    damage->tag == cppfm::nbt::Int && damage->i == 9;
            }
        }
    }
    check(movedDiamondDataPreserved && oldSlotCleared &&
              editedPickaxePreserved,
          "moving and editing known component-bearing items preserves metadata without duplicate source slots");

    cppfm::nbt::Value potionRoot = cppfm::nbt::Value::makeCompound();
    cppfm::nbt::Value potionEntities =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value brewing = cppfm::nbt::Value::makeCompound();
    brewing.set("id",
                cppfm::nbt::Value::makeString("minecraft:brewing_stand"));
    brewing.set("x", cppfm::nbt::Value::makeInt(34));
    brewing.set("y", cppfm::nbt::Value::makeInt(64));
    brewing.set("z", cppfm::nbt::Value::makeInt(-1));
    cppfm::nbt::Value potionItem = cppfm::nbt::Value::makeCompound();
    potionItem.set("id", cppfm::nbt::Value::makeString("minecraft:potion"));
    potionItem.set("Count", cppfm::nbt::Value::makeByte(1));
    potionItem.set("Slot", cppfm::nbt::Value::makeByte(0));
    cppfm::nbt::Value potionComponents = cppfm::nbt::Value::makeCompound();
    cppfm::nbt::Value potionContents = cppfm::nbt::Value::makeCompound();
    potionContents.set("potion",
                       cppfm::nbt::Value::makeString("minecraft:water"));
    potionContents.set("custom_color", cppfm::nbt::Value::makeInt(0x123456));
    potionContents.set("custom_name",
                       cppfm::nbt::Value::makeString("keep this label"));
    cppfm::nbt::Value customEffects =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value customEffect = cppfm::nbt::Value::makeCompound();
    customEffect.set("id",
                     cppfm::nbt::Value::makeString("minecraft:speed"));
    customEffect.set("amplifier", cppfm::nbt::Value::makeInt(1));
    customEffect.set("duration", cppfm::nbt::Value::makeInt(200));
    customEffect.set("ambient", cppfm::nbt::Value::makeByte(1));
    customEffect.set("show_particles", cppfm::nbt::Value::makeByte(0));
    customEffect.set("show_icon", cppfm::nbt::Value::makeByte(1));
    cppfm::nbt::Value hiddenEffect = cppfm::nbt::Value::makeCompound();
    hiddenEffect.set("id",
                     cppfm::nbt::Value::makeString("minecraft:resistance"));
    hiddenEffect.set("amplifier", cppfm::nbt::Value::makeInt(0));
    hiddenEffect.set("duration", cppfm::nbt::Value::makeInt(80));
    customEffect.set("hidden_effect", std::move(hiddenEffect));
    customEffects.list.push_back(std::move(customEffect));
    potionContents.set("custom_effects", std::move(customEffects));
    potionComponents.set("minecraft:potion_contents",
                         std::move(potionContents));
    potionItem.set("components", std::move(potionComponents));
    cppfm::nbt::Value brewingItems =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    brewingItems.list.push_back(std::move(potionItem));
    brewing.set("Items", std::move(brewingItems));
    potionEntities.list.push_back(std::move(brewing));
    potionRoot.set("block_entities", std::move(potionEntities));

    cppfm::BlockEntityStore potionStore;
    potionStore.readChunkNbt(potionRoot);
    const auto potionOwner = potionStore.getShared(cppfm::posKey(34, 64, -1));
    bool potionLoaded = false;
    if (potionOwner) {
        std::lock_guard entityLock(*potionOwner->stateMtx);
        auto& stack = potionOwner->brewing.slots[0];
        potionLoaded = stack.hasPotionContents() && stack.getPotionId() == 0 &&
                       static_cast<bool>(stack.persistedNbt);
        stack.setPotionId(cppfm::ItemStack::potionIdByName(
            "minecraft:awkward"));
        stack.itemId = cppfm::gen::itemIdByName().at(
            "minecraft:splash_potion");
    }
    check(potionLoaded,
          "saved potion contents become a runtime component when a container loads");

    cppfm::nbt::Value savedPotionEntities =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    potionStore.writeChunkNbt(2, -1, savedPotionEntities);
    bool potionNbtPatched = false;
    for (const auto& entity : savedPotionEntities.list) {
        const auto* entityId = entity.get("id");
        const auto* savedItems = entity.get("Items");
        if (!entityId || entityId->tag != cppfm::nbt::String ||
            entityId->str != "minecraft:brewing_stand" || !savedItems ||
            savedItems->tag != cppfm::nbt::List)
            continue;
        for (const auto& item : savedItems->list) {
            const auto* itemId = item.get("id");
            const auto* slot = item.get("Slot");
            if (!itemId || itemId->tag != cppfm::nbt::String ||
                itemId->str != "minecraft:splash_potion" || !slot ||
                slot->tag != cppfm::nbt::Byte || slot->b != 0)
                continue;
            const auto* components = item.get("components");
            const auto* contents = components
                ? components->get("minecraft:potion_contents") : nullptr;
            const auto* potion = contents ? contents->get("potion") : nullptr;
            const auto* color = contents
                ? contents->get("custom_color") : nullptr;
            const auto* name = contents
                ? contents->get("custom_name") : nullptr;
            const auto* savedEffects = contents
                ? contents->get("custom_effects") : nullptr;
            const auto* effect = savedEffects &&
                    savedEffects->tag == cppfm::nbt::List &&
                    !savedEffects->list.empty()
                ? &savedEffects->list.front() : nullptr;
            const auto* hidden = effect ? effect->get("hidden_effect") : nullptr;
            potionNbtPatched = potion && potion->tag == cppfm::nbt::String &&
                potion->str == "minecraft:awkward" && color &&
                color->tag == cppfm::nbt::Int && color->i == 0x123456 &&
                name && name->tag == cppfm::nbt::String &&
                name->str == "keep this label" && effect && hidden;
        }
    }
    check(potionNbtPatched,
          "brewing edits the potion registry value without dropping supported custom data or effects");

    cppfm::nbt::Value potionRoundTripRoot = cppfm::nbt::Value::makeCompound();
    potionRoundTripRoot.set("block_entities", std::move(savedPotionEntities));
    cppfm::BlockEntityStore potionRoundTrip;
    potionRoundTrip.readChunkNbt(potionRoundTripRoot);
    const auto roundTripOwner =
        potionRoundTrip.getShared(cppfm::posKey(34, 64, -1));
    bool potionRuntimeRoundTrip = false;
    if (roundTripOwner) {
        std::lock_guard entityLock(*roundTripOwner->stateMtx);
        const auto& stack = roundTripOwner->brewing.slots[0];
        auto protocolProjection = stack;
        protocolProjection.persistedNbt.reset();
        cppfm::WriteBuffer wire;
        protocolProjection.write(wire);
        cppfm::ReadBuffer input(wire.data);
        const auto decoded = cppfm::ItemStack::read(input);
        potionRuntimeRoundTrip = protocolProjection.itemId == decoded.itemId &&
            decoded.getPotionId() == cppfm::ItemStack::potionIdByName(
                "minecraft:awkward") && input.remaining() == 0;
    }
    check(potionRuntimeRoundTrip,
          "modified persisted potion survives save/load and remains in client wire data");

    store.remove(cppfm::posKey(32, 64, -1));
    cppfm::nbt::Value afterRemoval =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    store.writeChunkNbt(2, -1, afterRemoval);
    const bool beaconStillPresent = std::any_of(
        afterRemoval.list.begin(), afterRemoval.list.end(), [](const auto& e) {
            const auto* id = e.get("id");
            return id && id->tag == cppfm::nbt::String &&
                   id->str == "minecraft:beacon";
        });
    check(!beaconStillPresent && afterRemoval.list.size() == 1,
          "removing an opaque block entity does not affect another typed entity");

    const auto destinationKey = cppfm::posKey(41, 64, -1);
    const auto existingDestination = store.createShared(
        destinationKey, cppfm::BlockEntity::Kind::Chest);
    const bool relocated = store.replaceNbtAt(41, 64, -1, *savedChest);
    const auto retainedDestination = store.getShared(destinationKey);
    cppfm::nbt::Value relocatedEntities =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    store.writeChunkNbt(2, -1, relocatedEntities);
    const cppfm::nbt::Value* relocatedChest = nullptr;
    for (const auto& entity : relocatedEntities.list) {
        const auto* x = entity.get("x");
        const auto* y = entity.get("y");
        const auto* z = entity.get("z");
        if (x && x->tag == cppfm::nbt::Int && x->i == 41 &&
            y && y->tag == cppfm::nbt::Int && y->i == 64 &&
            z && z->tag == cppfm::nbt::Int && z->i == -1)
            relocatedChest = &entity;
    }
    const auto* relocatedName = relocatedChest
        ? relocatedChest->get("CustomName") : nullptr;
    check(relocated && retainedDestination == existingDestination &&
              relocatedChest && relocatedName &&
              relocatedName->tag == cppfm::nbt::String &&
              relocatedName->str == "preserve chest metadata",
          "block-entity relocation updates coordinates, preserves raw metadata, and retains live typed ownership");
}

void testDimensionYBoundsAndChunkResourceLimits() {
    using namespace cppfm;
    nbt::Value netherRoot = nbt::Value::makeCompound();
    netherRoot.set("xPos", nbt::Value::makeInt(0));
    netherRoot.set("zPos", nbt::Value::makeInt(0));
    nbt::Value records = nbt::Value::makeList(nbt::Compound);
    nbt::Value outsideNether = nbt::Value::makeCompound();
    outsideNether.set("id", nbt::Value::makeString(
        "example:opaque_block_entity"));
    outsideNether.set("x", nbt::Value::makeInt(1));
    outsideNether.set("y", nbt::Value::makeInt(200));
    outsideNether.set("z", nbt::Value::makeInt(1));
    records.list.push_back(std::move(outsideNether));
    netherRoot.set("block_entities", std::move(records));

    BlockEntityStore netherStore(0, 128);
    bool netherYRejected = false;
    try {
        netherStore.readChunkNbt(netherRoot);
    } catch (const std::runtime_error&) {
        netherYRejected = true;
    }
    bool stagedValidationRejected = false;
    try {
        BlockEntityStore::validateChunkNbt(netherRoot, 0, 128);
    } catch (const std::runtime_error&) {
        stagedValidationRejected = true;
    }
    check(netherYRejected && stagedValidationRejected && netherStore.empty(),
          "block entities outside a dimension's Y range are rejected before live-store publication and staged-file promotion");
    check(!netherStore.tryCreateShared(posKey(1, 200, 1),
                                       BlockEntity::Kind::Chest) &&
              !netherStore.replaceNbtAt(1, 200, 1, std::nullopt),
          "runtime block-entity creation and replacement enforce dimension Y bounds");

    nbt::Value oversizedRoot = nbt::Value::makeCompound();
    oversizedRoot.set("xPos", nbt::Value::makeInt(0));
    oversizedRoot.set("zPos", nbt::Value::makeInt(0));
    nbt::Value oversizedRecords = nbt::Value::makeList(
        nbt::Compound, BlockEntityStore::kMaxChunkBlockEntityRecords + 1);
    for (std::size_t i = 0;
         i <= BlockEntityStore::kMaxChunkBlockEntityRecords; ++i) {
        nbt::Value entity = nbt::Value::makeCompound();
        entity.set("id", nbt::Value::makeString(
            "example:opaque_block_entity"));
        entity.set("x", nbt::Value::makeInt(
            static_cast<std::int32_t>(i % 16)));
        entity.set("z", nbt::Value::makeInt(
            static_cast<std::int32_t>((i / 16) % 16)));
        entity.set("y", nbt::Value::makeInt(
            -64 + static_cast<std::int32_t>(i / 256)));
        oversizedRecords.list.push_back(std::move(entity));
    }
    oversizedRoot.set("block_entities", std::move(oversizedRecords));
    BlockEntityStore boundedStore;
    bool recordLimitRejected = false;
    try {
        boundedStore.readChunkNbt(oversizedRoot);
    } catch (const std::length_error&) {
        recordLimitRejected = true;
    }
    check(recordLimitRejected && boundedStore.empty(),
          "chunk block-entity decoding stops at the explicit record budget without partial publication");
}

void testLiveStoreBudgetsAndChunkEviction() {
    using namespace cppfm;
    BlockEntityStore bounded(-64, 320, 2, 1024, 3, 2048);
    const auto first = posKey(1, 64, 1);
    const auto second = posKey(2, 64, 2);
    const auto third = posKey(3, 64, 3);
    const auto globalFirst = posKey(16, 64, 1);
    const auto globalSecond = posKey(17, 64, 2);
    const auto globalOverflow = posKey(32, 64, 3);
    check(static_cast<bool>(bounded.tryCreateShared(
              first, BlockEntity::Kind::Chest)) &&
              static_cast<bool>(bounded.tryCreateShared(
                  second, BlockEntity::Kind::Chest)),
          "live block entities fit within the configured per-chunk budget");
    check(!bounded.tryCreateShared(third, BlockEntity::Kind::Chest),
          "runtime creation cannot exceed the per-chunk block-entity cap");
    bounded.forgetChunk(0, 0);
    check(bounded.empty() && bounded.size() == 0 &&
              static_cast<bool>(bounded.tryCreateShared(
                  third, BlockEntity::Kind::Chest)),
          "evicting a saved chunk releases its block-entity cache and capacity");
    check(static_cast<bool>(bounded.tryCreateShared(
              globalFirst, BlockEntity::Kind::Chest)) &&
              static_cast<bool>(bounded.tryCreateShared(
                  globalSecond, BlockEntity::Kind::Chest)) &&
              !bounded.tryCreateShared(globalOverflow,
                                       BlockEntity::Kind::Chest),
          "runtime creation also observes the store-wide resident-record cap");

    BlockEntityStore rawBounded(-64, 320, 2, 4096, 3, 8192);
    nbt::Value oversizedRoot = nbt::Value::makeCompound();
    nbt::Value entities = nbt::Value::makeList(nbt::Compound);
    nbt::Value opaque = nbt::Value::makeCompound();
    opaque.set("id", nbt::Value::makeString("example:opaque"));
    opaque.set("x", nbt::Value::makeInt(0));
    opaque.set("y", nbt::Value::makeInt(64));
    opaque.set("z", nbt::Value::makeInt(0));
    opaque.set("payload", nbt::Value::makeString(std::string(11000, 'x')));
    entities.list.push_back(std::move(opaque));
    oversizedRoot.set("block_entities", std::move(entities));
    cppfm::nbt::Value replacement =
        oversizedRoot.get("block_entities")->list.front();
    check(!rawBounded.replaceNbtAt(0, 64, 0, std::move(replacement)) &&
              rawBounded.empty(),
          "runtime NBT replacement cannot exceed the chunk memory budget");
    bool aggregateNbtRejected = false;
    try {
        rawBounded.readChunkNbt(oversizedRoot);
    } catch (const std::length_error&) {
        aggregateNbtRejected = true;
    }
    check(aggregateNbtRejected && rawBounded.empty(),
          "oversized raw block-entity NBT is rejected without changing live state");
    nbt::Value smallRoot = nbt::Value::makeCompound();
    nbt::Value smallEntities = nbt::Value::makeList(nbt::Compound);
    nbt::Value smallOpaque = nbt::Value::makeCompound();
    smallOpaque.set("id", nbt::Value::makeString("example:opaque"));
    smallOpaque.set("x", nbt::Value::makeInt(0));
    smallOpaque.set("y", nbt::Value::makeInt(64));
    smallOpaque.set("z", nbt::Value::makeInt(0));
    smallEntities.list.push_back(std::move(smallOpaque));
    smallRoot.set("block_entities", std::move(smallEntities));
    rawBounded.readChunkNbt(smallRoot);
    check(rawBounded.size() == 1,
          "valid opaque block-entity data is retained in the bounded store");
    rawBounded.forgetChunk(0, 0);
    check(rawBounded.empty(),
          "chunk eviction also releases opaque NBT and its budget accounting");

    BlockEntityStore writeBounded(-64, 320, 4, 512, 8, 1024);
    auto longSign = writeBounded.createShared(
        posKey(0, 64, 0), BlockEntity::Kind::Sign);
    longSign->sign.front[0].assign(700, 's');
    nbt::Value serialized = nbt::Value::makeList(nbt::Compound);
    bool overBudgetSaveRejected = false;
    try {
        writeBounded.writeChunkNbt(0, 0, serialized);
    } catch (const std::length_error&) {
        overBudgetSaveRejected = true;
    }
    check(overBudgetSaveRejected && serialized.list.empty(),
          "serializer refuses runtime block-entity state that reload would reject");
}

void testForgetChunkClearsDeletedTombstonesBesideLiveRecords() {
    using namespace cppfm;
    const auto removedKey = posKey(1, 64, 1);
    const auto retainedKey = posKey(2, 64, 2);

    BlockEntityStore source;
    source.createShared(removedKey, BlockEntity::Kind::Chest);
    nbt::Value records = nbt::Value::makeList(nbt::Compound);
    const bool serialized = source.writeChunkNbt(0, 0, records);
    nbt::Value savedChunk = nbt::Value::makeCompound();
    savedChunk.set("block_entities", std::move(records));

    BlockEntityStore store;
    store.createShared(removedKey, BlockEntity::Kind::Chest);
    store.createShared(retainedKey, BlockEntity::Kind::Barrel);
    const bool removed = store.remove(removedKey);
    store.forgetChunk(0, 0);
    store.readChunkNbt(savedChunk); // Legacy/test roots may omit xPos/zPos.

    check(serialized && removed && store.size() == 1 &&
              store.containsAt(posKeyUnpackX(removedKey),
                               posKeyUnpackY(removedKey),
                               posKeyUnpackZ(removedKey)) &&
              !store.containsAt(posKeyUnpackX(retainedKey),
                                posKeyUnpackY(retainedKey),
                                posKeyUnpackZ(retainedKey)),
          "forgetting a chunk clears removed-record tombstones even when the live index was non-empty");
}

void testBlockEntityIdExactnessAndUnlockedRemovalHook() {
    const auto makeRoot = [](const std::string& id, std::int32_t x) {
        cppfm::nbt::Value root = cppfm::nbt::Value::makeCompound();
        root.set("xPos", cppfm::nbt::Value::makeInt(2));
        root.set("zPos", cppfm::nbt::Value::makeInt(-1));
        auto entries = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
        auto entry = cppfm::nbt::Value::makeCompound();
        entry.set("id", cppfm::nbt::Value::makeString(id));
        entry.set("x", cppfm::nbt::Value::makeInt(x));
        entry.set("y", cppfm::nbt::Value::makeInt(64));
        entry.set("z", cppfm::nbt::Value::makeInt(-1));
        entry.set("owner_data",
                  cppfm::nbt::Value::makeString("keep exact type"));
        entries.list.push_back(std::move(entry));
        root.set("block_entities", std::move(entries));
        return root;
    };

    cppfm::BlockEntityStore modded;
    const auto moddedRoot = makeRoot("example:reinforced_chest", 32);
    modded.readChunkNbt(moddedRoot);
    cppfm::nbt::Value moddedSaved =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    modded.writeChunkNbt(2, -1, moddedSaved);
    const auto* moddedId = moddedSaved.list.empty()
        ? nullptr : moddedSaved.list.front().get("id");
    const auto* moddedOwnerData = moddedSaved.list.empty()
        ? nullptr : moddedSaved.list.front().get("owner_data");
    check(modded.size() == 1 && !modded.getShared(cppfm::posKey(32, 64, -1)) &&
              moddedId && moddedId->tag == cppfm::nbt::String &&
              moddedId->str == "example:reinforced_chest" && moddedOwnerData &&
              moddedOwnerData->tag == cppfm::nbt::String &&
              moddedOwnerData->str == "keep exact type",
          "modded chest-like block entities remain opaque instead of becoming vanilla chests");

    cppfm::BlockEntityStore trapped;
    trapped.readChunkNbt(makeRoot("minecraft:trapped_chest", 33));
    const auto trappedOwner = trapped.getShared(cppfm::posKey(33, 64, -1));
    bool trappedRecognized = false;
    if (trappedOwner) {
        std::lock_guard entityLock(*trappedOwner->stateMtx);
        trappedRecognized =
            trappedOwner->kind == cppfm::BlockEntity::Kind::Chest;
    }
    cppfm::nbt::Value trappedSaved =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    trapped.writeChunkNbt(2, -1, trappedSaved);
    const auto* trappedId = trappedSaved.list.empty()
        ? nullptr : trappedSaved.list.front().get("id");
    check(trappedRecognized && trappedId &&
              trappedId->tag == cppfm::nbt::String &&
              trappedId->str == "minecraft:trapped_chest",
          "recognized vanilla trapped chests retain their exact serialized block-entity ID");

    cppfm::BlockEntityStore callbacks;
    const auto key = cppfm::posKey(32, 64, -1);
    callbacks.createShared(key, cppfm::BlockEntity::Kind::Chest);
    std::atomic<bool> concurrentStoreAccess{false};
    bool callbackRanWithoutStoreLock = false;
    std::thread probe;
    callbacks.setDirtyCallback([&](std::int32_t, std::int32_t) {
        probe = std::thread([&] {
            (void)callbacks.size();
            concurrentStoreAccess.store(true, std::memory_order_release);
        });
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(1);
        while (!concurrentStoreAccess.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        callbackRanWithoutStoreLock =
            concurrentStoreAccess.load(std::memory_order_acquire);
    });
    callbacks.remove(key);
    if (probe.joinable()) probe.join();
    check(callbackRanWithoutStoreLock,
          "block-entity removal invokes dirty callbacks after releasing its store lock");

    cppfm::BlockEntityStore exceptionSafeRemoval;
    const auto exceptionSafeKey = cppfm::posKey(4, 64, 4);
    exceptionSafeRemoval.createShared(exceptionSafeKey,
                                      cppfm::BlockEntity::Kind::Chest);
    auto failCallbackCopy = std::make_shared<std::atomic<bool>>(false);
    auto dirtyCallbackInvocations = std::make_shared<std::atomic<int>>(0);
    std::function<void(std::int32_t, std::int32_t)> stagedCallback =
        CopyControlledDirtyCallback(failCallbackCopy,
                                   dirtyCallbackInvocations);
    exceptionSafeRemoval.setDirtyCallback(stagedCallback);
    failCallbackCopy->store(true, std::memory_order_relaxed);
    exceptionSafeRemoval.remove(exceptionSafeKey);
    failCallbackCopy->store(false, std::memory_order_relaxed);
    const auto stillLive = exceptionSafeRemoval.getShared(exceptionSafeKey);
    exceptionSafeRemoval.setDirtyCallback({});
    check(!stillLive && exceptionSafeRemoval.empty() &&
              dirtyCallbackInvocations->load(std::memory_order_relaxed) == 1,
          "committed block-entity removal dispatches its stable callback without a throwing copy");

    cppfm::BlockEntityStore throwingRemoval;
    const auto throwingRemovalKey = cppfm::posKey(5, 64, 5);
    throwingRemoval.createShared(throwingRemovalKey,
                                 cppfm::BlockEntity::Kind::Chest);
    throwingRemoval.setDirtyCallback([](std::int32_t, std::int32_t) {
        throw std::runtime_error("injected dirty callback failure");
    });
    bool removalCallbackThrew = false;
    try {
        throwingRemoval.remove(throwingRemovalKey);
    } catch (const std::runtime_error&) {
        removalCallbackThrew = true;
    }
    const auto preservedAfterCallbackFailure =
        throwingRemoval.getShared(throwingRemovalKey);
    throwingRemoval.setDirtyCallback({});
    check(removalCallbackThrew && preservedAfterCallbackFailure &&
              throwingRemoval.size() == 1,
          "a throwing dirty callback leaves the original block entity published");

    cppfm::BlockEntityStore worldReplacementFailure;
    const auto worldReplacementKey = cppfm::posKey(5, 64, 6);
    worldReplacementFailure.createShared(
        worldReplacementKey, cppfm::BlockEntity::Kind::Chest);
    worldReplacementFailure.setDirtyCallback(
        [](std::int32_t, std::int32_t) {
            throw std::runtime_error("injected world replacement dirty failure");
        });
    const bool replacementDirtyAccepted =
        worldReplacementFailure.removeAfterWorldBlockReplacement(
            worldReplacementKey);
    check(!replacementDirtyAccepted && worldReplacementFailure.empty(),
          "a block replacement removes stale entity state even when dirty admission fails");

    cppfm::BlockEntityStore reentrantRemoval;
    const auto reentrantRemovalKey = cppfm::posKey(6, 64, 6);
    reentrantRemoval.createShared(reentrantRemovalKey,
                                  cppfm::BlockEntity::Kind::Chest);
    int reentrantRemovalCallbackCount = 0;
    reentrantRemoval.setDirtyCallback([&](std::int32_t, std::int32_t) {
        ++reentrantRemovalCallbackCount;
        reentrantRemoval.remove(reentrantRemovalKey);
    });
    reentrantRemoval.remove(reentrantRemovalKey);
    check(reentrantRemoval.empty() && reentrantRemovalCallbackCount == 1,
          "same-key reentrant removal is bounded and the outer commit removes the record");

    cppfm::BlockEntityStore reentrantOpaqueReplacement;
    const auto reentrantOpaqueKey = cppfm::posKey(7, 64, 7);
    reentrantOpaqueReplacement.createShared(
        reentrantOpaqueKey, cppfm::BlockEntity::Kind::Chest);
    bool callbackInstalledOpaqueReplacement = false;
    reentrantOpaqueReplacement.setDirtyCallback(
        [&](std::int32_t, std::int32_t) {
            reentrantOpaqueReplacement.setDirtyCallback({});
            auto opaque = cppfm::nbt::Value::makeCompound();
            opaque.set("id", cppfm::nbt::Value::makeString(
                                  "example:future_block_entity"));
            callbackInstalledOpaqueReplacement =
                reentrantOpaqueReplacement.replaceNbtAt(
                    7, 64, 7, std::move(opaque));
        });
    const bool staleRemovalReportedSuccess =
        reentrantOpaqueReplacement.replaceNbtAt(
            7, 64, 7, std::nullopt);
    cppfm::nbt::Value retainedOpaque =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    reentrantOpaqueReplacement.writeChunkNbt(0, 0, retainedOpaque);
    const auto* retainedOpaqueId = retainedOpaque.list.empty()
        ? nullptr : retainedOpaque.list.front().get("id");
    check(callbackInstalledOpaqueReplacement &&
              !staleRemovalReportedSuccess &&
              reentrantOpaqueReplacement.containsAt(7, 64, 7) &&
              retainedOpaqueId && retainedOpaqueId->tag == cppfm::nbt::String &&
              retainedOpaqueId->str == "example:future_block_entity",
          "an absent-entity replacement reports a re-entrant owner change instead of false success");

    cppfm::BlockEntityStore indexed;
    indexed.createShared(cppfm::posKey(1, 64, 1),
                         cppfm::BlockEntity::Kind::Chest);
    const auto unrelated = indexed.createShared(
        cppfm::posKey(32, 64, 1), cppfm::BlockEntity::Kind::Chest);
    std::unique_lock unrelatedLock(*unrelated->stateMtx);
    cppfm::nbt::Value saved =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    std::atomic<bool> writeFinished{false};
    std::thread writer([&] {
        indexed.writeChunkNbt(0, 0, saved);
        writeFinished.store(true, std::memory_order_release);
    });
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(1);
    while (!writeFinished.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    const bool completedWithoutUnrelatedLock =
        writeFinished.load(std::memory_order_acquire);
    unrelatedLock.unlock();
    writer.join();
    check(completedWithoutUnrelatedLock && saved.list.size() == 1,
          "saving one chunk indexes only its entities and never waits on another chunk's state lock");
}

void testSameKindBlockEntityEnsureIsIdempotent() {
    cppfm::BlockEntityStore store;
    const auto key = cppfm::posKey(8, 64, -4);
    const auto owner = store.createShared(
        key, cppfm::BlockEntity::Kind::Chest);
    {
        std::lock_guard entityLock(*owner->stateMtx);
        owner->chest.slots[0].itemId = 42;
        owner->chest.slots[0].count = 7;
    }

    std::atomic<int> dirtyCalls{0};
    store.setDirtyCallback([&](std::int32_t, std::int32_t) {
        dirtyCalls.fetch_add(1, std::memory_order_relaxed);
    });
    const auto ensured = store.createShared(
        key, cppfm::BlockEntity::Kind::Chest);
    bool inventoryPreserved = false;
    {
        std::lock_guard entityLock(*ensured->stateMtx);
        inventoryPreserved = ensured->chest.slots[0].itemId == 42 &&
                             ensured->chest.slots[0].count == 7;
    }
    check(ensured == owner && inventoryPreserved && dirtyCalls.load() == 0,
          "same-kind block-entity ensure preserves its owner and inventory without dirtying it");

    const auto replaced = store.createShared(
        key, cppfm::BlockEntity::Kind::Hopper);
    bool replacementIsEmpty = false;
    {
        std::lock_guard entityLock(*replaced->stateMtx);
        replacementIsEmpty =
            replaced->kind == cppfm::BlockEntity::Kind::Hopper &&
            replaced->chest.slots[0].itemId == 0 &&
            replaced->chest.slots[0].count == 0;
    }
    check(replaced == owner && replacementIsEmpty && dirtyCalls.load() == 1,
          "explicit block-entity type replacement resets data while retaining shared-owner identity");
}

void testTransientPistonHasOnlyAnInitialChunkProjection() {
    using namespace cppfm;
    BlockEntityStore store;
    const auto owner = store.createShared(
        posKey(-1, 64, -2), BlockEntity::Kind::MovingPiston);
    owner->movingPiston.state = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    owner->movingPiston.facing = 5;
    owner->movingPiston.extending = false;
    owner->movingPiston.source = true;
    owner->movingPiston.progress = 0.5f;

    auto durable = nbt::Value::makeList(nbt::Compound);
    const bool durableWritten = store.writeChunkNbt(-1, -1, durable);
    auto initial = nbt::Value::makeList(nbt::Compound);
    const bool initialWritten =
        store.writeChunkInitialNbt(-1, -1, initial);
    const auto* record = initial.list.empty() ? nullptr : &initial.list.front();
    const auto* id = record ? record->get("id") : nullptr;
    const auto* blockState = record ? record->get("blockState") : nullptr;
    const auto* facing = record ? record->get("facing") : nullptr;
    const auto* progress = record ? record->get("progress") : nullptr;
    const auto* source = record ? record->get("source") : nullptr;
    check(durableWritten && durable.list.empty(),
          "transient piston animation remains excluded from durable chunk NBT");
    check(initialWritten && initial.list.size() == 1 && id &&
              id->tag == nbt::String && id->str == "minecraft:piston" &&
              blockState && blockState->tag == nbt::Compound &&
              facing && facing->tag == nbt::Int && facing->i == 5 &&
              progress && progress->tag == nbt::Float &&
              progress->f == 0.5f && source && source->tag == nbt::Byte &&
              source->b == 1,
          "transient piston exposes carried state and animation in initial chunks");
}

void testInitialChunkProjectionPreservesCampfireItems() {
    using namespace cppfm;
    auto campfire = nbt::Value::makeCompound();
    campfire.set("id", nbt::Value::makeString("minecraft:campfire"));
    campfire.set("x", nbt::Value::makeInt(1));
    campfire.set("y", nbt::Value::makeInt(64));
    campfire.set("z", nbt::Value::makeInt(2));
    auto items = nbt::Value::makeList(nbt::Compound);
    auto cookedItem = nbt::Value::makeCompound();
    cookedItem.set("id", nbt::Value::makeString("minecraft:cooked_beef"));
    cookedItem.set("Count", nbt::Value::makeByte(1));
    cookedItem.set("Slot", nbt::Value::makeByte(0));
    items.list.push_back(std::move(cookedItem));
    campfire.set("Items", std::move(items));

    const auto campfireUpdate = blockEntityUpdateTag(campfire);
    const auto* visibleItems = campfireUpdate
        ? campfireUpdate->get("Items") : nullptr;
    check(blockEntityWireTypeId("minecraft:campfire") == 33 &&
              visibleItems && visibleItems->tag == nbt::List &&
              visibleItems->list.size() == 1,
          "campfire's observable cooking items remain in initial chunk NBT");

    auto chest = nbt::Value::makeCompound();
    chest.set("id", nbt::Value::makeString("minecraft:chest"));
    chest.set("x", nbt::Value::makeInt(1));
    chest.set("y", nbt::Value::makeInt(64));
    chest.set("z", nbt::Value::makeInt(2));
    auto chestItems = nbt::Value::makeList(nbt::Compound);
    auto chestItem = nbt::Value::makeCompound();
    chestItem.set("id", nbt::Value::makeString("minecraft:cooked_beef"));
    chestItem.set("Count", nbt::Value::makeByte(1));
    chestItems.list.push_back(std::move(chestItem));
    chest.set("Items", std::move(chestItems));
    const auto chestUpdate = blockEntityUpdateTag(chest);
    check(!chestUpdate || !chestUpdate->get("Items"),
          "chest inventory remains private until its container screen opens");
}

void testItemIdentityAndTransactionalInsertion() {
    const auto diamondId = cppfm::gen::itemIdByName().at("minecraft:diamond");
    cppfm::ItemStack ordered = cppfm::ItemStack::of(diamondId, 1);
    ordered.components.emplace_back(3, std::vector<std::uint8_t>{0x04});
    ordered.components.emplace_back(5, std::vector<std::uint8_t>{0x0A, 0x0B});
    ordered.removedComponents = {9, 2, 11};
    cppfm::ItemStack reordered = cppfm::ItemStack::of(diamondId, 48);
    reordered.components = {ordered.components[1], ordered.components[0]};
    reordered.removedComponents = {11, 9, 2};
    check(ordered.sameItemData(reordered),
          "stack identity ignores component and removed-id ordering, not count");
    auto duplicateComponents = cppfm::ItemStack::of(diamondId, 1);
    duplicateComponents.components = {
        {3, {0x04}}, {3, {0x04}}, {5, {0x0A, 0x0B}}};
    auto reorderedDuplicates = cppfm::ItemStack::of(diamondId, 1);
    reorderedDuplicates.components = {
        {5, {0x0A, 0x0B}}, {3, {0x04}}, {3, {0x04}}};
    auto differentDuplicateCount = cppfm::ItemStack::of(diamondId, 1);
    differentDuplicateCount.components = {
        {5, {0x0A, 0x0B}}, {3, {0x04}}, {5, {0x0A, 0x0B}}};
    check(duplicateComponents.sameItemData(reorderedDuplicates) &&
              !duplicateComponents.sameItemData(differentDuplicateCount),
          "allocation-free component identity comparison preserves multiset multiplicity");

    auto fixedDamageCost = cppfm::ItemStack::of(diamondId, 1);
    fixedDamageCost.components = {
        {3, {0x05}}, // damage = VarInt(5)
        {17, {0x02}}, // repair_cost = VarInt(2)
    };
    cppfm::WriteBuffer fixedItemWire;
    fixedDamageCost.write(fixedItemWire);
    const std::vector<std::uint8_t> vanillaDamageCostFixture{
        0x01, 0xC4, 0x06, // count 1, diamond item id 836
        0x02, 0x00,       // two added components, no removed components
        0x03, 0x05,       // damage component 3, payload 5
        0x11, 0x02,       // repair_cost component 17, payload 2
    };
    check(fixedItemWire.data == vanillaDamageCostFixture,
          "item component bytes match the fixed vanilla 1.21.4 Slot codec fixture");
    cppfm::ReadBuffer fixedItemInput(vanillaDamageCostFixture);
    const auto fixedItemDecoded = cppfm::ItemStack::read(fixedItemInput);
    check(fixedItemInput.remaining() == 0 &&
              fixedItemDecoded.sameKnownItemData(fixedDamageCost),
          "fixed vanilla item-component fixture decodes without payload lengths");

    auto trimmedArmor = cppfm::ItemStack::of(
        cppfm::gen::itemIdByName().at("minecraft:iron_chestplate"), 1);
    cppfm::ItemStack::ArmorTrim ironCoastTrim;
    ironCoastTrim.pattern = "minecraft:coast";
    ironCoastTrim.material = "minecraft:iron";
    ironCoastTrim.has = true;
    const bool trimAccepted = trimmedArmor.setTrim(ironCoastTrim);
    cppfm::WriteBuffer trimWire;
    trimmedArmor.write(trimWire);
    const std::vector<std::uint8_t> vanillaTrimFixture{
        0x01, 0x80, 0x07, // count 1, iron chestplate item id 896
        0x01, 0x00,       // one added component, no removed components
        0x2D, 0x05, 0x01, 0x01, // trim 45: iron 5, coast 1, tooltip true
    };
    check(trimAccepted && trimWire.data == vanillaTrimFixture,
          "trim component uses vanilla material-pattern-tooltip field order");
    auto invalidTrim = ironCoastTrim;
    invalidTrim.pattern = "example:unknown_pattern";
    const auto acceptedTrimComponents = trimmedArmor.components;
    check(!trimmedArmor.setTrim(invalidTrim) &&
              trimmedArmor.components == acceptedTrimComponents,
          "unknown trim registry keys are rejected without substituting a different trim");

    auto formerIdAliases = cppfm::ItemStack::of(diamondId, 1);
    formerIdAliases.components = {
        {6, {0x08, 0x00, 0x01, 'x'}}, // item_name: anonymous NBT string
        {7, {0x01, 'A'}}, // item_model: string "A"
        {21, {0x01, 0x02, 0x03, 0x01}}, // not an enchantments component
    };
    check(formerIdAliases.getDamage() == 0 &&
              formerIdAliases.getRepairCost() == 0 &&
              formerIdAliases.enchantLevel("minecraft:binding_curse") == 0,
          "current 1.21.4 item_name, item_model, and food IDs are not mistaken for legacy item components");

    std::array<cppfm::ItemStack, 46> inventory{};
    for (int slot = 9; slot < 45; ++slot)
        inventory[static_cast<std::size_t>(slot)] =
            cppfm::ItemStack::of(diamondId, 64);
    auto decorated = cppfm::ItemStack::of(diamondId, 1);
    decorated.components.emplace_back(5, std::vector<std::uint8_t>{0x7F});
    check(!cppfm::insertCompletePlayerStack(inventory, decorated, 1, 64),
          "different component data cannot be merged into a full inventory");
    bool unchanged = true;
    for (int slot = 9; slot < 45; ++slot) {
        const auto& item = inventory[static_cast<std::size_t>(slot)];
        unchanged = unchanged && item.itemId == diamondId && item.count == 64 &&
                    item.components.empty();
    }
    check(unchanged,
          "failed full-inventory insertion is atomic and preserves every stack");

    cppfm::nbt::Value item = cppfm::nbt::Value::makeCompound();
    item.set("id", cppfm::nbt::Value::makeString("minecraft:diamond_pickaxe"));
    item.set("count", cppfm::nbt::Value::makeInt(3));
    item.set("Slot", cppfm::nbt::Value::makeByte(0));
    cppfm::nbt::Value components = cppfm::nbt::Value::makeCompound();
    components.set("minecraft:damage", cppfm::nbt::Value::makeInt(3));
    cppfm::nbt::Value opaque = cppfm::nbt::Value::makeCompound();
    opaque.set("zeta", cppfm::nbt::Value::makeInt(2));
    opaque.set("alpha", cppfm::nbt::Value::makeInt(1));
    components.set("example:opaque_component", std::move(opaque));
    item.set("components", std::move(components));
    auto loaded = cppfm::ItemStack::fromNbt(item);
    bool persisted = false;
    if (loaded) {
        loaded->setDamage(9);
        const auto saved = loaded->toNbt(12);
        const auto reloaded = cppfm::ItemStack::fromNbt(saved);
        const auto* savedComponents = saved.get("components");
        const auto* savedOpaque = savedComponents
            ? savedComponents->get("example:opaque_component") : nullptr;
        const auto* alpha = savedOpaque ? savedOpaque->get("alpha") : nullptr;
        const auto* damage = savedComponents
            ? savedComponents->get("minecraft:damage") : nullptr;
        persisted = reloaded && reloaded->getDamage() == 9 &&
                    reloaded->count == 3 &&
                    reloaded->persistedNbt && savedOpaque && alpha &&
                    alpha->tag == cppfm::nbt::Int && alpha->i == 1 &&
                    saved.get("count") &&
                    saved.get("count")->tag == cppfm::nbt::Int &&
                    saved.get("count")->i == 3 && !saved.get("Count") &&
                    damage && damage->tag == cppfm::nbt::Int &&
                    damage->i == 9;
    }
    check(persisted,
          "player inventory NBT save/reload patches known data and retains opaque item components");

    cppfm::nbt::Value mapItem = cppfm::nbt::Value::makeCompound();
    mapItem.set("id", cppfm::nbt::Value::makeString("minecraft:filled_map"));
    mapItem.set("count", cppfm::nbt::Value::makeInt(1));
    cppfm::nbt::Value mapComponents = cppfm::nbt::Value::makeCompound();
    mapComponents.set("minecraft:map_id", cppfm::nbt::Value::makeInt(73));
    mapItem.set("components", std::move(mapComponents));
    auto loadedMap = cppfm::ItemStack::fromNbt(mapItem);
    bool mapIdRoundTrip = false;
    if (loadedMap) {
        cppfm::WriteBuffer mapWire;
        loadedMap->write(mapWire);
        cppfm::ReadBuffer mapInput(mapWire.data);
        const auto decodedMap = cppfm::ItemStack::read(mapInput);
        const auto savedMap = decodedMap.toNbt(0);
        const auto* savedMapComponents = savedMap.get("components");
        const auto* savedMapId = savedMapComponents
            ? savedMapComponents->get("minecraft:map_id") : nullptr;
        mapIdRoundTrip = savedMapId && savedMapId->tag == cppfm::nbt::Int &&
                         savedMapId->i == 73 && mapInput.remaining() == 0;
    }
    check(mapIdRoundTrip,
          "filled-map map_id survives the NBT-to-protocol-to-player-save round trip");

    cppfm::nbt::Value decoratedItem = cppfm::nbt::Value::makeCompound();
    decoratedItem.set("id", cppfm::nbt::Value::makeString(
        "minecraft:leather_helmet"));
    decoratedItem.set("count", cppfm::nbt::Value::makeInt(1));
    cppfm::nbt::Value decoratedComponents = cppfm::nbt::Value::makeCompound();
    decoratedComponents.set("minecraft:item_name",
        cppfm::nbt::Value::makeString("{\"text\":\"Heirloom\"}"));
    decoratedComponents.set("minecraft:item_model",
        cppfm::nbt::Value::makeString("minecraft:diamond"));
    cppfm::nbt::Value entityData = cppfm::nbt::Value::makeCompound();
    entityData.set("id", cppfm::nbt::Value::makeString("minecraft:pig"));
    decoratedComponents.set("minecraft:entity_data", std::move(entityData));
    cppfm::nbt::Value lore = cppfm::nbt::Value::makeList(cppfm::nbt::String);
    lore.list.push_back(cppfm::nbt::Value::makeString(
        "{\"text\":\"First line\"}"));
    lore.list.push_back(cppfm::nbt::Value::makeString(
        "{\"text\":\"Second line\"}"));
    decoratedComponents.set("minecraft:lore", std::move(lore));
    decoratedComponents.set("minecraft:rarity",
        cppfm::nbt::Value::makeString("rare"));
    cppfm::nbt::Value unbreakable = cppfm::nbt::Value::makeCompound();
    unbreakable.set("show_in_tooltip", cppfm::nbt::Value::makeByte(0));
    decoratedComponents.set("minecraft:unbreakable", std::move(unbreakable));
    cppfm::nbt::Value modelData = cppfm::nbt::Value::makeCompound();
    cppfm::nbt::Value modelFloats = cppfm::nbt::Value::makeList(cppfm::nbt::Float);
    modelFloats.list.push_back(cppfm::nbt::Value::makeFloat(1.25f));
    modelData.set("floats", std::move(modelFloats));
    cppfm::nbt::Value modelFlags = cppfm::nbt::Value::makeList(cppfm::nbt::Byte);
    modelFlags.list.push_back(cppfm::nbt::Value::makeByte(1));
    modelData.set("flags", std::move(modelFlags));
    cppfm::nbt::Value modelStrings = cppfm::nbt::Value::makeList(cppfm::nbt::String);
    modelStrings.list.push_back(cppfm::nbt::Value::makeString("golden"));
    modelData.set("strings", std::move(modelStrings));
    cppfm::nbt::Value modelColors = cppfm::nbt::Value::makeList(cppfm::nbt::Int);
    modelColors.list.push_back(cppfm::nbt::Value::makeInt(0x123456));
    modelData.set("colors", std::move(modelColors));
    decoratedComponents.set("minecraft:custom_model_data", std::move(modelData));
    cppfm::nbt::Value dyed = cppfm::nbt::Value::makeCompound();
    dyed.set("rgb", cppfm::nbt::Value::makeInt(0xAABBCC));
    dyed.set("show_in_tooltip", cppfm::nbt::Value::makeByte(1));
    decoratedComponents.set("minecraft:dyed_color", std::move(dyed));
    cppfm::nbt::Value storedEnchantments = cppfm::nbt::Value::makeCompound();
    cppfm::nbt::Value storedLevels = cppfm::nbt::Value::makeCompound();
    storedLevels.set("minecraft:unbreaking", cppfm::nbt::Value::makeInt(3));
    storedEnchantments.set("levels", std::move(storedLevels));
    storedEnchantments.set("show_in_tooltip", cppfm::nbt::Value::makeByte(1));
    decoratedComponents.set("minecraft:stored_enchantments",
                            std::move(storedEnchantments));
    decoratedItem.set("components", std::move(decoratedComponents));

    bool commonComponentsRoundTrip = false;
    auto componentRoundTripItem = cppfm::ItemStack::fromNbt(decoratedItem);
    if (componentRoundTripItem) {
        cppfm::WriteBuffer wire;
        componentRoundTripItem->write(wire);
        cppfm::ReadBuffer input(wire.data);
        const auto fromWire = cppfm::ItemStack::read(input);
        const auto saved = fromWire.toNbt(0);
        const auto* savedComponents = saved.get("components");
        const auto* savedLore = savedComponents
            ? savedComponents->get("minecraft:lore") : nullptr;
        const auto* savedModelName = savedComponents
            ? savedComponents->get("minecraft:item_model") : nullptr;
        const auto* savedEntityData = savedComponents
            ? savedComponents->get("minecraft:entity_data") : nullptr;
        const auto* savedModel = savedComponents
            ? savedComponents->get("minecraft:custom_model_data") : nullptr;
        const auto* savedModelFloat = savedModel
            ? savedModel->get("floats") : nullptr;
        const auto* savedModelFlag = savedModel
            ? savedModel->get("flags") : nullptr;
        const auto* savedModelString = savedModel
            ? savedModel->get("strings") : nullptr;
        const auto* savedModelColor = savedModel
            ? savedModel->get("colors") : nullptr;
        const auto* savedDyed = savedComponents
            ? savedComponents->get("minecraft:dyed_color") : nullptr;
        const auto* savedUnbreakable = savedComponents
            ? savedComponents->get("minecraft:unbreakable") : nullptr;
        const auto* savedStored = savedComponents
            ? savedComponents->get("minecraft:stored_enchantments") : nullptr;
        const auto* savedStoredLevels = savedStored
            ? savedStored->get("levels") : nullptr;
        commonComponentsRoundTrip = input.remaining() == 0 && savedLore &&
            savedLore->tag == cppfm::nbt::List && savedLore->list.size() == 2 &&
            savedLore->list[1].str == "{\"text\":\"Second line\"}" &&
            savedModelName && savedModelName->tag == cppfm::nbt::String &&
            savedModelName->str == "minecraft:diamond" && savedEntityData &&
            savedEntityData->tag == cppfm::nbt::Compound &&
            savedEntityData->get("id") &&
            savedEntityData->get("id")->str == "minecraft:pig" &&
            savedModelFloat && savedModelFloat->list.size() == 1 &&
            savedModelFloat->list[0].f == 1.25f &&
            savedModelFlag && savedModelFlag->list[0].b == 1 &&
            savedModelString && savedModelString->list[0].str == "golden" &&
            savedModelColor && savedModelColor->list[0].i == 0x123456 &&
            savedDyed && savedDyed->get("rgb") &&
            savedDyed->get("rgb")->i == 0xAABBCC &&
            savedUnbreakable && savedUnbreakable->get("show_in_tooltip") &&
            savedUnbreakable->get("show_in_tooltip")->b == 0 &&
            savedStoredLevels && savedStoredLevels->get("minecraft:unbreaking") &&
            savedStoredLevels->get("minecraft:unbreaking")->i == 3;
    }
    check(commonComponentsRoundTrip,
          "item model, entity data, lore, names, rarity, unbreakable, custom model data, dyed color, and stored enchantments survive NBT/protocol round-trip");

    cppfm::nbt::Value editedBook = cppfm::nbt::Value::makeCompound();
    editedBook.set("id", cppfm::nbt::Value::makeString(
        "minecraft:enchanted_book"));
    editedBook.set("count", cppfm::nbt::Value::makeInt(1));
    cppfm::nbt::Value editedBookComponents =
        cppfm::nbt::Value::makeCompound();
    cppfm::nbt::Value originalBookEnchantments =
        cppfm::nbt::Value::makeCompound();
    cppfm::nbt::Value originalBookLevels =
        cppfm::nbt::Value::makeCompound();
    originalBookLevels.set("minecraft:mending",
                           cppfm::nbt::Value::makeInt(1));
    originalBookEnchantments.set("levels", std::move(originalBookLevels));
    originalBookEnchantments.set("show_in_tooltip",
                                 cppfm::nbt::Value::makeByte(1));
    editedBookComponents.set("minecraft:stored_enchantments",
                             std::move(originalBookEnchantments));
    cppfm::nbt::Value unrelatedBookData =
        cppfm::nbt::Value::makeCompound();
    unrelatedBookData.set("example:marker",
                          cppfm::nbt::Value::makeString("preserve"));
    editedBookComponents.set("example:opaque_component",
                             std::move(unrelatedBookData));
    editedBook.set("components", std::move(editedBookComponents));
    auto editableBook = cppfm::ItemStack::fromNbt(editedBook);
    bool storedEnchantmentsRemainStored = false;
    if (editableBook) {
        cppfm::ItemStack::addEnchant(
            *editableBook, "minecraft:unbreaking", 2);
        const auto saved = editableBook->toNbt(0);
        const auto* components = saved.get("components");
        const auto* stored = components
            ? components->get("minecraft:stored_enchantments") : nullptr;
        const auto* levels = stored ? stored->get("levels") : nullptr;
        const auto* mending = levels
            ? levels->get("minecraft:mending") : nullptr;
        const auto* unbreaking = levels
            ? levels->get("minecraft:unbreaking") : nullptr;
        const auto* ordinary = components
            ? components->get("minecraft:enchantments") : nullptr;
        const auto* opaque = components
            ? components->get("example:opaque_component") : nullptr;
        const auto reloaded = cppfm::ItemStack::fromNbt(saved);
        storedEnchantmentsRemainStored = stored && levels && mending &&
            mending->tag == cppfm::nbt::Int && mending->i == 1 &&
            unbreaking && unbreaking->tag == cppfm::nbt::Int &&
            unbreaking->i == 2 && !ordinary && opaque &&
            opaque->get("example:marker") && reloaded &&
            reloaded->hasEnchant("minecraft:mending") &&
            reloaded->enchantLevel("minecraft:unbreaking") == 2;
    }
    check(storedEnchantmentsRemainStored,
          "editing an enchanted book keeps stored_enchantments, removes no stale component, and round-trips merged levels");

    bool opaqueComponentsFailClosed = false;
    if (loaded) {
        try {
            cppfm::WriteBuffer opaqueWire;
            loaded->write(opaqueWire);
        } catch (const std::runtime_error&) {
            opaqueComponentsFailClosed = true;
        }
    }
    check(opaqueComponentsFailClosed,
          "opaque saved item components are retained but never silently omitted from a protocol stack");

    cppfm::nbt::Value opaqueRootItem = cppfm::nbt::Value::makeCompound();
    opaqueRootItem.set("id", cppfm::nbt::Value::makeString(
        "minecraft:diamond_pickaxe"));
    opaqueRootItem.set("count", cppfm::nbt::Value::makeInt(1));
    cppfm::nbt::Value legacyTag = cppfm::nbt::Value::makeCompound();
    legacyTag.set("example:plugin_data",
        cppfm::nbt::Value::makeString("preserve across unsupported wire data"));
    opaqueRootItem.set("tag", legacyTag);
    const auto opaqueRootStack = cppfm::ItemStack::fromNbt(opaqueRootItem);
    bool opaqueRootPreserved = false;
    bool opaqueRootWireRejected = false;
    if (opaqueRootStack) {
        const auto saved = opaqueRootStack->toNbt(0);
        const auto* savedTag = saved.get("tag");
        const auto* savedPluginData = savedTag
            ? savedTag->get("example:plugin_data") : nullptr;
        opaqueRootPreserved = opaqueRootStack->persistedNbt &&
            opaqueRootStack->persistedNbt->hasOpaqueComponents &&
            !opaqueRootStack->canWriteToProtocol() && savedPluginData &&
            savedPluginData->tag == cppfm::nbt::String &&
            savedPluginData->str == "preserve across unsupported wire data";
        try {
            cppfm::WriteBuffer wire;
            opaqueRootStack->write(wire);
        } catch (const std::runtime_error&) {
            opaqueRootWireRejected = true;
        }
    }
    check(opaqueRootPreserved && opaqueRootWireRejected,
          "unsupported root-level item NBT remains persisted and fails closed before protocol serialization");

    cppfm::nbt::Value savedShulkerItems =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value savedShulkerDiamond =
        cppfm::nbt::Value::makeCompound();
    savedShulkerDiamond.set("id",
        cppfm::nbt::Value::makeString("minecraft:diamond"));
    savedShulkerDiamond.set("Count", cppfm::nbt::Value::makeByte(1));
    savedShulkerDiamond.set("Slot", cppfm::nbt::Value::makeByte(4));
    savedShulkerItems.list.push_back(std::move(savedShulkerDiamond));

    cppfm::nbt::Value savedShulkerEntity =
        cppfm::nbt::Value::makeCompound();
    savedShulkerEntity.set("id", cppfm::nbt::Value::makeString(
        "minecraft:shulker_box"));
    savedShulkerEntity.set("x", cppfm::nbt::Value::makeInt(4));
    savedShulkerEntity.set("y", cppfm::nbt::Value::makeInt(64));
    savedShulkerEntity.set("z", cppfm::nbt::Value::makeInt(8));
    savedShulkerEntity.set("keepPacked", cppfm::nbt::Value::makeByte(0));
    savedShulkerEntity.set("CustomName", cppfm::nbt::Value::makeString(
        "{\"text\":\"Archive box\"}"));
    savedShulkerEntity.set("Items", savedShulkerItems);
    const auto savedRedShulker = cppfm::makeShulkerBoxDropItem(
        "minecraft:red_shulker_box", savedShulkerEntity);
    const auto savedDefaultShulker = cppfm::makeShulkerBoxDropItem(
        "minecraft:shulker_box", savedShulkerEntity);
    const auto decodedShulker = savedRedShulker
        ? cppfm::shulkerBoxContentsFromItem(*savedRedShulker)
        : std::nullopt;
    const auto expectedShulkerName =
        cppfm::item_component_nbt::protocolComponentFromNbt(
            "minecraft:custom_name", *savedShulkerEntity.get("CustomName"));
    bool customNamePreserved = false;
    if (savedRedShulker && expectedShulkerName) {
        for (const auto& [componentId, payload] : savedRedShulker->components) {
            if (componentId == expectedShulkerName->id &&
                payload == expectedShulkerName->payload)
                customNamePreserved = true;
        }
    }
    check(savedRedShulker && savedDefaultShulker &&
              savedRedShulker->canWriteToProtocol() &&
              savedDefaultShulker->canWriteToProtocol() &&
              decodedShulker &&
              (*decodedShulker)[4].itemId ==
                  cppfm::gen::itemIdByName().at("minecraft:diamond") &&
              (*decodedShulker)[4].count == 1 && customNamePreserved,
          "filled colored and default shulkers retain contents and encode CustomName as minecraft:custom_name");
    const auto shulkerPlacementData = savedRedShulker
        ? cppfm::shulkerBoxDataFromItem(*savedRedShulker)
        : std::nullopt;
    cppfm::BlockEntityStore shulkerPlacementStore;
    bool shulkerWasAbsentDuringDirtyAdmission = false;
    shulkerPlacementStore.setDirtyCallback(
        [&shulkerPlacementStore, &shulkerWasAbsentDuringDirtyAdmission](
            std::int32_t, std::int32_t) {
            shulkerWasAbsentDuringDirtyAdmission =
                !shulkerPlacementStore.containsAt(4, 64, 8);
        });
    const bool shulkerPlacementSaved = shulkerPlacementData &&
        shulkerPlacementStore.replaceNbtAt(
            4, 64, 8, cppfm::shulkerBoxBlockEntityNbt(
                        *shulkerPlacementData, 4, 64, 8));
    cppfm::nbt::Value placedShulkerEntities =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    const bool placedShulkerSerialized = shulkerPlacementSaved &&
        shulkerPlacementStore.writeChunkNbt(
            0, 0, placedShulkerEntities);
    const auto* placedName = placedShulkerSerialized &&
            !placedShulkerEntities.list.empty()
        ? placedShulkerEntities.list.front().get("CustomName") : nullptr;
    const auto* placedItems = placedShulkerSerialized &&
            !placedShulkerEntities.list.empty()
        ? placedShulkerEntities.list.front().get("Items") : nullptr;
    const auto* placedDiamond = placedItems &&
            placedItems->tag == cppfm::nbt::List &&
            !placedItems->list.empty()
        ? placedItems->list.front().get("id") : nullptr;
    check(placedName && placedName->tag == cppfm::nbt::String &&
              placedName->str == "{\"text\":\"Archive box\"}" &&
              placedDiamond && placedDiamond->tag == cppfm::nbt::String &&
              placedDiamond->str == "minecraft:diamond" &&
              shulkerWasAbsentDuringDirtyAdmission,
          "shulker contents are admitted for dirty persistence before becoming visible");
    cppfm::nbt::Value namedEmptyShulker = savedShulkerEntity;
    namedEmptyShulker.get("Items")->list.clear();
    const auto namedEmptyDrop = cppfm::makeShulkerBoxDropItem(
        "minecraft:red_shulker_box", namedEmptyShulker);
    bool emptyDropHasName = false;
    bool emptyDropHasNoContainer = true;
    if (namedEmptyDrop) {
        const auto containerId = cppfm::item_component_nbt::componentId(
            "minecraft:container");
        for (const auto& [componentId, payload] : namedEmptyDrop->components) {
            if (expectedShulkerName &&
                componentId == expectedShulkerName->id &&
                payload == expectedShulkerName->payload)
                emptyDropHasName = true;
            if (containerId && componentId == *containerId)
                emptyDropHasNoContainer = false;
        }
    }
    check(namedEmptyDrop && emptyDropHasName && emptyDropHasNoContainer,
          "empty named shulker drops retain CustomName without adding an empty container component");
    cppfm::nbt::Value pendingLootShulker = namedEmptyShulker;
    pendingLootShulker.set("Lock",
        cppfm::nbt::Value::makeString("keyed"));
    pendingLootShulker.set("LootTable",
        cppfm::nbt::Value::makeString("minecraft:chests/simple_dungeon"));
    pendingLootShulker.set("LootTableSeed",
        cppfm::nbt::Value::makeLong(123456));
    const auto pendingLootDrop = cppfm::makeShulkerBoxDropItem(
        "minecraft:red_shulker_box", pendingLootShulker);
    auto pendingLootComponentNbt = cppfm::nbt::Value::makeCompound();
    pendingLootComponentNbt.set("loot_table",
        cppfm::nbt::Value::makeString("minecraft:chests/simple_dungeon"));
    pendingLootComponentNbt.set("seed",
        cppfm::nbt::Value::makeLong(123456));
    const auto expectedPendingLootComponent =
        cppfm::item_component_nbt::protocolComponentFromNbt(
            "minecraft:container_loot", pendingLootComponentNbt);
    bool pendingLootComponentPreserved = false;
    if (pendingLootDrop && expectedPendingLootComponent) {
        for (const auto& [componentId, payload] :
             pendingLootDrop->components) {
            if (componentId == expectedPendingLootComponent->id &&
                payload == expectedPendingLootComponent->payload)
                pendingLootComponentPreserved = true;
        }
    }
    const auto pendingLootPlacement = pendingLootDrop
        ? cppfm::shulkerBoxDataFromItem(*pendingLootDrop)
        : std::nullopt;
    const auto pendingLootBlockNbt = pendingLootPlacement
        ? cppfm::shulkerBoxBlockEntityNbt(*pendingLootPlacement, 4, 64, 8)
        : cppfm::nbt::Value{};
    const auto* restoredLootTable = pendingLootBlockNbt.get("LootTable");
    const auto* restoredLootSeed = pendingLootBlockNbt.get("LootTableSeed");
    check(pendingLootDrop && pendingLootDrop->canWriteToProtocol() &&
              pendingLootComponentPreserved && restoredLootTable &&
              restoredLootTable->tag == cppfm::nbt::String &&
              restoredLootTable->str == "minecraft:chests/simple_dungeon" &&
              restoredLootSeed && restoredLootSeed->tag == cppfm::nbt::Long &&
              restoredLootSeed->l == 123456,
          "pending shulker loot table and seed survive block-item protocol round trip; vanilla Lock does not block breaking");

    constexpr std::int32_t pendingChestX = 16;
    constexpr std::int32_t pendingChestY = 64;
    constexpr std::int32_t pendingChestZ = 16;
    constexpr std::int64_t pendingChestSeed = 8675309;
    constexpr const char* pendingChestTable =
        "minecraft:chests/simple_dungeon";
    cppfm::LootTableEvaluator pendingLootEvaluator;
    pendingLootEvaluator.loadDirectory("assets/data/loot_tables");
    const auto expectedPendingChestLoot =
        pendingLootEvaluator.evaluateRegisteredTable(
            pendingChestTable, {}, nullptr, pendingChestSeed);
    cppfm::BlockEntityStore pendingChestStore;
    auto pendingChestNbt = cppfm::nbt::Value::makeCompound();
    pendingChestNbt.set("id",
        cppfm::nbt::Value::makeString("minecraft:chest"));
    pendingChestNbt.set("x", cppfm::nbt::Value::makeInt(pendingChestX));
    pendingChestNbt.set("y", cppfm::nbt::Value::makeInt(pendingChestY));
    pendingChestNbt.set("z", cppfm::nbt::Value::makeInt(pendingChestZ));
    pendingChestNbt.set("LootTable",
        cppfm::nbt::Value::makeString(pendingChestTable));
    pendingChestNbt.set("LootTableSeed",
        cppfm::nbt::Value::makeLong(pendingChestSeed));
    pendingChestNbt.set("Items",
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound));
    const auto matchesExpectedPendingLoot = [&](const auto& plan) {
        bool matches = expectedPendingChestLoot &&
            plan.status == cppfm::PendingLootDropPlanStatus::Ready &&
            plan.items.size() == expectedPendingChestLoot->size();
        if (matches) {
            for (std::size_t i = 0; i < plan.items.size(); ++i) {
                if (plan.items[i].itemId !=
                        (*expectedPendingChestLoot)[i].itemId ||
                    plan.items[i].count !=
                        (*expectedPendingChestLoot)[i].count) {
                    matches = false;
                    break;
                }
            }
        }
        return matches;
    };
    const auto pendingChestDropPlan = cppfm::planPendingContainerLootDrops(
        pendingChestNbt, pendingLootEvaluator);
    auto pendingBarrelNbt = pendingChestNbt;
    pendingBarrelNbt.set("id", cppfm::nbt::Value::makeString(
        "minecraft:barrel"));
    const auto pendingBarrelDropPlan = cppfm::planPendingContainerLootDrops(
        pendingBarrelNbt, pendingLootEvaluator);
    const bool pendingChestInstalled = pendingChestStore.replaceNbtAt(
        pendingChestX, pendingChestY, pendingChestZ,
        std::move(pendingChestNbt));
    check(matchesExpectedPendingLoot(pendingChestDropPlan),
          "destroy-drop planning evaluates an unopened chest's exact seeded loot without mutating its NBT");
    check(matchesExpectedPendingLoot(pendingBarrelDropPlan),
          "destroy-drop planning accepts and evaluates an unopened barrel's seeded loot");
    const auto materialization = pendingChestInstalled
        ? cppfm::materializePendingLootAt(
              pendingChestStore, pendingLootEvaluator, pendingChestX,
              pendingChestY, pendingChestZ, 27)
        : cppfm::PendingLootMaterialization::Failed;
    const auto materializedChest = cppfm::serializedBlockEntityAt(
        pendingChestStore, pendingChestX, pendingChestY, pendingChestZ);
    const auto* materializedItems = materializedChest
        ? materializedChest->get("Items") : nullptr;
    bool materializedLootMatchesSeed = expectedPendingChestLoot &&
        materializedItems && materializedItems->tag == cppfm::nbt::List &&
        materializedItems->list.size() == expectedPendingChestLoot->size();
    if (materializedLootMatchesSeed) {
        for (std::size_t i = 0; i < expectedPendingChestLoot->size(); ++i) {
            const auto actual = cppfm::ItemStack::fromNbt(
                materializedItems->list[i]);
            if (!actual || actual->itemId !=
                               (*expectedPendingChestLoot)[i].itemId ||
                actual->count != (*expectedPendingChestLoot)[i].count) {
                materializedLootMatchesSeed = false;
                break;
            }
        }
    }
    check(materialization == cppfm::PendingLootMaterialization::Materialized &&
              materializedChest && !materializedChest->get("LootTable") &&
              !materializedChest->get("LootTableSeed") &&
              materializedLootMatchesSeed &&
              cppfm::materializePendingLootAt(
                  pendingChestStore, pendingLootEvaluator, pendingChestX,
                  pendingChestY, pendingChestZ, 27) ==
                  cppfm::PendingLootMaterialization::NoPendingLoot,
          "opening a lootable container materializes its seeded contents once and clears the pending table");
    cppfm::BlockEntityStore unsupportedLootStore;
    auto unsupportedLootNbt = cppfm::nbt::Value::makeCompound();
    unsupportedLootNbt.set("id",
        cppfm::nbt::Value::makeString("minecraft:chest"));
    unsupportedLootNbt.set("x", cppfm::nbt::Value::makeInt(17));
    unsupportedLootNbt.set("y", cppfm::nbt::Value::makeInt(64));
    unsupportedLootNbt.set("z", cppfm::nbt::Value::makeInt(16));
    unsupportedLootNbt.set("LootTable",
        cppfm::nbt::Value::makeString("modded:unknown/table"));
    unsupportedLootNbt.set("Items",
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound));
    const auto unsupportedLootDropPlan =
        cppfm::planPendingContainerLootDrops(
            unsupportedLootNbt, pendingLootEvaluator);
    auto unsupportedBarrelLootNbt = unsupportedLootNbt;
    unsupportedBarrelLootNbt.set("id", cppfm::nbt::Value::makeString(
        "minecraft:barrel"));
    const auto unsupportedBarrelLootDropPlan =
        cppfm::planPendingContainerLootDrops(
            unsupportedBarrelLootNbt, pendingLootEvaluator);
    const bool unsupportedLootInstalled = unsupportedLootStore.replaceNbtAt(
        17, 64, 16, std::move(unsupportedLootNbt));
    const auto unsupportedLootResult = unsupportedLootInstalled
        ? cppfm::materializePendingLootAt(
              unsupportedLootStore, pendingLootEvaluator, 17, 64, 16, 27)
        : cppfm::PendingLootMaterialization::Failed;
    const auto retainedUnsupportedLoot = cppfm::serializedBlockEntityAt(
        unsupportedLootStore, 17, 64, 16);
    const auto* retainedTable = retainedUnsupportedLoot
        ? retainedUnsupportedLoot->get("LootTable") : nullptr;
    check(unsupportedLootResult ==
              cppfm::PendingLootMaterialization::UnsupportedTable &&
              unsupportedLootDropPlan.status ==
                  cppfm::PendingLootDropPlanStatus::UnsupportedTable &&
              unsupportedBarrelLootDropPlan.status ==
                  cppfm::PendingLootDropPlanStatus::UnsupportedTable &&
              retainedTable && retainedTable->tag == cppfm::nbt::String &&
              retainedTable->str == "modded:unknown/table",
          "unsupported pending loot fails closed and preserves its original table metadata");

    cppfm::BlockEntityStore deferredLootStore;
    constexpr std::int32_t deferredLootX = 33;
    constexpr std::int32_t deferredLootY = 64;
    constexpr std::int32_t deferredLootZ = 32;
    constexpr const char* deferredLootTable = "modded:chests/unknown";
    const bool deferredLootPersisted =
        cppfm::persistPendingContainerLootAt(
            deferredLootStore, deferredLootX, deferredLootY, deferredLootZ,
            deferredLootTable);
    auto deferredLootEntities =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    const bool deferredLootSerialized = deferredLootPersisted &&
        deferredLootStore.writeChunkNbt(
            deferredLootX >> 4, deferredLootZ >> 4,
            deferredLootEntities);
    cppfm::nbt::Value deferredLootRoot =
        cppfm::nbt::Value::makeCompound();
    if (deferredLootSerialized)
        deferredLootRoot.set("block_entities",
                             std::move(deferredLootEntities));
    cppfm::BlockEntityStore restartedDeferredLootStore;
    if (deferredLootSerialized)
        restartedDeferredLootStore.readChunkNbt(deferredLootRoot);
    const auto restartedDeferredLoot = deferredLootSerialized
        ? cppfm::serializedBlockEntityAt(
              restartedDeferredLootStore, deferredLootX, deferredLootY,
              deferredLootZ)
        : std::nullopt;
    const auto* restartedDeferredTable = restartedDeferredLoot
        ? restartedDeferredLoot->get("LootTable") : nullptr;
    const auto* restartedDeferredItems = restartedDeferredLoot
        ? restartedDeferredLoot->get("Items") : nullptr;
    check(deferredLootSerialized && restartedDeferredTable &&
              restartedDeferredTable->tag == cppfm::nbt::String &&
              restartedDeferredTable->str == deferredLootTable &&
              restartedDeferredItems &&
              restartedDeferredItems->tag == cppfm::nbt::List &&
              restartedDeferredItems->list.empty() &&
              cppfm::materializePendingLootAt(
                  restartedDeferredLootStore, pendingLootEvaluator,
                  deferredLootX, deferredLootY, deferredLootZ, 27) ==
                  cppfm::PendingLootMaterialization::UnsupportedTable,
          "deferred unsupported structure loot survives a chunk save/load boundary");

    cppfm::BlockEntityStore occupiedDeferredLootStore;
    auto occupiedDeferredChest = cppfm::nbt::Value::makeCompound();
    occupiedDeferredChest.set("id", cppfm::nbt::Value::makeString(
        "minecraft:chest"));
    occupiedDeferredChest.set("x", cppfm::nbt::Value::makeInt(35));
    occupiedDeferredChest.set("y", cppfm::nbt::Value::makeInt(64));
    occupiedDeferredChest.set("z", cppfm::nbt::Value::makeInt(32));
    auto occupiedChestItems =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    auto protectedDiamond = cppfm::nbt::Value::makeCompound();
    protectedDiamond.set("Slot", cppfm::nbt::Value::makeByte(0));
    protectedDiamond.set("id", cppfm::nbt::Value::makeString(
        "minecraft:diamond"));
    protectedDiamond.set("count", cppfm::nbt::Value::makeByte(1));
    occupiedChestItems.list.push_back(std::move(protectedDiamond));
    occupiedDeferredChest.set("Items", std::move(occupiedChestItems));
    const bool occupiedChestInstalled = occupiedDeferredLootStore.replaceNbtAt(
        35, 64, 32, std::move(occupiedDeferredChest));
    const bool occupiedChestRejectedLoot = occupiedChestInstalled &&
        !cppfm::persistPendingContainerLootAt(
            occupiedDeferredLootStore, 35, 64, 32, deferredLootTable);
    const auto occupiedChestAfter = occupiedChestRejectedLoot
        ? cppfm::serializedBlockEntityAt(
              occupiedDeferredLootStore, 35, 64, 32)
        : std::nullopt;
    const auto* occupiedChestLoot = occupiedChestAfter
        ? occupiedChestAfter->get("LootTable") : nullptr;
    const auto* occupiedChestItemsAfter = occupiedChestAfter
        ? occupiedChestAfter->get("Items") : nullptr;
    check(occupiedChestRejectedLoot && !occupiedChestLoot &&
              occupiedChestItemsAfter &&
              occupiedChestItemsAfter->tag == cppfm::nbt::List &&
              occupiedChestItemsAfter->list.size() == 1 &&
              occupiedChestItemsAfter->list.front().get("id") &&
              occupiedChestItemsAfter->list.front().get("id")->str ==
                  "minecraft:diamond",
          "pending structure loot refuses to overwrite an occupied chest");

    const auto nestedLootRoot = std::filesystem::temp_directory_path() /
        ("cppfm-nested-loot-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code nestedLootError;
    const bool nestedLootRootCreated =
        std::filesystem::create_directory(nestedLootRoot, nestedLootError);
    bool nestedFixturesReady = nestedLootRootCreated;
    const auto configuredRecipesRoot = nestedLootRoot / "configured-recipes";
    if (nestedFixturesReady) {
        std::filesystem::create_directories(
            configuredRecipesRoot, nestedLootError);
        nestedFixturesReady = !nestedLootError;
    }
    if (nestedFixturesReady) {
        std::ofstream configuredRecipe(
            configuredRecipesRoot / "probe_recipe.json",
            std::ios::binary | std::ios::trunc);
        configuredRecipe << R"({"type":"minecraft:crafting_shapeless","ingredients":[{"item":"minecraft:stone"}],"result":{"id":"minecraft:gold_nugget","count":1}})";
        nestedFixturesReady = static_cast<bool>(configuredRecipe);
    }
    const auto minecraftLootRoot =
        nestedLootRoot / "data" / "minecraft" / "loot_table";
    const auto exampleLootRoot =
        nestedLootRoot / "data" / "example" / "loot_table";
    const auto vanillaPriorityTagRoot =
        nestedLootRoot / "data" / "example" / "tags" / "item";
    if (nestedFixturesReady) {
        std::filesystem::create_directories(
            minecraftLootRoot / "chests", nestedLootError);
        nestedFixturesReady = !nestedLootError;
    }
    if (nestedFixturesReady) {
        std::ofstream rootTable(
            minecraftLootRoot / "chests" / "root.json",
            std::ios::binary | std::ios::trunc);
        rootTable << R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:loot_table","name":"minecraft:chests/nested"}]}]})";
        nestedFixturesReady = static_cast<bool>(rootTable);
    }
    if (nestedFixturesReady) {
        std::ofstream nestedTable(
            minecraftLootRoot / "chests" / "nested.json",
            std::ios::binary | std::ios::trunc);
        nestedTable << R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})";
        nestedFixturesReady = static_cast<bool>(nestedTable);
    }
    if (nestedFixturesReady) {
        std::filesystem::create_directories(
            minecraftLootRoot / "blocks", nestedLootError);
        nestedFixturesReady = !nestedLootError;
    }
    if (nestedFixturesReady) {
        std::ofstream invalidStoneTable(
            minecraftLootRoot / "blocks" / "stone.json",
            std::ios::binary | std::ios::trunc);
        invalidStoneTable << R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:not_a_real_item"}]}]})";
        nestedFixturesReady = !nestedLootError &&
                              static_cast<bool>(invalidStoneTable);
    }
    if (nestedFixturesReady) {
        std::ofstream compositeTable(
            minecraftLootRoot / "blocks" / "composite.json",
            std::ios::binary | std::ios::trunc);
        compositeTable << R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:alternatives","children":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]}]})";
        nestedFixturesReady = static_cast<bool>(compositeTable);
    }
    if (nestedFixturesReady) {
        std::filesystem::create_directories(
            exampleLootRoot / "blocks", nestedLootError);
        std::filesystem::create_directories(
            vanillaPriorityTagRoot, nestedLootError);
        nestedFixturesReady = !nestedLootError;
    }
    if (nestedFixturesReady) {
        std::ofstream vanillaPriorityTable(
            exampleLootRoot / "blocks" / "priority_probe.json",
            std::ios::binary | std::ios::trunc);
        vanillaPriorityTable << R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:iron_ingot"}]}]})";
        std::ofstream vanillaPriorityTag(
            vanillaPriorityTagRoot / "priority_probe.json",
            std::ios::binary | std::ios::trunc);
        vanillaPriorityTag << R"({"replace":true,"values":["minecraft:iron_ingot"]})";
        nestedFixturesReady = static_cast<bool>(vanillaPriorityTable) &&
                              static_cast<bool>(vanillaPriorityTag);
    }
    if (nestedFixturesReady) {
        std::ofstream customNamespaceTable(
            exampleLootRoot / "blocks" / "custom.json",
            std::ios::binary | std::ios::trunc);
        customNamespaceTable << R"({"pools":[{"rolls":{"type":"minecraft:uniform","min":1,"max":1},"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})";
        nestedFixturesReady = static_cast<bool>(customNamespaceTable);
    }
    if (nestedFixturesReady) {
        std::ofstream unsupportedRollsTable(
            exampleLootRoot / "blocks" / "binomial_provider.json",
            std::ios::binary | std::ios::trunc);
        unsupportedRollsTable << R"({"pools":[{"rolls":{"type":"minecraft:binomial","n":3,"p":0.5},"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})";
        nestedFixturesReady = static_cast<bool>(unsupportedRollsTable);
    }
    const auto writeLootFixture = [&](const char* name,
                                      const char* contents) {
        if (!nestedFixturesReady) return false;
        std::ofstream fixture(exampleLootRoot / "blocks" / name,
                              std::ios::binary | std::ios::trunc);
        fixture << contents;
        return static_cast<bool>(fixture);
    };
    nestedFixturesReady = writeLootFixture(
        "zero_binomial_count.json",
        R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond","functions":[{"function":"minecraft:set_count","count":{"type":"minecraft:binomial","n":1,"p":0}}]}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "uniform_bonus.json",
        R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:redstone","functions":[{"function":"minecraft:apply_bonus","enchantment":"minecraft:fortune","formula":"minecraft:uniform_bonus_count","parameters":{"bonusMultiplier":2}}]}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "binomial_bonus.json",
        R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond","functions":[{"function":"minecraft:apply_bonus","enchantment":"minecraft:fortune","formula":"minecraft:binomial_with_bonus_count","parameters":{"extra":2,"probability":0.5}}]}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "set_count_add.json",
        R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond","functions":[{"function":"minecraft:set_count","count":2,"add":true}]}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "function_order.json",
        R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:redstone","functions":[{"function":"minecraft:apply_bonus","enchantment":"minecraft:fortune","formula":"minecraft:uniform_bonus_count","parameters":{"bonusMultiplier":2}},{"function":"minecraft:set_count","count":1}]}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "nested_uniform_count.json",
        R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond","functions":[{"function":"minecraft:set_count","count":{"type":"minecraft:uniform","min":1,"max":{"type":"minecraft:constant","value":4}}}]}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "random_chance_provider.json",
        R"({"pools":[{"rolls":1,"conditions":[{"condition":"minecraft:random_chance","chance":{"type":"minecraft:constant","value":1}}],"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "any_of_condition.json",
        R"({"pools":[{"rolls":1,"conditions":[{"condition":"minecraft:any_of","terms":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"on_fire":true}},{"condition":"minecraft:random_chance","chance":1}]}],"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "context_leaf.json",
        R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond","conditions":[{"condition":"minecraft:entity_properties","entity":"this","predicate":{"on_fire":true}}]}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "context_nested.json",
        R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:loot_table","name":"example:blocks/context_leaf"}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "player_kill_context.json",
        R"({"pools":[{"rolls":1,"conditions":[{"condition":"minecraft:killed_by_player"}],"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "six_stacks.json",
        R"({"pools":[{"rolls":6,"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "enchanted_count_increase.json",
        R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:rotten_flesh","functions":[{"function":"minecraft:set_count","count":{"type":"minecraft:uniform","min":0,"max":2},"add":false},{"function":"minecraft:enchanted_count_increase","enchantment":"minecraft:looting","count":{"type":"minecraft:uniform","min":0,"max":1}}]}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "enchanted_chance.json",
        R"({"pools":[{"rolls":1,"conditions":[{"condition":"minecraft:killed_by_player"},{"condition":"minecraft:random_chance_with_enchanted_bonus","enchantment":"minecraft:looting","unenchanted_chance":0,"enchanted_chance":{"type":"minecraft:linear","base":1,"per_level_above_first":0}}],"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "enchanted_fortune_chance.json",
        R"({"pools":[{"rolls":1,"conditions":[{"condition":"minecraft:random_chance_with_enchanted_bonus","enchantment":"minecraft:fortune","unenchanted_chance":0,"enchanted_chance":{"type":"minecraft:linear","base":1,"per_level_above_first":0}}],"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "looting_condition.json",
        R"({"conditions":[{"condition":"minecraft:random_chance_with_looting","chance":0,"looting_multiplier":0.5}],"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "unsupported_enchant_levels.json",
        R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond","functions":[{"function":"minecraft:enchant_with_levels","levels":30}]}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "zero_uniform_rolls.json",
        R"({"pools":[{"rolls":{"min":0,"max":0},"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "omitted_rolls.json",
        R"({"pools":[{"entries":[{"type":"minecraft:item","name":"minecraft:emerald"}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "bonus_rolls.json",
        R"({"pools":[{"rolls":1,"bonus_rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "survives_explosion.json",
        R"({"pools":[{"entries":[{"type":"minecraft:item","name":"minecraft:diamond","conditions":[{"condition":"minecraft:survives_explosion"}]}]}]})");
    nestedFixturesReady = nestedFixturesReady && writeLootFixture(
        "explosion_decay.json",
        R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond","functions":[{"function":"minecraft:set_count","count":64},{"function":"minecraft:explosion_decay"}]}]}]})");
    if (nestedFixturesReady) {
        std::ofstream malformedTable(
            exampleLootRoot / "blocks" / "malformed.json",
            std::ios::binary | std::ios::trunc);
        malformedTable << R"({"pools":[)";
        nestedFixturesReady = static_cast<bool>(malformedTable);
    }
    cppfm::LootTableEvaluator nestedLootEvaluator;
    if (nestedFixturesReady)
        nestedLootEvaluator.loadDirectory(
            (nestedLootRoot / "data").string());
    const auto worldDatapacksRoot = nestedLootRoot / "world-datapacks";
    const auto writeOverlayPack = [&](const char* packName,
                                      const char* itemName) {
        if (!nestedFixturesReady) return false;
        const auto packRoot = worldDatapacksRoot / packName;
        const auto tableRoot = packRoot / "data" / "example" /
                               "loot_table" / "blocks";
        const auto itemTagRoot = packRoot / "data" / "example" /
                                 "tags" / "item";
        const auto vanillaItemTagRoot = packRoot / "data" / "minecraft" /
                                        "tags" / "item";
        const auto blockTagRoot = packRoot / "data" / "example" /
                                  "tags" / "block";
        const auto biomeTagRoot = packRoot / "data" / "example" /
                                  "tags" / "worldgen" / "biome";
        std::filesystem::create_directories(tableRoot, nestedLootError);
        std::filesystem::create_directories(itemTagRoot, nestedLootError);
        std::filesystem::create_directories(vanillaItemTagRoot, nestedLootError);
        std::filesystem::create_directories(blockTagRoot, nestedLootError);
        std::filesystem::create_directories(biomeTagRoot, nestedLootError);
        if (nestedLootError) return false;
        std::ofstream metadata(packRoot / "pack.mcmeta",
                               std::ios::binary | std::ios::trunc);
        metadata << R"({"pack":{"pack_format":61,"description":"loot precedence fixture"}})";
        std::ofstream table(tableRoot / "ordered.json",
                            std::ios::binary | std::ios::trunc);
        table << "{\"pools\":[{\"rolls\":1,\"entries\":[{\"type\":\"minecraft:item\",\"name\":\""
               << itemName << "\"}]}]}";
        std::ofstream priorityTable(tableRoot / "priority_probe.json",
                                    std::ios::binary | std::ios::trunc);
        priorityTable << "{\"pools\":[{\"rolls\":1,\"entries\":[{\"type\":\"minecraft:item\",\"name\":\""
                       << itemName << "\"}]}]}";
        std::ofstream overlayTag(itemTagRoot / "overlay.json",
                                 std::ios::binary | std::ios::trunc);
        std::ofstream priorityTag(itemTagRoot / "priority_probe.json",
                                  std::ios::binary | std::ios::trunc);
        priorityTag << "{\"replace\":true,\"values\":[\""
                    << itemName << "\"]}";
        std::ofstream planksTag(vanillaItemTagRoot / "planks.json",
                                std::ios::binary | std::ios::trunc);
        if (std::string(packName) == "z-last") {
            overlayTag << R"({"replace":true,"values":["minecraft:gold_nugget"]})";
            planksTag << R"({"replace":true,"values":["minecraft:gold_nugget"]})";
        } else {
            overlayTag << R"({"values":["minecraft:diamond","minecraft:iron_ingot"]})";
            planksTag << R"({"values":["minecraft:diamond","minecraft:iron_ingot"]})";
        }
        std::ofstream aliasTag(itemTagRoot / "overlay_alias.json",
                               std::ios::binary | std::ios::trunc);
        aliasTag << R"({"values":["#example:overlay"]})";
        std::ofstream blockTag(blockTagRoot / "soft.json",
                               std::ios::binary | std::ios::trunc);
        blockTag << R"({"values":["minecraft:dirt"]})";
        std::ofstream biomeTag(biomeTagRoot / "warm.json",
                               std::ios::binary | std::ios::trunc);
        biomeTag << R"({"values":["minecraft:desert"]})";
        return static_cast<bool>(metadata) && static_cast<bool>(table) &&
               static_cast<bool>(priorityTable) &&
               static_cast<bool>(overlayTag) && static_cast<bool>(planksTag) &&
               static_cast<bool>(priorityTag) && static_cast<bool>(aliasTag) &&
               static_cast<bool>(blockTag) && static_cast<bool>(biomeTag);
    };
    nestedFixturesReady = writeOverlayPack("z-last", "minecraft:emerald") &&
                          writeOverlayPack("a-first", "minecraft:diamond");
    cppfm::RecipeManager nestedRecipes;
    nestedRecipes.loadDefaults();
    nestedRecipes.loadDirectory(configuredRecipesRoot.string());
    const bool configuredRecipeLoaded =
        nestedRecipes.findById("minecraft:probe_recipe") != nullptr;
    cppfm::DatapackManager nestedDatapacks;
    if (nestedFixturesReady)
        nestedDatapacks.loadAll(
            nestedRecipes, (nestedLootRoot / "data").string(),
            worldDatapacksRoot.string());
    const bool configuredRecipeSurvivesDatapackLoad =
        nestedRecipes.findById("minecraft:probe_recipe") != nullptr;

    const auto transactionalPacksRoot =
        nestedLootRoot / "transactional-datapacks";
    const auto transactionalGoodTagDir = transactionalPacksRoot / "good" /
        "data" / "example" / "tags" / "item";
    std::filesystem::create_directories(transactionalGoodTagDir,
                                        nestedLootError);
    const bool transactionalDirsCreated = !nestedLootError;
    if (transactionalDirsCreated) {
        std::ofstream validTag(transactionalGoodTagDir / "valid.json",
                               std::ios::binary | std::ios::trunc);
        validTag << R"({"values":["minecraft:diamond"]})";
        std::ofstream airTag(transactionalGoodTagDir / "air.json",
                             std::ios::binary | std::ios::trunc);
        airTag << R"({"values":["minecraft:air"]})";
        std::ofstream optionalTag(transactionalGoodTagDir / "optional.json",
                                  std::ios::binary | std::ios::trunc);
        optionalTag << R"({"values":[{"id":"#example:missing","required":false},{"id":"minecraft:not_a_real_item","required":false}]})";
    }
    cppfm::RecipeManager transactionalRecipes;
    transactionalRecipes.loadDefaults();
    cppfm::DatapackManager transactionalDatapacks;
    bool transactionalInitialLoad = false;
    try {
        if (transactionalDirsCreated) {
            transactionalDatapacks.loadAll(
                transactionalRecipes, (nestedLootRoot / "empty-assets").string(),
                transactionalPacksRoot.string());
            transactionalInitialLoad = true;
        }
    } catch (...) {
        transactionalInitialLoad = false;
    }
    const bool optionalTagEntriesAreAccepted = transactionalInitialLoad &&
        transactionalDatapacks.tagManager.itemTags.contains(
            "example:optional");
    const auto transactionalBefore =
        transactionalDatapacks.tagManager.itemTags.find("example:valid");
    const auto transactionalDiamond =
        cppfm::gen::itemIdByName().at("minecraft:diamond");
    const bool transactionalInitialStateIsValid =
        transactionalBefore != transactionalDatapacks.tagManager.itemTags.end() &&
        transactionalBefore->second ==
            std::unordered_set<std::uint32_t>{transactionalDiamond};
    const auto transactionalAirTag =
        transactionalDatapacks.tagManager.itemTags.find("example:air");
    const bool requiredAirTagResolvesToZero =
        transactionalAirTag !=
            transactionalDatapacks.tagManager.itemTags.end() &&
        transactionalAirTag->second == std::unordered_set<std::uint32_t>{0};
    const auto invalidPackTagDir = transactionalPacksRoot / "invalid" /
        "data" / "example" / "tags" / "item";
    std::filesystem::create_directories(invalidPackTagDir, nestedLootError);
    bool invalidPackFixtureReady = !nestedLootError;
    if (invalidPackFixtureReady) {
        std::ofstream invalidTag(invalidPackTagDir / "required.json",
                                 std::ios::binary | std::ios::trunc);
        invalidTag << R"({"values":["minecraft:not_a_real_item"]})";
        invalidPackFixtureReady = static_cast<bool>(invalidTag);
    }
    bool requiredPackLoadRejected = false;
    if (invalidPackFixtureReady) {
        try {
            transactionalDatapacks.loadAll(
                transactionalRecipes, (nestedLootRoot / "empty-assets").string(),
                transactionalPacksRoot.string());
        } catch (const std::exception&) {
            requiredPackLoadRejected = true;
        }
    }
    const auto transactionalAfter =
        transactionalDatapacks.tagManager.itemTags.find("example:valid");
    const auto transactionalRecipeTag =
        transactionalRecipes.tags_.find("example:valid");
    const bool failedPackReloadPreservesPublishedState =
        requiredPackLoadRejected &&
        !transactionalDatapacks.isAvailable("invalid") &&
        transactionalDatapacks.isEnabled("good") &&
        transactionalAfter != transactionalDatapacks.tagManager.itemTags.end() &&
        transactionalAfter->second ==
            std::unordered_set<std::uint32_t>{transactionalDiamond} &&
        transactionalRecipeTag != transactionalRecipes.tags_.end() &&
        transactionalRecipeTag->second ==
            std::unordered_set<std::uint32_t>{transactionalDiamond};

    const auto overlayTag = nestedDatapacks.tagManager.itemTags.find(
        "example:overlay");
    const auto overlayAliasTag = nestedDatapacks.tagManager.itemTags.find(
        "example:overlay_alias");
    const auto softBlockTag = nestedDatapacks.tagManager.blockTags.find(
        "example:soft");
    const auto warmBiomeTag = nestedDatapacks.tagManager.biomeTags.find(
        "example:warm");
    const auto goldNuggetId = cppfm::gen::itemIdByName().at(
        "minecraft:gold_nugget");
    const auto diamondItemId = cppfm::gen::itemIdByName().at(
        "minecraft:diamond");
    const auto ironIngotItemId = cppfm::gen::itemIdByName().at(
        "minecraft:iron_ingot");
    const auto oakPlanksItemId = cppfm::gen::itemIdByName().at(
        "minecraft:oak_planks");
    const auto priorityProbeMatches = [&](const char* expectedItem,
                                          std::uint32_t expectedTagItem) {
        const auto loot = nestedDatapacks.lootTables.evaluateRegisteredTable(
            "example:blocks/priority_probe", {}, nullptr, 1);
        const auto tag = nestedDatapacks.tagManager.itemTags.find(
            "example:priority_probe");
        return loot && loot->size() == 1 &&
            loot->front().name() == expectedItem &&
            tag != nestedDatapacks.tagManager.itemTags.end() &&
            tag->second ==
                std::unordered_set<std::uint32_t>{expectedTagItem};
    };
    const auto replacedPlanksTag = nestedRecipes.tags_.find("minecraft:planks");
    const bool builtInTagReplacementApplied = nestedFixturesReady &&
        replacedPlanksTag != nestedRecipes.tags_.end() &&
        replacedPlanksTag->second ==
            std::unordered_set<std::uint32_t>{goldNuggetId} &&
        nestedRecipes.planksTag() == replacedPlanksTag->second;
    const bool namespacedTagsLoaded = nestedFixturesReady &&
        overlayTag != nestedDatapacks.tagManager.itemTags.end() &&
        overlayTag->second.size() == 1 &&
        overlayTag->second.contains(goldNuggetId) &&
        overlayAliasTag != nestedDatapacks.tagManager.itemTags.end() &&
        overlayAliasTag->second == overlayTag->second &&
        softBlockTag != nestedDatapacks.tagManager.blockTags.end() &&
        softBlockTag->second.contains(static_cast<std::uint32_t>(
            cppfm::gen::blockNameToState().at("minecraft:dirt"))) &&
        warmBiomeTag != nestedDatapacks.tagManager.biomeTags.end() &&
        warmBiomeTag->second.contains("minecraft:desert");
    const auto nestedDrops = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "minecraft:chests/root", {}, nullptr, 987654)
        : std::nullopt;
    const auto customNamespaceDrops = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/custom", {}, nullptr, 1234)
        : std::nullopt;
    bool binomialRollsStayInRange = true;
    bool observedZeroBinomialRolls = false;
    bool observedMaximumBinomialRolls = false;
    if (nestedFixturesReady) {
        for (std::int64_t seed = 0; seed < 256; ++seed) {
            const auto result = nestedLootEvaluator.evaluateRegisteredTable(
                "example:blocks/binomial_provider", {}, nullptr, seed);
            if (!result || result->size() > 3) {
                binomialRollsStayInRange = false;
                continue;
            }
            observedZeroBinomialRolls |= result->empty();
            observedMaximumBinomialRolls |= result->size() == 3;
            for (const auto& item : *result)
                if (item.name() != "minecraft:diamond")
                    binomialRollsStayInRange = false;
        }
    }
    const auto zeroBinomial = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/zero_binomial_count", {}, nullptr, 1234)
        : std::nullopt;
    cppfm::LootContext fortuneThree;
    fortuneThree.fortuneLevel = 3;
    int minimumUniformBonus = std::numeric_limits<int>::max();
    int maximumUniformBonus = 0;
    bool sawUniformBonusMinimum = false;
    bool sawUniformBonusMaximum = false;
    bool binomialBonusBounded = true;
    bool sawBinomialBonusMinimum = false;
    bool sawBinomialBonusMaximum = false;
    if (nestedFixturesReady) {
        for (std::int64_t seed = 0; seed < 256; ++seed) {
            const auto uniform = nestedLootEvaluator.evaluateRegisteredTable(
                "example:blocks/uniform_bonus", {}, &fortuneThree, seed);
            if (uniform && uniform->size() == 1) {
                minimumUniformBonus = std::min(
                    minimumUniformBonus, static_cast<int>(uniform->front().count));
                maximumUniformBonus = std::max(
                    maximumUniformBonus, static_cast<int>(uniform->front().count));
                sawUniformBonusMinimum |= uniform->front().count == 1;
                sawUniformBonusMaximum |= uniform->front().count == 7;
            }
            cppfm::LootContext fortuneTwo;
            fortuneTwo.fortuneLevel = 2;
            const auto binomial = nestedLootEvaluator.evaluateRegisteredTable(
                "example:blocks/binomial_bonus", {}, &fortuneTwo, seed);
            if (!binomial || binomial->size() != 1 ||
                binomial->front().count < 1 || binomial->front().count > 5)
                binomialBonusBounded = false;
            if (binomial && binomial->size() == 1) {
                sawBinomialBonusMinimum |= binomial->front().count == 1;
                sawBinomialBonusMaximum |= binomial->front().count == 5;
            }
        }
    }
    cppfm::LootContext noLooting;
    cppfm::LootContext lootingTwo;
    lootingTwo.lootingLevel = 2;
    bool enchantedCountIsBounded = true;
    bool sawEnchantedCountMinimum = false;
    bool sawEnchantedCountMaximum = false;
    if (nestedFixturesReady) {
        for (std::int64_t seed = 0; seed < 256; ++seed) {
            const auto result = nestedLootEvaluator.evaluateRegisteredTable(
                "example:blocks/enchanted_count_increase", {},
                &lootingTwo, seed);
            if (!result || result->size() > 1 ||
                (result->size() == 1 && result->front().count > 4)) {
                enchantedCountIsBounded = false;
                continue;
            }
            sawEnchantedCountMinimum |= result->empty();
            sawEnchantedCountMaximum |= result->size() == 1 &&
                                        result->front().count == 4;
        }
    }
    cppfm::LootContext playerKillNoLooting;
    playerKillNoLooting.hasEntityContext = true;
    playerKillNoLooting.wasKilledByPlayer = true;
    cppfm::LootContext playerKillWithLooting;
    playerKillWithLooting.hasEntityContext = true;
    playerKillWithLooting.wasKilledByPlayer = true;
    playerKillWithLooting.lootingLevel = 1;
    cppfm::LootContext fortuneEnchanted;
    fortuneEnchanted.fortuneLevel = 1;
    cppfm::LootContext unrelatedLooting;
    unrelatedLooting.lootingLevel = 1;
    const auto enchantedChanceBase = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/enchanted_chance", {},
              &playerKillNoLooting, 1)
        : std::nullopt;
    const auto enchantedChanceBonus = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/enchanted_chance", {},
              &playerKillWithLooting, 1)
        : std::nullopt;
    const auto fortuneChanceBonus = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/enchanted_fortune_chance", {},
              &fortuneEnchanted, 1)
        : std::nullopt;
    const auto mismatchedEnchantmentChance = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/enchanted_fortune_chance", {},
              &unrelatedLooting, 1)
        : std::nullopt;
    const auto lootingConditionFails = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/looting_condition", {}, &noLooting, 1)
        : std::nullopt;
    const auto lootingConditionPasses = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/looting_condition", {}, &lootingTwo, 1)
        : std::nullopt;
    const auto setCountAdd = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/set_count_add", {}, nullptr, 1)
        : std::nullopt;
    const auto functionOrder = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/function_order", {}, &fortuneThree, 1)
        : std::nullopt;
    const auto providerChance = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/random_chance_provider", {}, nullptr, 1)
        : std::nullopt;
    const auto anyOfCondition = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/any_of_condition", {}, nullptr, 1)
        : std::nullopt;
    cppfm::LootContext fireContext;
    fireContext.hasEntityContext = true;
    fireContext.entityOnFire = true;
    const auto anyOfFireCondition = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/any_of_condition", {}, &fireContext, 1)
        : std::nullopt;
    const auto nestedContextWithoutEntity = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/context_nested", {}, nullptr, 1)
        : std::nullopt;
    cppfm::LootContext notOnFireContext;
    notOnFireContext.hasEntityContext = true;
    const auto nestedContextNotOnFire = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/context_nested", {}, &notOnFireContext, 1)
        : std::nullopt;
    const auto nestedContextOnFire = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/context_nested", {}, &fireContext, 1)
        : std::nullopt;
    cppfm::LootContext playerActionContext;
    playerActionContext.hasEntityContext = true;
    const auto playerActionDoesNotCountAsKill = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/player_kill_context", {},
              &playerActionContext, 1)
        : std::nullopt;
    const auto playerKillPassesKillCondition = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/player_kill_context", {},
              &playerKillNoLooting, 1)
        : std::nullopt;
    const auto playerKillConditionNeedsEntityContext = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/player_kill_context", {}, nullptr, 1)
        : std::nullopt;
    bool nestedUniformCountIsBounded = true;
    bool sawNestedUniformCountMinimum = false;
    bool sawNestedUniformCountMaximum = false;
    if (nestedFixturesReady) {
        for (std::int64_t seed = 0; seed < 256; ++seed) {
            const auto result = nestedLootEvaluator.evaluateRegisteredTable(
                "example:blocks/nested_uniform_count", {}, nullptr, seed);
            if (!result || result->size() != 1 ||
                result->front().count < 1 || result->front().count > 4) {
                nestedUniformCountIsBounded = false;
                continue;
            }
            sawNestedUniformCountMinimum |= result->front().count == 1;
            sawNestedUniformCountMaximum |= result->front().count == 4;
        }
    }
    const auto unsupportedEnchantLevels = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/unsupported_enchant_levels", {}, nullptr, 1)
        : std::nullopt;
    const auto unsupportedEnchantLevelsPartial = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTableBestEffort(
              "example:blocks/unsupported_enchant_levels", {}, nullptr, 1)
        : std::nullopt;
    cppfm::BlockEntityStore registeredUnsupportedLootStore;
    auto registeredUnsupportedLootNbt =
        cppfm::nbt::Value::makeCompound();
    registeredUnsupportedLootNbt.set("id",
        cppfm::nbt::Value::makeString("minecraft:chest"));
    registeredUnsupportedLootNbt.set("x", cppfm::nbt::Value::makeInt(29));
    registeredUnsupportedLootNbt.set("y", cppfm::nbt::Value::makeInt(64));
    registeredUnsupportedLootNbt.set("z", cppfm::nbt::Value::makeInt(31));
    registeredUnsupportedLootNbt.set("LootTable",
        cppfm::nbt::Value::makeString(
            "example:blocks/unsupported_enchant_levels"));
    registeredUnsupportedLootNbt.set("LootTableSeed",
        cppfm::nbt::Value::makeLong(1));
    auto existingUnsupportedLootItems =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    auto existingUnsupportedLootItem =
        cppfm::nbt::Value::makeCompound();
    existingUnsupportedLootItem.set("Slot",
        cppfm::nbt::Value::makeByte(0));
    existingUnsupportedLootItem.set("id",
        cppfm::nbt::Value::makeString("minecraft:diamond"));
    existingUnsupportedLootItem.set("count",
        cppfm::nbt::Value::makeByte(1));
    existingUnsupportedLootItems.list.push_back(
        std::move(existingUnsupportedLootItem));
    registeredUnsupportedLootNbt.set(
        "Items", std::move(existingUnsupportedLootItems));
    const bool registeredUnsupportedLootInstalled =
        nestedFixturesReady && registeredUnsupportedLootStore.replaceNbtAt(
            29, 64, 31, std::move(registeredUnsupportedLootNbt));
    const auto registeredUnsupportedLootMaterialization =
        registeredUnsupportedLootInstalled
            ? cppfm::materializePendingLootAt(
                  registeredUnsupportedLootStore, nestedLootEvaluator,
                  29, 64, 31, 27)
            : cppfm::PendingLootMaterialization::Failed;
    const auto retainedRegisteredUnsupportedLoot =
        registeredUnsupportedLootInstalled
            ? cppfm::serializedBlockEntityAt(
                  registeredUnsupportedLootStore, 29, 64, 31)
            : std::nullopt;
    const auto* retainedRegisteredLootTable =
        retainedRegisteredUnsupportedLoot
            ? retainedRegisteredUnsupportedLoot->get("LootTable")
            : nullptr;
    const auto* retainedRegisteredLootSeed =
        retainedRegisteredUnsupportedLoot
            ? retainedRegisteredUnsupportedLoot->get("LootTableSeed")
            : nullptr;
    const auto* retainedRegisteredLootItems =
        retainedRegisteredUnsupportedLoot
            ? retainedRegisteredUnsupportedLoot->get("Items")
            : nullptr;
    const bool registeredUnsupportedLootPreserved =
        registeredUnsupportedLootMaterialization ==
            cppfm::PendingLootMaterialization::UnsupportedTable &&
        retainedRegisteredLootTable &&
        retainedRegisteredLootTable->tag == cppfm::nbt::String &&
        retainedRegisteredLootTable->str ==
            "example:blocks/unsupported_enchant_levels" &&
        retainedRegisteredLootSeed &&
        retainedRegisteredLootSeed->tag == cppfm::nbt::Long &&
        retainedRegisteredLootSeed->l == 1 &&
        retainedRegisteredLootItems &&
        retainedRegisteredLootItems->tag == cppfm::nbt::List &&
        retainedRegisteredLootItems->list.size() == 1 &&
        retainedRegisteredLootItems->list.front().get("id") &&
        retainedRegisteredLootItems->list.front().get("id")->str ==
            "minecraft:diamond";
    cppfm::BlockEntityStore contextLootStore;
    auto contextLootChest = cppfm::nbt::Value::makeCompound();
    contextLootChest.set("id", cppfm::nbt::Value::makeString(
        "minecraft:chest"));
    contextLootChest.set("x", cppfm::nbt::Value::makeInt(30));
    contextLootChest.set("y", cppfm::nbt::Value::makeInt(64));
    contextLootChest.set("z", cppfm::nbt::Value::makeInt(31));
    contextLootChest.set("LootTable", cppfm::nbt::Value::makeString(
        "example:blocks/context_nested"));
    contextLootChest.set("Items",
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound));
    const bool contextLootChestInstalled = nestedFixturesReady &&
        contextLootStore.replaceNbtAt(
            30, 64, 31, std::move(contextLootChest));
    const auto contextLootWithoutEntity = contextLootChestInstalled
        ? cppfm::materializePendingLootAt(
              contextLootStore, nestedLootEvaluator, 30, 64, 31, 27)
        : cppfm::PendingLootMaterialization::Failed;
    const auto contextLootRetained = contextLootChestInstalled
        ? cppfm::serializedBlockEntityAt(contextLootStore, 30, 64, 31)
        : std::nullopt;
    const bool contextLootTableRetained = contextLootRetained &&
        contextLootRetained->get("LootTable") &&
        contextLootRetained->get("LootTable")->tag == cppfm::nbt::String &&
        contextLootRetained->get("LootTable")->str ==
            "example:blocks/context_nested";
    const auto contextLootMaterialized = contextLootChestInstalled
        ? cppfm::materializePendingLootAt(
              contextLootStore, nestedLootEvaluator, 30, 64, 31, 27,
              &fireContext)
        : cppfm::PendingLootMaterialization::Failed;
    const auto contextLootResult = contextLootChestInstalled
        ? cppfm::serializedBlockEntityAt(contextLootStore, 30, 64, 31)
        : std::nullopt;
    const auto* contextLootItems = contextLootResult
        ? contextLootResult->get("Items") : nullptr;
    const auto contextLootDiamond = contextLootItems &&
            contextLootItems->tag == cppfm::nbt::List &&
            contextLootItems->list.size() == 1
        ? cppfm::ItemStack::fromNbt(contextLootItems->list.front())
        : std::nullopt;
    const auto zeroUniformRolls = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/zero_uniform_rolls", {}, nullptr, 1)
        : std::nullopt;
    const auto* zeroUniformBlockTable = nestedFixturesReady
        ? nestedLootEvaluator.findBlockTable("example:zero_uniform_rolls")
        : nullptr;
    check(nestedFixturesReady && zeroUniformRolls &&
              zeroUniformRolls->empty() && zeroUniformBlockTable,
          "a registered block loot table with a valid empty result remains distinguishable from no table");
    auto emptyPendingChest = cppfm::nbt::Value::makeCompound();
    emptyPendingChest.set("id", cppfm::nbt::Value::makeString(
        "minecraft:chest"));
    emptyPendingChest.set("LootTable", cppfm::nbt::Value::makeString(
        "example:blocks/zero_uniform_rolls"));
    emptyPendingChest.set("Items",
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound));
    const auto emptyPendingChestDrops = nestedFixturesReady
        ? cppfm::planPendingContainerLootDrops(emptyPendingChest,
                                          nestedLootEvaluator)
        : cppfm::PendingLootDropPlan{};
    auto emptyPendingBarrel = emptyPendingChest;
    emptyPendingBarrel.set("id", cppfm::nbt::Value::makeString(
        "minecraft:barrel"));
    const auto emptyPendingBarrelDrops = nestedFixturesReady
        ? cppfm::planPendingContainerLootDrops(emptyPendingBarrel,
                                               nestedLootEvaluator)
        : cppfm::PendingLootDropPlan{};
    check(nestedFixturesReady &&
              emptyPendingChestDrops.status ==
                  cppfm::PendingLootDropPlanStatus::Ready &&
              emptyPendingChestDrops.items.empty() &&
              emptyPendingBarrelDrops.status ==
                  cppfm::PendingLootDropPlanStatus::Ready &&
              emptyPendingBarrelDrops.items.empty(),
          "destroying a chest or barrel with registered valid-empty pending loot creates no fabricated contents");
    auto contextPendingChest = cppfm::nbt::Value::makeCompound();
    contextPendingChest.set("id", cppfm::nbt::Value::makeString(
        "minecraft:chest"));
    contextPendingChest.set("LootTable", cppfm::nbt::Value::makeString(
        "example:blocks/context_nested"));
    contextPendingChest.set("Items",
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound));
    const auto contextPendingWithoutEntity = nestedFixturesReady
        ? cppfm::planPendingContainerLootDrops(
              contextPendingChest, nestedLootEvaluator)
        : cppfm::PendingLootDropPlan{};
    const auto contextPendingNotOnFire = nestedFixturesReady
        ? cppfm::planPendingContainerLootDrops(
              contextPendingChest, nestedLootEvaluator, &notOnFireContext)
        : cppfm::PendingLootDropPlan{};
    const auto contextPendingOnFire = nestedFixturesReady
        ? cppfm::planPendingContainerLootDrops(
              contextPendingChest, nestedLootEvaluator, &fireContext)
        : cppfm::PendingLootDropPlan{};
    check(contextPendingWithoutEntity.status ==
              cppfm::PendingLootDropPlanStatus::UnsupportedTable &&
              contextPendingNotOnFire.status ==
                  cppfm::PendingLootDropPlanStatus::Ready &&
              contextPendingNotOnFire.items.empty() &&
              contextPendingOnFire.status ==
                  cppfm::PendingLootDropPlanStatus::Ready &&
              contextPendingOnFire.items.size() == 1 &&
              contextPendingOnFire.items.front().name() ==
                  "minecraft:diamond",
          "pending loot drop planning preserves context-dependent tables unless a real triggering-entity context is supplied");
    using PendingLootKind = cppfm::BlockEntity::Kind;
    const std::vector<std::tuple<const char*, std::size_t,
                                 PendingLootKind>> pendingLootContainers{
        {"minecraft:chest", 27, PendingLootKind::Chest},
        {"minecraft:trapped_chest", 27, PendingLootKind::Chest},
        {"minecraft:barrel", 27, PendingLootKind::Barrel},
        {"minecraft:hopper", 5, PendingLootKind::Hopper},
        {"minecraft:dispenser", 9, PendingLootKind::Dispenser},
        {"minecraft:dropper", 9, PendingLootKind::Dropper},
        {"minecraft:crafter", 9, PendingLootKind::Crafter},
    };
    bool pendingLootContainerIdsAndSlotsSupported = nestedFixturesReady;
    for (const auto& [id, slots, kind] : pendingLootContainers) {
        auto record = cppfm::nbt::Value::makeCompound();
        record.set("id", cppfm::nbt::Value::makeString(id));
        record.set("LootTable", cppfm::nbt::Value::makeString(
            "example:blocks/omitted_rolls"));
        record.set("Items",
            cppfm::nbt::Value::makeList(cppfm::nbt::Compound));
        const auto planned = cppfm::planPendingContainerLootDrops(
            record, nestedLootEvaluator, nullptr, slots);
        const auto actualSlots = cppfm::pendingLootInventorySlots(id);
        pendingLootContainerIdsAndSlotsSupported &= actualSlots &&
            *actualSlots == slots &&
            cppfm::pendingLootBlockEntityMatchesKind(record, kind) &&
            planned.status == cppfm::PendingLootDropPlanStatus::Ready &&
            planned.items.size() == 1 &&
            planned.items.front().name() == "minecraft:emerald";
    }
    auto sixLootHopper = cppfm::nbt::Value::makeCompound();
    sixLootHopper.set("id", cppfm::nbt::Value::makeString(
        "minecraft:hopper"));
    sixLootHopper.set("LootTable", cppfm::nbt::Value::makeString(
        "example:blocks/six_stacks"));
    sixLootHopper.set("Items",
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound));
    const auto sixLootHopperPlan = nestedFixturesReady
        ? cppfm::planPendingContainerLootDrops(
              sixLootHopper, nestedLootEvaluator)
        : cppfm::PendingLootDropPlan{};
    auto sixLootDispenser = sixLootHopper;
    sixLootDispenser.set("id", cppfm::nbt::Value::makeString(
        "minecraft:dispenser"));
    const auto sixLootDispenserPlan = nestedFixturesReady
        ? cppfm::planPendingContainerLootDrops(
              sixLootDispenser, nestedLootEvaluator)
        : cppfm::PendingLootDropPlan{};
    const auto wrongHopperCapacity = nestedFixturesReady
        ? cppfm::planPendingContainerLootDrops(
              sixLootHopper, nestedLootEvaluator, nullptr, 9)
        : cppfm::PendingLootDropPlan{};
    auto nonLootableBlockEntity = sixLootHopper;
    nonLootableBlockEntity.set("id", cppfm::nbt::Value::makeString(
        "minecraft:furnace"));
    const auto nonLootablePlan = nestedFixturesReady
        ? cppfm::planPendingContainerLootDrops(
              nonLootableBlockEntity, nestedLootEvaluator)
        : cppfm::PendingLootDropPlan{};
    auto decoratedPotLoot = sixLootHopper;
    decoratedPotLoot.set("id", cppfm::nbt::Value::makeString(
        "minecraft:decorated_pot"));
    const auto decoratedPotPlan = nestedFixturesReady
        ? cppfm::planPendingContainerLootDrops(
              decoratedPotLoot, nestedLootEvaluator)
        : cppfm::PendingLootDropPlan{};
    check(pendingLootContainerIdsAndSlotsSupported &&
              sixLootHopperPlan.status ==
                  cppfm::PendingLootDropPlanStatus::InvalidData &&
              sixLootDispenserPlan.status ==
                  cppfm::PendingLootDropPlanStatus::Ready &&
              sixLootDispenserPlan.items.size() == 6 &&
              wrongHopperCapacity.status ==
                  cppfm::PendingLootDropPlanStatus::InvalidData &&
              nonLootablePlan.status ==
                  cppfm::PendingLootDropPlanStatus::InvalidData &&
              decoratedPotPlan.status ==
                  cppfm::PendingLootDropPlanStatus::InvalidData &&
              !cppfm::pendingLootInventorySlots(
                  "minecraft:decorated_pot"),
          "pending loot plans validate supported inventory IDs and capacities while rejecting furnace and decorated-pot records fail-closed");
    const auto omittedRolls = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/omitted_rolls", {}, nullptr, 1)
        : std::nullopt;
    const auto unsupportedBonusRolls = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/bonus_rolls", {}, nullptr, 1)
        : std::nullopt;
    const auto unsupportedBonusRollsPartial = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTableBestEffort(
              "example:blocks/bonus_rolls", {}, nullptr, 1)
        : std::nullopt;
    const bool overlayPacksInitiallyEnabled = nestedFixturesReady &&
        nestedDatapacks.isEnabled("a-first") &&
        nestedDatapacks.isEnabled("z-last");
    const bool higherOverlayDisabled = nestedFixturesReady &&
        nestedDatapacks.disablePack("z-last");
    const auto lowerRecipeTagAfterDisable = nestedRecipes.tags_.find(
        "example:overlay");
    const auto lowerPlanksTagAfterDisable = nestedRecipes.tags_.find(
        "minecraft:planks");
    const bool recipeTagsRebuiltAfterPackDisable = nestedFixturesReady &&
        lowerRecipeTagAfterDisable != nestedRecipes.tags_.end() &&
        lowerRecipeTagAfterDisable->second.contains(diamondItemId) &&
        lowerRecipeTagAfterDisable->second.contains(ironIngotItemId) &&
        !lowerRecipeTagAfterDisable->second.contains(goldNuggetId) &&
        lowerPlanksTagAfterDisable != nestedRecipes.tags_.end() &&
        lowerPlanksTagAfterDisable->second.contains(diamondItemId) &&
        lowerPlanksTagAfterDisable->second.contains(ironIngotItemId) &&
        !lowerPlanksTagAfterDisable->second.contains(goldNuggetId);
    const auto lowerOverlayDrops = nestedFixturesReady
        ? nestedDatapacks.lootTables.evaluateRegisteredTable(
              "example:blocks/ordered", {}, nullptr, 1)
        : std::nullopt;
    const bool higherOverlayMovedToLast = nestedFixturesReady &&
        nestedDatapacks.enablePack("z-last");
    const auto higherOverlayWinsAfterReenable = nestedFixturesReady
        ? nestedDatapacks.lootTables.evaluateRegisteredTable(
              "example:blocks/ordered", {}, nullptr, 1)
        : std::nullopt;
    const auto priorityOrder = nestedDatapacks.listEnabled();
    const auto lowerPriorityPosition = std::find(
        priorityOrder.begin(), priorityOrder.end(), "a-first");
    const auto higherPriorityPosition = std::find(
        priorityOrder.begin(), priorityOrder.end(), "z-last");
    const bool explicitEnableOrderIsExposed =
        lowerPriorityPosition != priorityOrder.end() &&
        higherPriorityPosition != priorityOrder.end() &&
        lowerPriorityPosition < higherPriorityPosition;
    const bool higherOverlayDisabledAfterPriorityCheck = nestedFixturesReady &&
        nestedDatapacks.disablePack("z-last");
    const bool packEnabledAtFirst = nestedFixturesReady &&
        nestedDatapacks.enablePack(
            "z-last", cppfm::DatapackManager::PackPosition::First);
    const auto firstPriorityOrder = nestedDatapacks.listEnabled();
    const bool firstPriorityOrderApplied = packEnabledAtFirst &&
        !firstPriorityOrder.empty() && firstPriorityOrder.front() == "z-last";
    const bool firstPriorityPackDisabled = nestedFixturesReady &&
        nestedDatapacks.disablePack("z-last");
    const bool packEnabledBeforeReference = nestedFixturesReady &&
        nestedDatapacks.enablePack(
            "z-last", cppfm::DatapackManager::PackPosition::Before,
            "a-first");
    const auto beforePriorityDrops = nestedFixturesReady
        ? nestedDatapacks.lootTables.evaluateRegisteredTable(
              "example:blocks/ordered", {}, nullptr, 1)
        : std::nullopt;
    const auto beforePriorityOrder = nestedDatapacks.listEnabled();
    const auto beforePosition = std::find(beforePriorityOrder.begin(),
                                          beforePriorityOrder.end(), "z-last");
    const auto referencePosition = std::find(beforePriorityOrder.begin(),
                                             beforePriorityOrder.end(), "a-first");
    const bool beforePriorityOrderApplied = packEnabledBeforeReference &&
        beforePosition != beforePriorityOrder.end() &&
        referencePosition != beforePriorityOrder.end() &&
        beforePosition < referencePosition;
    const bool beforePriorityPackDisabled = nestedFixturesReady &&
        nestedDatapacks.disablePack("z-last");
    const bool packEnabledAfterReference = nestedFixturesReady &&
        nestedDatapacks.enablePack(
            "z-last", cppfm::DatapackManager::PackPosition::After,
            "a-first");
    const auto afterPriorityDrops = nestedFixturesReady
        ? nestedDatapacks.lootTables.evaluateRegisteredTable(
              "example:blocks/ordered", {}, nullptr, 1)
        : std::nullopt;
    const auto afterPriorityOrder = nestedDatapacks.listEnabled();
    const auto afterPosition = std::find(afterPriorityOrder.begin(),
                                         afterPriorityOrder.end(), "z-last");
    const auto afterReferencePosition = std::find(
        afterPriorityOrder.begin(), afterPriorityOrder.end(), "a-first");
    const bool afterPriorityOrderApplied = packEnabledAfterReference &&
        afterPosition != afterPriorityOrder.end() &&
        afterReferencePosition != afterPriorityOrder.end() &&
        afterReferencePosition < afterPosition;
    const bool afterPriorityPackDisabled = nestedFixturesReady &&
        nestedDatapacks.disablePack("z-last");
    const bool priorityTargetValidationIsAtomic = nestedFixturesReady &&
        !nestedDatapacks.enablePack(
            "z-last", cppfm::DatapackManager::PackPosition::Before,
            "missing-pack") && !nestedDatapacks.isEnabled("z-last");
    const bool packEnabledAtLast = nestedFixturesReady &&
        nestedDatapacks.enablePack(
            "z-last", cppfm::DatapackManager::PackPosition::Last);
    const auto lastPriorityOrder = nestedDatapacks.listEnabled();
    const bool lastPriorityOrderApplied = packEnabledAtLast &&
        !lastPriorityOrder.empty() && lastPriorityOrder.back() == "z-last";
    const bool zLastDisabledForVanillaPriority = nestedFixturesReady &&
        nestedDatapacks.disablePack("z-last");
    const bool aFirstDisabledForVanillaPriority = nestedFixturesReady &&
        nestedDatapacks.disablePack("a-first");
    const auto checkVanillaBoundaryPosition = [&]
        (cppfm::DatapackManager::PackPosition position,
         bool expectedBeforeVanilla, const char* expectedItem,
         std::uint32_t expectedTagItem) {
        if (!nestedDatapacks.enablePack("a-first", position, "vanilla"))
            return false;
        const auto order = nestedDatapacks.listEnabled();
        const auto pack = std::find(order.begin(), order.end(), "a-first");
        const auto vanilla = std::find(order.begin(), order.end(), "vanilla");
        const bool relativeOrder = pack != order.end() &&
            vanilla != order.end() &&
            ((pack < vanilla) == expectedBeforeVanilla);
        const bool resourcesMatch = priorityProbeMatches(
            expectedItem, expectedTagItem);
        const bool disabled = nestedDatapacks.disablePack("a-first");
        return relativeOrder && resourcesMatch && disabled;
    };
    const bool firstPositionResolvesToVanilla = nestedFixturesReady &&
        checkVanillaBoundaryPosition(
            cppfm::DatapackManager::PackPosition::First, true,
            "minecraft:iron_ingot", ironIngotItemId);
    const bool lastPositionResolvesToWorldPack = nestedFixturesReady &&
        checkVanillaBoundaryPosition(
            cppfm::DatapackManager::PackPosition::Last, false,
            "minecraft:diamond", diamondItemId);
    const bool beforeVanillaResolvesToVanilla = nestedFixturesReady &&
        checkVanillaBoundaryPosition(
            cppfm::DatapackManager::PackPosition::Before, true,
            "minecraft:iron_ingot", ironIngotItemId);
    const bool afterVanillaResolvesToWorldPack = nestedFixturesReady &&
        checkVanillaBoundaryPosition(
            cppfm::DatapackManager::PackPosition::After, false,
            "minecraft:diamond", diamondItemId);
    const bool aFirstRestoredForCleanup = nestedFixturesReady &&
        nestedDatapacks.enablePack(
            "a-first", cppfm::DatapackManager::PackPosition::Last);
    const bool zLastRestoredForCleanup = nestedFixturesReady &&
        nestedDatapacks.enablePack(
            "z-last", cppfm::DatapackManager::PackPosition::Last);
    const bool vanillaBoundaryPriorityApplied =
        zLastDisabledForVanillaPriority &&
        aFirstDisabledForVanillaPriority &&
        firstPositionResolvesToVanilla && lastPositionResolvesToWorldPack &&
        beforeVanillaResolvesToVanilla &&
        afterVanillaResolvesToWorldPack && aFirstRestoredForCleanup &&
        zLastRestoredForCleanup;
    const bool higherOverlayDisabledForCleanup = nestedFixturesReady &&
        nestedDatapacks.disablePack("z-last");
    const bool lowerOverlayDisabled = nestedFixturesReady &&
        nestedDatapacks.disablePack("a-first");
    const bool overlayRemovedWhenDisabled = nestedFixturesReady &&
        nestedDatapacks.lootTables.find("example:blocks/ordered") == nullptr;
    const bool recipeTagsRemovedWhenAllPacksDisabled = nestedFixturesReady &&
        !nestedRecipes.tags_.contains("example:overlay") &&
        nestedRecipes.tags_.contains("minecraft:planks") &&
        nestedRecipes.tags_.at("minecraft:planks").contains(oakPlanksItemId) &&
        nestedRecipes.tags_.at("minecraft:planks") ==
            nestedRecipes.planksTag();
    if (nestedFixturesReady)
        nestedDatapacks.loadAll(
            nestedRecipes, (nestedLootRoot / "data").string(),
            worldDatapacksRoot.string());
    const bool disabledPacksSurviveReload = nestedFixturesReady &&
        !nestedDatapacks.isEnabled("a-first") &&
        !nestedDatapacks.isEnabled("z-last") &&
        nestedDatapacks.lootTables.find("example:blocks/ordered") == nullptr;
    const bool configuredRecipeSurvivesReload =
        nestedRecipes.findById("minecraft:probe_recipe") != nullptr;
    const bool lowerOverlayReenabled = nestedFixturesReady &&
        nestedDatapacks.enablePack("a-first");
    const auto lowerOverlayAfterReenable = nestedFixturesReady
        ? nestedDatapacks.lootTables.evaluateRegisteredTable(
              "example:blocks/ordered", {}, nullptr, 1)
        : std::nullopt;
    const bool lowerOverlayDisabledAgain = nestedFixturesReady &&
        nestedDatapacks.disablePack("a-first");
    const bool higherOverlayReenabled = nestedFixturesReady &&
        nestedDatapacks.enablePack("z-last");
    const auto higherOverlayAfterReenable = nestedFixturesReady
        ? nestedDatapacks.lootTables.evaluateRegisteredTable(
              "example:blocks/ordered", {}, nullptr, 1)
        : std::nullopt;
    cppfm::LootContext explosionRadiusOne;
    explosionRadiusOne.explosionRadius = 1.f;
    const auto survivesRadiusOne = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/survives_explosion", {},
              &explosionRadiusOne, 1)
        : std::nullopt;
    const auto decaysRadiusOne = nestedFixturesReady
        ? nestedLootEvaluator.evaluateRegisteredTable(
              "example:blocks/explosion_decay", {}, &explosionRadiusOne, 1)
        : std::nullopt;
    cppfm::LootContext explosionRadiusTwo;
    explosionRadiusTwo.explosionRadius = 2.f;
    bool explosionDecayHasPerItemCounts = false;
    if (nestedFixturesReady) {
        for (std::int64_t seed = 0; seed < 32; ++seed) {
            const auto decayed = nestedLootEvaluator.evaluateRegisteredTable(
                "example:blocks/explosion_decay", {}, &explosionRadiusTwo,
                seed);
            if (decayed && decayed->size() == 1 &&
                decayed->front().count > 0 && decayed->front().count < 64)
                explosionDecayHasPerItemCounts = true;
        }
    }
    const auto* unsupportedComposite = nestedFixturesReady
        ? nestedLootEvaluator.findBlockTable("minecraft:composite")
        : nullptr;
    cppfm::BlockEntityStore nestedLootStore;
    auto nestedChest = cppfm::nbt::Value::makeCompound();
    nestedChest.set("id",
        cppfm::nbt::Value::makeString("minecraft:chest"));
    nestedChest.set("x", cppfm::nbt::Value::makeInt(18));
    nestedChest.set("y", cppfm::nbt::Value::makeInt(64));
    nestedChest.set("z", cppfm::nbt::Value::makeInt(18));
    nestedChest.set("LootTable",
        cppfm::nbt::Value::makeString("minecraft:chests/root"));
    nestedChest.set("LootTableSeed", cppfm::nbt::Value::makeLong(987654));
    nestedChest.set("Items",
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound));
    const bool nestedChestInstalled = nestedFixturesReady &&
        nestedLootStore.replaceNbtAt(18, 64, 18, std::move(nestedChest));
    const auto nestedMaterialization = nestedChestInstalled
        ? cppfm::materializePendingLootAt(
              nestedLootStore, nestedLootEvaluator, 18, 64, 18, 27)
        : cppfm::PendingLootMaterialization::Failed;
    const auto materializedNestedChest =
        cppfm::serializedBlockEntityAt(nestedLootStore, 18, 64, 18);
    const auto* nestedItems = materializedNestedChest
        ? materializedNestedChest->get("Items") : nullptr;
    const auto nestedDiamond = nestedItems &&
            nestedItems->tag == cppfm::nbt::List &&
            !nestedItems->list.empty()
        ? cppfm::ItemStack::fromNbt(nestedItems->list.front())
        : std::nullopt;
    check(configuredRecipeLoaded,
          "configured recipe fixture loads before datapack initialization");
    check(configuredRecipeSurvivesDatapackLoad,
          "datapack initialization preserves configured recipes");
    check(configuredRecipeSurvivesReload,
          "datapack reload preserves configured recipes");
    check(!unsupportedEnchantLevels && unsupportedEnchantLevelsPartial &&
              unsupportedEnchantLevelsPartial->empty() &&
              !unsupportedBonusRolls && unsupportedBonusRollsPartial &&
              unsupportedBonusRollsPartial->empty() &&
              registeredUnsupportedLootPreserved,
          "registered loot with unsupported semantics remains unavailable and pending container NBT is preserved");
    check(nestedFixturesReady && !anyOfCondition && anyOfFireCondition &&
              anyOfFireCondition->size() == 1 &&
              !nestedContextWithoutEntity && nestedContextNotOnFire &&
              nestedContextNotOnFire->empty() && nestedContextOnFire &&
              nestedContextOnFire->size() == 1 &&
              nestedContextOnFire->front().name() == "minecraft:diamond",
          "registered and nested entity-dependent loot fails closed without an entity and evaluates with explicit fire context");
    check(nestedFixturesReady && playerActionDoesNotCountAsKill &&
              playerActionDoesNotCountAsKill->empty() &&
              playerKillPassesKillCondition &&
              playerKillPassesKillCondition->size() == 1 &&
              playerKillPassesKillCondition->front().name() ==
                  "minecraft:diamond" && !playerKillConditionNeedsEntityContext,
          "player block actions do not satisfy killed_by_player while actual player-kill loot still does");
    check(nestedFixturesReady &&
              nestedLootEvaluator.blockTableRequiresEntityContext(
                  "example:context_nested"),
          "block-drop callers can detect nested tables that require a real player/entity context");
    check(contextLootWithoutEntity ==
              cppfm::PendingLootMaterialization::UnsupportedTable &&
              contextLootTableRetained &&
              contextLootMaterialized ==
                  cppfm::PendingLootMaterialization::Materialized &&
              contextLootResult && !contextLootResult->get("LootTable") &&
              contextLootDiamond &&
              contextLootDiamond->name() == "minecraft:diamond",
          "pending nested loot metadata is preserved without context and materializes from a player fire context");
    check(namespacedTagsLoaded,
          "namespaced datapack item, block, biome, replace, and referenced tags load");
    check(optionalTagEntriesAreAccepted,
          "optional missing datapack tag IDs and references are ignored");
    check(transactionalInitialStateIsValid && requiredAirTagResolvesToZero &&
              failedPackReloadPreservesPublishedState,
          "required air ID zero resolves, while an unknown required value rejects reload without replacing live datapack or recipe-tag state");
    check(builtInTagReplacementApplied,
          "replace:true replaces a built-in recipe tag without leaking baseline items");
    check(vanillaBoundaryPriorityApplied,
          "first/last/before/after vanilla priority selects matching loot and tag values");
    check(fortuneChanceBonus && fortuneChanceBonus->size() == 1,
          "enchanted-bonus loot condition reads the declared Fortune enchantment");
    check(mismatchedEnchantmentChance &&
              mismatchedEnchantmentChance->empty(),
          "enchanted-bonus loot condition does not substitute Looting for Fortune");
    check(nestedFixturesReady && configuredRecipeLoaded &&
              configuredRecipeSurvivesDatapackLoad &&
              configuredRecipeSurvivesReload && namespacedTagsLoaded &&
              nestedDrops && nestedDrops->size() == 1 &&
              nestedDrops->front().name() == "minecraft:diamond" &&
              nestedMaterialization ==
                  cppfm::PendingLootMaterialization::Materialized &&
              nestedDiamond && nestedDiamond->name() == "minecraft:diamond" &&
              customNamespaceDrops && customNamespaceDrops->size() == 1 &&
              customNamespaceDrops->front().name() == "minecraft:diamond" &&
              nestedDatapacks.lootTables.find(
                  "example:blocks/custom") != nullptr &&
              binomialRollsStayInRange && observedZeroBinomialRolls &&
              observedMaximumBinomialRolls &&
              zeroBinomial && zeroBinomial->empty() &&
              minimumUniformBonus == 1 && maximumUniformBonus == 7 &&
              sawUniformBonusMinimum && sawUniformBonusMaximum &&
              binomialBonusBounded && sawBinomialBonusMinimum &&
              sawBinomialBonusMaximum && lootingConditionFails &&
              lootingConditionFails->empty() && lootingConditionPasses &&
              lootingConditionPasses->size() == 1 &&
              zeroUniformRolls && zeroUniformRolls->empty() &&
              zeroUniformBlockTable &&
              omittedRolls && omittedRolls->size() == 1 &&
              omittedRolls->front().name() == "minecraft:emerald" &&
              setCountAdd && setCountAdd->size() == 1 &&
              setCountAdd->front().count == 3 && functionOrder &&
              functionOrder->size() == 1 &&
              functionOrder->front().count == 1 && providerChance &&
              providerChance->size() == 1 && anyOfFireCondition &&
              anyOfFireCondition->size() == 1 && nestedUniformCountIsBounded &&
              sawNestedUniformCountMinimum && sawNestedUniformCountMaximum &&
              enchantedCountIsBounded && sawEnchantedCountMinimum &&
              sawEnchantedCountMaximum && enchantedChanceBase &&
              enchantedChanceBase->empty() && enchantedChanceBonus &&
              enchantedChanceBonus->size() == 1 && fortuneChanceBonus &&
              fortuneChanceBonus->size() == 1 &&
              mismatchedEnchantmentChance &&
              mismatchedEnchantmentChance->empty() &&
              !nestedLootEvaluator.loadDiagnostics().empty() &&
              overlayPacksInitiallyEnabled && higherOverlayDisabled &&
              lowerOverlayDrops && lowerOverlayDrops->size() == 1 &&
              lowerOverlayDrops->front().name() == "minecraft:diamond" &&
              higherOverlayMovedToLast && higherOverlayWinsAfterReenable &&
              higherOverlayWinsAfterReenable->size() == 1 &&
              higherOverlayWinsAfterReenable->front().name() ==
                  "minecraft:emerald" && explicitEnableOrderIsExposed &&
              higherOverlayDisabledAfterPriorityCheck && lowerOverlayDisabled &&
              firstPriorityOrderApplied && firstPriorityPackDisabled &&
              beforePriorityOrderApplied && beforePriorityDrops &&
              beforePriorityDrops->size() == 1 &&
              beforePriorityDrops->front().name() == "minecraft:diamond" &&
              beforePriorityPackDisabled && afterPriorityOrderApplied &&
              afterPriorityDrops && afterPriorityDrops->size() == 1 &&
              afterPriorityDrops->front().name() == "minecraft:emerald" &&
              afterPriorityPackDisabled && priorityTargetValidationIsAtomic &&
              lastPriorityOrderApplied &&
              higherOverlayDisabledForCleanup &&
              overlayRemovedWhenDisabled && recipeTagsRebuiltAfterPackDisable &&
              recipeTagsRemovedWhenAllPacksDisabled &&
              disabledPacksSurviveReload && lowerOverlayReenabled &&
              lowerOverlayAfterReenable &&
              lowerOverlayAfterReenable->size() == 1 &&
              lowerOverlayAfterReenable->front().name() ==
                  "minecraft:diamond" && lowerOverlayDisabledAgain &&
              higherOverlayReenabled &&
              higherOverlayAfterReenable &&
              higherOverlayAfterReenable->size() == 1 &&
              higherOverlayAfterReenable->front().name() ==
                  "minecraft:emerald" && vanillaBoundaryPriorityApplied &&
              survivesRadiusOne && survivesRadiusOne->size() == 1 &&
              decaysRadiusOne && decaysRadiusOne->size() == 1 &&
              decaysRadiusOne->front().count == 64 &&
              explosionDecayHasPerItemCounts &&
              nestedLootEvaluator.findBlockTable(
                  "example:binomial_provider") &&
              unsupportedComposite &&
              nestedLootEvaluator.evaluate("minecraft:composite").empty() &&
              materializedNestedChest &&
              !materializedNestedChest->get("LootTable") &&
              !nestedLootEvaluator.findBlockTable("minecraft:stone"),
          "1.21 namespaced loot tables load through DatapackManager, nested container loot works, and unsupported providers do not become false drops");
    if (nestedLootRootCreated) {
        nestedLootError.clear();
        std::filesystem::remove_all(nestedLootRoot, nestedLootError);
        check(!nestedLootError,
              "owned nested loot-table fixture directory is removed after evaluation");
    }
    cppfm::nbt::Value invalidShulkerEntity = savedShulkerEntity;
    invalidShulkerEntity.get("Items")->list.front().set(
        "id", cppfm::nbt::Value::makeString("example:unknown_item"));
    check(!cppfm::makeShulkerBoxDropItem(
               "minecraft:red_shulker_box", invalidShulkerEntity),
          "shulker drop conversion refuses nested item data this server cannot encode");
    cppfm::nbt::Value unsupportedShulkerMetadata = savedShulkerEntity;
    unsupportedShulkerMetadata.set(
        "example:mod_data", cppfm::nbt::Value::makeString("opaque"));
    check(!cppfm::makeShulkerBoxDropItem(
               "minecraft:red_shulker_box", unsupportedShulkerMetadata),
          "shulker drop conversion refuses unsupported block-entity metadata");
    cppfm::nbt::Value malformedShulkerName = savedShulkerEntity;
    malformedShulkerName.set("CustomName",
                             cppfm::nbt::Value::makeInt(12));
    check(!cppfm::makeShulkerBoxDropItem(
               "minecraft:red_shulker_box", malformedShulkerName),
          "shulker drop conversion refuses a CustomName with an unsupported NBT type");

    cppfm::nbt::Value legacyListItem = cppfm::nbt::Value::makeCompound();
    legacyListItem.set("id", cppfm::nbt::Value::makeString(
        "minecraft:diamond_pickaxe"));
    legacyListItem.set("count", cppfm::nbt::Value::makeInt(2));
    legacyListItem.set("Slot", cppfm::nbt::Value::makeByte(0));
    cppfm::nbt::Value legacyListComponents =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value unsupportedLegacyComponent =
        cppfm::nbt::Value::makeCompound();
    unsupportedLegacyComponent.set("type",
        cppfm::nbt::Value::makeInt(11));
    cppfm::nbt::Value unsupportedLegacyPayload;
    unsupportedLegacyPayload.tag = cppfm::nbt::ByteArray;
    unsupportedLegacyPayload.byteArray = {0x04, 0xAA, 0x00};
    unsupportedLegacyComponent.set("data",
                                   std::move(unsupportedLegacyPayload));
    unsupportedLegacyComponent.set("future_field",
        cppfm::nbt::Value::makeString("preserve unknown fields"));
    legacyListComponents.list.push_back(
        std::move(unsupportedLegacyComponent));
    cppfm::nbt::Value malformedLegacyComponent =
        cppfm::nbt::Value::makeCompound();
    malformedLegacyComponent.set("data",
        cppfm::nbt::Value::makeString("missing numeric type"));
    legacyListComponents.list.push_back(
        std::move(malformedLegacyComponent));
    legacyListItem.set("components", legacyListComponents);
    const auto legacyListStack = cppfm::ItemStack::fromNbt(legacyListItem);
    bool legacyListPreserved = false;
    bool legacyListWireRejected = false;
    if (legacyListStack) {
        const auto savedLegacyList = legacyListStack->toNbt(7);
        const auto* savedComponents = savedLegacyList.get("components");
        const auto* firstEntry = savedComponents &&
                savedComponents->tag == cppfm::nbt::List &&
                savedComponents->list.size() == 2
            ? &savedComponents->list.front() : nullptr;
        const auto* secondEntry = savedComponents &&
                savedComponents->tag == cppfm::nbt::List &&
                savedComponents->list.size() == 2
            ? &savedComponents->list.back() : nullptr;
        const auto* type = firstEntry ? firstEntry->get("type") : nullptr;
        const auto* payload = firstEntry ? firstEntry->get("data") : nullptr;
        const auto* futureField = firstEntry
            ? firstEntry->get("future_field") : nullptr;
        const auto* malformedPayload = secondEntry
            ? secondEntry->get("data") : nullptr;
        legacyListPreserved = legacyListStack->persistedNbt &&
            legacyListStack->persistedNbt->hasOpaqueComponents &&
            legacyListStack->components.empty() && savedComponents &&
            savedComponents->tag == cppfm::nbt::List &&
            savedComponents->list.size() == 2 && type &&
            type->tag == cppfm::nbt::Int && type->i == 11 && payload &&
            payload->tag == cppfm::nbt::ByteArray &&
            payload->byteArray == std::vector<std::uint8_t>{0x04, 0xAA, 0x00} &&
            futureField && futureField->tag == cppfm::nbt::String &&
            futureField->str == "preserve unknown fields" && secondEntry &&
            !secondEntry->get("type") && malformedPayload &&
            malformedPayload->tag == cppfm::nbt::String &&
            malformedPayload->str == "missing numeric type";
        try {
            cppfm::WriteBuffer wire;
            legacyListStack->write(wire);
        } catch (const std::runtime_error&) {
            legacyListWireRejected = true;
        }
    }
    check(legacyListPreserved && legacyListWireRejected,
          "legacy list-form item components remain opaque, lossless, and fail closed on wire serialization");

    cppfm::nbt::Value legacyListChestRoot =
        cppfm::nbt::Value::makeCompound();
    cppfm::nbt::Value legacyListChestEntities =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    cppfm::nbt::Value legacyListChest =
        cppfm::nbt::Value::makeCompound();
    legacyListChest.set("id",
        cppfm::nbt::Value::makeString("minecraft:chest"));
    legacyListChest.set("x", cppfm::nbt::Value::makeInt(36));
    legacyListChest.set("y", cppfm::nbt::Value::makeInt(64));
    legacyListChest.set("z", cppfm::nbt::Value::makeInt(-1));
    cppfm::nbt::Value legacyListChestItems =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    legacyListChestItems.list.push_back(legacyListItem);
    legacyListChest.set("Items", std::move(legacyListChestItems));
    legacyListChestEntities.list.push_back(std::move(legacyListChest));
    legacyListChestRoot.set("block_entities",
                            std::move(legacyListChestEntities));
    cppfm::BlockEntityStore legacyListChestStore;
    legacyListChestStore.readChunkNbt(legacyListChestRoot);
    const bool legacyListChestRejected =
        !legacyListChestStore.tryCreateShared(
            cppfm::posKey(36, 64, -1), cppfm::BlockEntity::Kind::Chest);
    cppfm::nbt::Value savedLegacyListChest =
        cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    legacyListChestStore.writeChunkNbt(2, -1, savedLegacyListChest);
    const auto* savedLegacyChestItems = savedLegacyListChest.list.empty()
        ? nullptr : savedLegacyListChest.list.front().get("Items");
    const auto* savedLegacyChestStack = savedLegacyChestItems &&
            savedLegacyChestItems->tag == cppfm::nbt::List &&
            savedLegacyChestItems->list.size() == 1
        ? &savedLegacyChestItems->list.front() : nullptr;
    const auto* savedLegacyChestComponents = savedLegacyChestStack
        ? savedLegacyChestStack->get("components") : nullptr;
    check(legacyListChestRejected && legacyListChestStore.containsAt(36, 64, -1) &&
              !legacyListChestStore.getShared(cppfm::posKey(36, 64, -1)) &&
              savedLegacyChestComponents &&
              savedLegacyChestComponents->tag == cppfm::nbt::List &&
              savedLegacyChestComponents->list.size() == 2,
          "legacy list-form components keep inventory containers opaque and lossless");

    cppfm::nbt::Value knownItem = cppfm::nbt::Value::makeCompound();
    knownItem.set("id", cppfm::nbt::Value::makeString(
        "minecraft:diamond_pickaxe"));
    knownItem.set("count", cppfm::nbt::Value::makeInt(1));
    cppfm::nbt::Value knownComponents = cppfm::nbt::Value::makeCompound();
    knownComponents.set("minecraft:damage", cppfm::nbt::Value::makeInt(4));
    knownItem.set("components", std::move(knownComponents));
    const auto knownStack = cppfm::ItemStack::fromNbt(knownItem);
    bool wireDoesNotInventPersistence = false;
    if (knownStack) {
        cppfm::WriteBuffer knownWire;
        knownStack->write(knownWire);
        cppfm::ReadBuffer knownInput(knownWire.data);
        const auto decodedKnown = cppfm::ItemStack::read(knownInput);
        wireDoesNotInventPersistence = knownInput.remaining() == 0 &&
            !decodedKnown.persistedNbt &&
            decodedKnown.sameKnownItemData(*knownStack);
    }
    check(wireDoesNotInventPersistence,
          "a protocol item stack does not inherit unrelated saved NBT by item-id coincidence");

    cppfm::WriteBuffer excessiveComponentCount;
    excessiveComponentCount.varint(1);
    excessiveComponentCount.varint(static_cast<std::int32_t>(diamondId));
    excessiveComponentCount.varint(
        static_cast<std::int32_t>(cppfm::ItemStack::kMaxComponentCount + 1));
    excessiveComponentCount.varint(0);
    bool excessiveCountRejected = false;
    try {
        cppfm::ReadBuffer input(excessiveComponentCount.data);
        (void)cppfm::ItemStack::read(input);
    } catch (const std::runtime_error&) {
        excessiveCountRejected = true;
    }
    cppfm::WriteBuffer negativeComponentId;
    negativeComponentId.varint(1);
    negativeComponentId.varint(static_cast<std::int32_t>(diamondId));
    negativeComponentId.varint(1);
    negativeComponentId.varint(0);
    negativeComponentId.varint(-1);
    negativeComponentId.varint(0);
    bool negativeIdRejected = false;
    try {
        cppfm::ReadBuffer input(negativeComponentId.data);
        (void)cppfm::ItemStack::read(input);
    } catch (const std::runtime_error&) {
        negativeIdRejected = true;
    }
    check(excessiveCountRejected && negativeIdRejected,
          "item component decoding rejects oversized lists and negative IDs before allocation/use");

    cppfm::WriteBuffer unknownItemId;
    unknownItemId.varint(1);
    unknownItemId.varint(std::numeric_limits<std::int32_t>::max());
    unknownItemId.varint(0);
    unknownItemId.varint(0);
    bool unknownItemRejected = false;
    try {
        cppfm::ReadBuffer input(unknownItemId.data);
        (void)cppfm::ItemStack::read(input);
    } catch (const std::runtime_error&) {
        unknownItemRejected = true;
    }
    check(unknownItemRejected,
          "item decoding rejects IDs absent from this server's negotiated registry");

    cppfm::WriteBuffer unknownNestedItemId;
    unknownNestedItemId.varint(1);
    unknownNestedItemId.varint(static_cast<std::int32_t>(diamondId));
    unknownNestedItemId.varint(1);
    unknownNestedItemId.varint(0);
    unknownNestedItemId.varint(62); // container component
    unknownNestedItemId.varint(1);  // one nested slot
    unknownNestedItemId.varint(1);  // non-empty nested stack
    unknownNestedItemId.varint(std::numeric_limits<std::int32_t>::max());
    unknownNestedItemId.varint(0);  // nested added components
    unknownNestedItemId.varint(0);  // nested removed components
    bool unknownNestedItemRejected = false;
    try {
        cppfm::ReadBuffer input(unknownNestedItemId.data);
        (void)cppfm::ItemStack::read(input);
    } catch (const std::runtime_error&) {
        unknownNestedItemRejected = true;
    }
    check(unknownNestedItemRejected,
          "container decoding rejects nested IDs absent from the negotiated item registry");

    cppfm::WriteBuffer duplicateComponentsWire;
    duplicateComponentsWire.varint(1);
    duplicateComponentsWire.varint(static_cast<std::int32_t>(diamondId));
    duplicateComponentsWire.varint(2);
    duplicateComponentsWire.varint(0);
    duplicateComponentsWire.varint(3);
    duplicateComponentsWire.u8(1);
    duplicateComponentsWire.varint(3);
    duplicateComponentsWire.u8(2);
    bool duplicateComponentRejected = false;
    try {
        cppfm::ReadBuffer input(duplicateComponentsWire.data);
        (void)cppfm::ItemStack::read(input);
    } catch (const std::runtime_error&) {
        duplicateComponentRejected = true;
    }
    cppfm::WriteBuffer overlappingComponents;
    overlappingComponents.varint(1);
    overlappingComponents.varint(static_cast<std::int32_t>(diamondId));
    overlappingComponents.varint(1);
    overlappingComponents.varint(1);
    overlappingComponents.varint(3);
    overlappingComponents.u8(1);
    overlappingComponents.varint(3);
    bool addRemoveOverlapRejected = false;
    try {
        cppfm::ReadBuffer input(overlappingComponents.data);
        (void)cppfm::ItemStack::read(input);
    } catch (const std::runtime_error&) {
        addRemoveOverlapRejected = true;
    }
    cppfm::WriteBuffer negativeItemCount;
    negativeItemCount.varint(-1);
    bool negativeCountRejected = false;
    try {
        cppfm::ReadBuffer input(negativeItemCount.data);
        (void)cppfm::ItemStack::read(input);
    } catch (const std::runtime_error&) {
        negativeCountRejected = true;
    }
    check(duplicateComponentRejected && addRemoveOverlapRejected &&
              negativeCountRejected,
          "item decoder rejects duplicate/conflicting component patches and negative counts");

    cppfm::nbt::Value reorderedItem = cppfm::nbt::Value::makeCompound();
    reorderedItem.set("id",
                      cppfm::nbt::Value::makeString("minecraft:diamond_pickaxe"));
    reorderedItem.set("count", cppfm::nbt::Value::makeInt(2));
    reorderedItem.set("Slot", cppfm::nbt::Value::makeByte(8));
    cppfm::nbt::Value reorderedComponents = cppfm::nbt::Value::makeCompound();
    cppfm::nbt::Value reorderedOpaque = cppfm::nbt::Value::makeCompound();
    reorderedOpaque.set("alpha", cppfm::nbt::Value::makeInt(1));
    reorderedOpaque.set("zeta", cppfm::nbt::Value::makeInt(2));
    reorderedComponents.set("example:opaque_component",
                            std::move(reorderedOpaque));
    reorderedComponents.set("minecraft:damage",
                            cppfm::nbt::Value::makeInt(9));
    reorderedItem.set("components", std::move(reorderedComponents));
    const auto loadedReordered = cppfm::ItemStack::fromNbt(reorderedItem);
    const bool orderIndependentOpaque = loaded && loadedReordered &&
        loaded->sameItemData(*loadedReordered);
    check(orderIndependentOpaque,
          "NBT compound field order does not change persisted stack identity");
}

} // namespace

int main() {
    testBlockEntityKindMapping();
    testBoundStoreRequiresOwningMutationGate();
    testAdmissionSnapshotFailsFastOnCrossOwnerContention();
    testContendedSnapshotPreservesExistingOutput();
    testDurableSnapshotBacksOffAndWaitsForConsistentOwnerState();
    testBlockEntityAssignmentPreservesOwnerLock();
    testPackedCoordinateOverflowCannotAliasExistingEntity();
    testFallbackTombstoneDoesNotReenterChunkIndexOnDelayedImport();
    testMultiplePreparedImportsCannotResurrectRemovedEntity();
    testWorldReplacementSurvivesTombstoneAllocationFailure();
    testBatchDirtyCallbackFailureDoesNotPublishOwners();
    testTryCreateSharedDirtyCallbackFailureIsAtomic();
    testConcurrentTryCreateSharedPublication();
    testReentrantTryCreateSharedDirtyCallbackDoesNotRecurse();
    testReentrantTryCreateSharedBatchDirtyCallbackDoesNotRecurse();
    testExternalAnvilCandidateAdmissionPublishesLiveState();
    testOpaqueBlockEntityRoundTrip();
    testDimensionYBoundsAndChunkResourceLimits();
    testLiveStoreBudgetsAndChunkEviction();
    testForgetChunkClearsDeletedTombstonesBesideLiveRecords();
    testBlockEntityIdExactnessAndUnlockedRemovalHook();
    testSameKindBlockEntityEnsureIsIdempotent();
    testTransientPistonHasOnlyAnInitialChunkProjection();
    testInitialChunkProjectionPreservesCampfireItems();
    testItemIdentityAndTransactionalInsertion();
    std::printf("block_entity_contract: %d assertions, %d failures\n", assertions, failures);
    return failures == 0 ? 0 : 1;
}
