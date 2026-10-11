#include "game/World.hpp"
#include "game/ChunkCodec.hpp"
#include "game/Anvil.hpp"
#include "game/BlockEntities.hpp"
#include "game/BlockEntityNbtProjection.hpp"
#include "game/ChunkNbtProjection.hpp"
#include "game/GameServer.hpp"
#include "game/GameServerHelpers.hpp"
#include "game/SessionInteractionSupport.hpp"
#include "game/Persistence.hpp"
#include "game/CloneTransaction.hpp"
#include "game/StairsHelper.hpp"
#include "../src/worldgen/PortalHandler.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace cppfm {
struct WorldMutationContractTestAccess {
    static bool inFlightLoadBecomesStaleAfterSave() {
        cppfm::GameServer::ChunkSaveCoordinator coordinator;
        const auto key = cppfm::GameServer::dimensionChunkKey(0, 17, -9);
        const auto load = coordinator.beginLoad(key);
        if (!load) return false;
        const auto save = coordinator.beginSave(key);
        coordinator.finishSave(key, save.stamp, save.saveId, false, [] {});
        std::unique_lock<std::mutex> stateLock;
        const bool accepted = coordinator.lockCurrentLoad(key, *load, stateLock);
        const bool rejectedWithoutLock = !stateLock.owns_lock();
        if (stateLock.owns_lock()) stateLock.unlock();
        coordinator.finishLoad(key, load->stamp);
        return !accepted && rejectedWithoutLock;
    }

    static bool loadWaitsForActiveSave() {
        cppfm::GameServer::ChunkSaveCoordinator coordinator;
        const auto key = cppfm::GameServer::dimensionChunkKey(-1, -5, 12);
        const auto save = coordinator.beginSave(key);
        const auto duringSave = coordinator.beginLoad(key);
        coordinator.finishSave(key, save.stamp, save.saveId, false, [] {});
        return !duringSave;
    }

    static bool sameRevisionSavesReceiveDistinctTickets() {
        cppfm::GameServer::ChunkSaveCoordinator coordinator;
        const auto key = cppfm::GameServer::dimensionChunkKey(1, 2, 3);
        const auto older = coordinator.beginSave(key);
        const auto newer = coordinator.beginSave(key);
        const bool distinct = older.stamp == newer.stamp &&
                              older.saveId != newer.saveId;
        int completed = 0;
        const bool pendingDuringSaves = coordinator.hasPendingWrite(key);
        coordinator.finishSave(key, older.stamp, older.saveId, true,
                               [&] { ++completed; });
        const bool olderCannotComplete = completed == 0 &&
                                         coordinator.hasPendingWrite(key);
        coordinator.finishSave(key, newer.stamp, newer.saveId, true,
                               [&] { ++completed; });
        return distinct && pendingDuringSaves && olderCannotComplete &&
               completed == 1 && !coordinator.hasPendingWrite(key);
    }

    static bool persistedSaveCompletionRunsUnlocked() {
        cppfm::GameServer::ChunkSaveCoordinator coordinator;
        const auto key = cppfm::GameServer::dimensionChunkKey(0, 4, -7);
        const auto save = coordinator.beginSave(key);
        bool callbackRan = false;
        bool coordinatorUnlocked = false;
        bool stampUnlocked = false;
        coordinator.finishSave(key, save.stamp, save.saveId, true, [&] {
            callbackRan = true;
            std::unique_lock coordinatorLock(coordinator.mutex,
                                             std::try_to_lock);
            std::unique_lock stampLock(save.stamp->stateMutex,
                                       std::try_to_lock);
            coordinatorUnlocked = coordinatorLock.owns_lock();
            stampUnlocked = stampLock.owns_lock();
        });
        return callbackRan && coordinatorUnlocked && stampUnlocked;
    }

    static cppfm::World::ChunkRevisionSnapshot captureChunkVersion(
        const cppfm::World& world, std::int32_t cx, std::int32_t cz) {
        cppfm::World::ChunkRevisionSnapshot versions;
        const auto key = cppfm::chunkKey(cx, cz);
        const bool loaded = world.withChunk(cx, cz, [&](const cppfm::Chunk& chunk) {
            versions.emplace(key, cppfm::World::ChunkVersion{
                                      chunk.revision, chunk.incarnation()});
        });
        if (!loaded) versions.emplace(key, std::nullopt);
        return versions;
    }
    static bool publishSkyLight(
        cppfm::World& world, std::int32_t cx, std::int32_t cz,
        const cppfm::World::ChunkRevisionSnapshot& versions,
        const cppfm::Chunk::LightArray& sky) {
        return world.publishSkyLightCache(cx, cz, versions, sky);
    }
    static std::size_t chunkBuildStripe(std::int32_t cx, std::int32_t cz) {
        return cppfm::World::chunkBuildStripeIndex(cppfm::chunkKey(cx, cz));
    }
    static std::uint32_t chunkBuildWaiters(const World& world) {
        return world.chunkBuildWaiters_.load(std::memory_order_acquire);
    }
    static std::uint32_t mutationOrderWaiters(const World& world) {
        return world.mutationOrderWaiters_.load(std::memory_order_acquire);
    }
    static std::uint32_t editHookWaiters(const World& world) {
        return world.editHookWaitersForTest_.load(std::memory_order_acquire);
    }
    static bool waitForPersistenceCallbackWaiter(
        Persistence& persistence, std::chrono::milliseconds timeout) {
        auto state = persistence.callbackState_;
        std::unique_lock lock(state->mutex);
        return state->quiescent.wait_for(lock, timeout, [&] {
            return state->waitersForTest != 0;
        });
    }
    static void setBeforeEditLeaseForTest(
        Persistence& persistence, std::function<void()> callback) {
        auto state = persistence.callbackState_;
        std::lock_guard lock(state->mutex);
        state->beforeEditLeaseForTest = std::move(callback);
    }
    static std::optional<std::string> biomeOverride(
        Persistence& persistence) {
        std::lock_guard lock(persistence.bioMtx_);
        return persistence.biomeOverride_;
    }
    static MutationGateLease holdMutationOrder(World& world) {
        return world.lockMutationOrder();
    }
    static bool hasPendingLoadedChunk(const World& world, std::int32_t cx,
                                     std::int32_t cz) {
        return static_cast<bool>(world.findPendingLoadedChunk(
            cppfm::chunkKey(cx, cz)));
    }
    static std::size_t pendingLoadedChunkCount(const World& world) {
        std::lock_guard lock(world.pendingLoadedMtx_);
        return world.pendingLoadedChunks_.size();
    }
    static bool injectPendingLoadedChunk(
        World& world, std::int32_t cx, std::int32_t cz, Chunk chunk,
        std::function<void()> publishCallback) {
        auto pending = std::make_shared<World::PendingLoadedChunk>();
        pending->chunk = std::make_unique<Chunk>(std::move(chunk));
        pending->publishCallback = std::move(publishCallback);
        std::lock_guard lock(world.pendingLoadedMtx_);
        return world.pendingLoadedChunks_
            .emplace(cppfm::chunkKey(cx, cz), std::move(pending))
            .second;
    }
    static bool injectResidentChunk(World& world, std::int32_t cx,
                                    std::int32_t cz, Chunk chunk) {
        auto resident = std::make_unique<Chunk>(std::move(chunk));
        std::unique_lock lock(world.mutex_);
        return world.chunks_
            .emplace(cppfm::chunkKey(cx, cz), std::move(resident))
            .second;
    }
};
} // namespace cppfm

namespace {

template <typename T>
concept HasPublicBlockArray = requires(T& value) { value.blocks_; };

template <typename T>
concept HasRawSkyLightView = requires(const T& value) { value.skyLightData(); };

template <typename T>
concept HasPublicSkyLightMember = requires(T& value) { value.skyLight_; };

template <typename T>
concept HasPublicChunkReset = requires(T& value) { value.resetForReuse(); };

template <typename T>
concept HasPublicChunkStore = requires(T& value) { value.chunks_; };

template <typename T>
concept HasMutableChunkCallback = requires(T& value) {
    value.withChunk(0, 0, [](cppfm::Chunk&) {});
};

template <typename T>
concept HasUnversionedSkyMutation = requires(T& value) {
    value.setSkyLightRaw(0, 0, 0, 0);
    value.markSkyLightReady(0, 0);
};

template <typename T>
concept HasPublicSkyLightPublisher = requires(
    T& value, const cppfm::World::ChunkRevisionSnapshot& versions,
    const cppfm::Chunk::LightArray& sky) {
    value.publishSkyLightCache(0, 0, versions, sky);
};

static_assert(!HasPublicBlockArray<cppfm::Chunk>);
static_assert(!HasRawSkyLightView<cppfm::Chunk>);
static_assert(!HasPublicSkyLightMember<cppfm::Chunk>);
static_assert(!HasPublicChunkReset<cppfm::Chunk>);
static_assert(!HasPublicChunkStore<cppfm::World>);
static_assert(!HasMutableChunkCallback<cppfm::World>);
static_assert(!HasUnversionedSkyMutation<cppfm::World>);
static_assert(!HasPublicSkyLightPublisher<cppfm::World>);
static_assert(std::is_copy_constructible_v<cppfm::Chunk>);
static_assert(!std::is_copy_constructible_v<cppfm::ChunkBuildWriter>);
static_assert(!std::is_move_constructible_v<cppfm::ChunkBuildWriter>);
static_assert(!std::is_copy_constructible_v<cppfm::ChunkBuildWriter::BlockStates>);
static_assert(!std::is_move_constructible_v<cppfm::ChunkBuildWriter::BlockStates>);
static_assert(std::is_same_v<
    decltype(std::declval<cppfm::ChunkBuildWriter::BlockStates&>().get(0)),
    std::uint16_t>);
static_assert(!std::is_reference_v<
    decltype(std::declval<cppfm::ChunkBuildWriter::BlockStates&>().get(0))>);

int failures = 0;
int assertions = 0;

void check(bool condition, const char* description) {
    ++assertions;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", description);
}

bool stopPersistenceAndWait(cppfm::Persistence& persistence) {
    for (;;) {
        const auto result = persistence.stopWithStatus();
        if (result == cppfm::Persistence::StopStatus::StoppedCleanly)
            return true;
        if (result == cppfm::Persistence::StopStatus::UnsavedData)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void checkTrace(const std::vector<std::string>& actual,
                const std::vector<std::string>& expected,
                const char* description) {
    ++assertions;
    if (actual == expected) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n expected:", description);
    for (const auto& event : expected)
        std::fprintf(stderr, " %s", event.c_str());
    std::fprintf(stderr, "\n      got:");
    for (const auto& event : actual)
        std::fprintf(stderr, " %s", event.c_str());
    std::fprintf(stderr, "\n");
}

void appendNeighbors(std::vector<std::string>& trace, int x, int y, int z) {
    static constexpr int dx[6] = {1, -1, 0, 0, 0, 0};
    static constexpr int dy[6] = {0, 0, 1, -1, 0, 0};
    static constexpr int dz[6] = {0, 0, 0, 0, 1, -1};
    for (int i = 0; i < 6; ++i) {
        const auto position = std::to_string(x + dx[i]) + "," +
                              std::to_string(y + dy[i]) + "," +
                              std::to_string(z + dz[i]);
        trace.emplace_back("neighbor:" + position);
        trace.emplace_back("neighbor-listener:" + position);
    }
}

void testSessionInteractionContextTracksPitchAndHeldItem() {
    cppfm::Player player;
    player.heldSlot = 0;
    player.inv[36] = cppfm::ItemStack::ofName("minecraft:stone");
    const auto snapshot = cppfm::session_detail::snapshotPlayerForSession(
        player);
    const auto heldItem = player.inv[36];
    check(cppfm::session_detail::interactionContextUnchanged(
              player, snapshot, heldItem),
          "session interaction accepts an unchanged player and held-item snapshot");

    player.pitch += 1.0f;
    check(!cppfm::session_detail::interactionContextUnchanged(
              player, snapshot, heldItem),
          "session interaction rejects a callback that changes player pitch");
    player.pitch = snapshot.pitch;
    --player.inv[36].count;
    check(!cppfm::session_detail::interactionContextUnchanged(
              player, snapshot, heldItem),
          "session interaction rejects a callback that changes the held stack");
}

void testOrdinaryMutationContract() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:dirt"));
    std::vector<std::string> trace;
    world.setOnBlockChanged([&](int, int, int, std::uint16_t, std::uint16_t) {
        trace.emplace_back("changed");
    });
    world.setOnBlockPlace([&](int, int, int, std::uint16_t, std::uint16_t) {
        trace.emplace_back("place-hook");
    });
    world.addOnBlockPlaceListener(
        [&](int, int, int, std::uint16_t, std::uint16_t) {
            trace.emplace_back("place-listener");
        });
    world.setOnBlockBreak([&](int, int, int, std::uint16_t, std::uint16_t) {
        trace.emplace_back("break-hook");
    });
    world.addOnBlockBreakListener(
        [&](int, int, int, std::uint16_t, std::uint16_t) {
            trace.emplace_back("break-listener");
        });
    world.setOnBlockNeighborChange([&](int x, int y, int z, std::uint16_t) {
        trace.emplace_back("neighbor:" + std::to_string(x) + "," +
                           std::to_string(y) + "," + std::to_string(z));
    });
    world.addOnBlockNeighborChangeListener(
        [&](int x, int y, int z, std::uint16_t) {
            trace.emplace_back("neighbor-listener:" + std::to_string(x) + "," +
                               std::to_string(y) + "," + std::to_string(z));
        });
    world.setOnEdit([&](int, int) { trace.emplace_back("edit"); });

    world.setBlock(8, 100, 8, stone);
    std::vector<std::string> expected{"changed", "place-hook", "place-listener"};
    appendNeighbors(expected, 8, 100, 8);
    expected.emplace_back("edit");
    checkTrace(trace, expected, "air-to-block notification order");
    check(world.revisionAt(0, 0) == 1,
          "changed gameplay write increments revision once");

    trace.clear();
    world.setBlock(8, 100, 8, stone);
    checkTrace(trace, {}, "identical state write has no callbacks");
    check(world.revisionAt(0, 0) == 1,
          "identical state write does not increment revision");

    trace.clear();
    world.setBlock(8, 100, 8, dirt);
    expected = {"changed", "neighbor:8,100,8", "neighbor-listener:8,100,8"};
    appendNeighbors(expected, 8, 100, 8);
    expected.emplace_back("edit");
    checkTrace(trace, expected, "non-air replacement notification order");
    check(world.revisionAt(0, 0) == 2,
          "replacement increments revision once");

    trace.clear();
    world.setBlock(8, 100, 8, 0);
    expected = {"changed", "break-hook", "break-listener"};
    appendNeighbors(expected, 8, 100, 8);
    expected.emplace_back("edit");
    checkTrace(trace, expected, "block-to-air notification order");
    check(world.revisionAt(0, 0) == 3,
          "block removal increments revision once");

    trace.clear();
    world.setBlock(8, 100, 8, std::numeric_limits<std::uint16_t>::max());
    checkTrace(trace, {}, "unknown block state is rejected without callbacks");
    check(world.getBlock(8, 100, 8) == 0 && world.revisionAt(0, 0) == 3,
          "unknown block state cannot mutate a chunk");
}

void testWorldMutationRegistersItsGateForCallbacks() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    std::recursive_mutex unownedMutex;
    bool rejectedMissingOwner = false;
    try {
        cppfm::MutationGateLease invalid{
            nullptr, std::unique_lock<std::recursive_mutex>{unownedMutex}};
    } catch (const std::invalid_argument&) {
        rejectedMissingOwner = true;
    }
    const bool releasedAfterRejectedConstruction = unownedMutex.try_lock();
    if (releasedAfterRejectedConstruction) unownedMutex.unlock();
    check(rejectedMissingOwner && releasedAfterRejectedConstruction,
          "an owning mutation lease rejects a missing World identity without leaking its lock");
    auto lease = world.lockMutationGate();
    check(lease.ownedByCurrentThread(),
          "mutation lease records its owning thread");
    cppfm::MutationGateLease movedLease{std::move(lease)};
    check(movedLease.ownedByCurrentThread(),
          "same-thread lease moves preserve thread ownership");
    movedLease.unlock();
    check(!cppfm::MutationGateLease::heldByCurrentThread(
              world.mutationGateIdentity()),
          "explicit lease release removes the thread-local mutation gate");
    bool callbackObservedGate = false;
    world.setOnBlockChanged(
        [&](int, int, int, std::uint16_t, std::uint16_t) {
            callbackObservedGate = cppfm::MutationGateLease::heldByCurrentThread(
                world.mutationGateIdentity());
        });

    world.setBlock(0, 100, 0, stone);

    check(callbackObservedGate,
          "internal World mutation holds a registered gate through callbacks");
}

void testBlockBatchRejectsInvalidEditsBeforeMutation() {
    using namespace cppfm;
    World nether("minecraft:nether_wastes", LevelType::Nether, 0x5A);
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    const std::array updates{
        World::BlockUpdate{8, 254, 8, stone},
        World::BlockUpdate{8, 255, 8, stone},
        World::BlockUpdate{8, 256, 8, stone},
    };
    check(!nether.setBlockBatch(updates),
          "multi-block write rejects an out-of-range member");
    check(!nether.hasChunk(0, 0) && nether.getBlock(8, 254, 8) == 0 &&
              nether.getBlock(8, 255, 8) == 0,
          "invalid multi-block write has no chunk-generation or block side effect");

    const std::array validUpdates{
        World::BlockUpdate{8, 253, 8, stone},
        World::BlockUpdate{8, 254, 8, stone},
    };
    check(nether.setBlockBatch(validUpdates) &&
              nether.getBlock(8, 253, 8) == stone &&
              nether.getBlock(8, 254, 8) == stone,
          "valid multi-block write commits every in-range state");
}

void testChunkSaveCompletionCallbacksRunUnlocked() {
    check(cppfm::WorldMutationContractTestAccess::
              persistedSaveCompletionRunsUnlocked(),
          "successful chunk-save completion runs after coordinator and stamp locks are released");
}

void testBlockBatchReadersSeeWholeCommitBeforeCallbacks() {
    using namespace cppfm;
    World world("minecraft:plains", LevelType::Flat, 0);
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:dirt"));
    constexpr std::int32_t y = 100;
    constexpr std::int32_t z = 0;
    const std::array updates{
        World::BlockUpdate{15, y, z, stone},
        World::BlockUpdate{16, y, z, dirt},
    };

    std::mutex callbackMutex;
    std::condition_variable callbackChanged;
    bool firstCallbackEntered = false;
    bool releaseFirstCallback = false;
    bool everyCallbackSawWholeBatch = true;
    std::vector<std::int32_t> callbackXs;
    world.setOnBlockChanged(
        [&](std::int32_t x, std::int32_t, std::int32_t,
            std::uint16_t, std::uint16_t) {
            if (world.getBlock(15, y, z) != stone ||
                world.getBlock(16, y, z) != dirt)
                everyCallbackSawWholeBatch = false;
            callbackXs.push_back(x);
            if (x != 15) return;
            std::unique_lock lock(callbackMutex);
            firstCallbackEntered = true;
            callbackChanged.notify_all();
            callbackChanged.wait(lock, [&] { return releaseFirstCallback; });
        });

    bool batchSucceeded = false;
    std::thread writer([&] { batchSucceeded = world.setBlockBatch(updates); });
    bool callbackEntered = false;
    {
        std::unique_lock lock(callbackMutex);
        callbackEntered = callbackChanged.wait_for(
            lock, std::chrono::seconds(10), [&] { return firstCallbackEntered; });
    }
    // The callback pauses the writer after the first notification. A separate
    // reader must still observe both chunk-boundary edits as one committed set.
    std::uint16_t leftDuringCallback = 0;
    std::uint16_t rightDuringCallback = 0;
    bool readerCompleted = false;
    std::thread reader([&] {
        leftDuringCallback = world.getBlock(15, y, z);
        rightDuringCallback = world.getBlock(16, y, z);
        {
            std::lock_guard lock(callbackMutex);
            readerCompleted = true;
        }
        callbackChanged.notify_all();
    });
    bool readerCompletedDuringCallback = false;
    if (callbackEntered) {
        std::unique_lock lock(callbackMutex);
        readerCompletedDuringCallback = callbackChanged.wait_for(
            lock, std::chrono::seconds(2), [&] { return readerCompleted; });
    }
    {
        std::lock_guard lock(callbackMutex);
        releaseFirstCallback = true;
    }
    callbackChanged.notify_all();
    writer.join();
    reader.join();

    check(callbackEntered,
          "batch mutation reaches callbacks after committing its states");
    check(readerCompletedDuringCallback && leftDuringCallback == stone &&
              rightDuringCallback == dirt,
          "concurrent getBlock readers see the complete batch while a callback is paused");
    check(batchSucceeded && everyCallbackSawWholeBatch &&
              callbackXs == std::vector<std::int32_t>{15, 16},
          "all batch callbacks run after every final state is visible");
    check(world.revisionAt(0, 0) == 1 && world.revisionAt(1, 0) == 1,
          "atomic cross-chunk batch increments each changed chunk revision once");
}

void testBlockBatchDuplicateCoordinatesUseLastStateOnce() {
    using namespace cppfm;
    World world("minecraft:plains", LevelType::Flat, 0);
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:dirt"));
    constexpr std::int32_t y = 100;
    const std::array updates{
        World::BlockUpdate{4, y, 4, stone},
        World::BlockUpdate{5, y, 4, dirt},
        World::BlockUpdate{4, y, 4, dirt},
        World::BlockUpdate{5, y, 4, stone},
    };
    std::vector<std::tuple<std::int32_t, std::uint16_t, std::uint16_t>> changes;
    world.setOnBlockChanged(
        [&](std::int32_t x, std::int32_t, std::int32_t,
            std::uint16_t oldState, std::uint16_t newState) {
            changes.emplace_back(x, oldState, newState);
        });

    check(world.setBlockBatch(updates),
          "batch with repeated coordinates is accepted");
    check(world.getBlock(4, y, 4) == dirt && world.getBlock(5, y, 4) == stone,
          "last update wins for each repeated coordinate");
    check(changes == std::vector<std::tuple<std::int32_t, std::uint16_t,
                                            std::uint16_t>>{
                         {4, 0, dirt}, {5, 0, stone}},
          "duplicate coordinates notify once in first-occurrence order using pre-batch and final states");
    check(world.revisionAt(0, 0) == 2,
          "duplicate updates increment revision once per changed unique coordinate");
}

void testConditionalAirMutationLoadsSavedTargetBeforeReplacement() {
    using namespace cppfm;
    World world("minecraft:plains", LevelType::Flat, 0);
    constexpr std::int32_t x = 17;
    constexpr std::int32_t y = 64;
    constexpr std::int32_t z = 0;
    const auto chest = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:chest"));
    const auto shulker = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:red_shulker_box"));
    world.setLoader([&](std::int32_t cx, std::int32_t cz,
                        ChunkBuildWriter& writer) {
        if (cx != (x >> 4) || cz != (z >> 4)) return false;
        const auto storageY = y - world.minY();
        writer.blocks.set(Chunk::index(storageY >> 4, storageY & 15,
                                       z & 15, x & 15), chest);
        return true;
    });

    const bool staleUnloadedReadLooksLikeAir = world.getBlock(x, y, z) == 0 &&
        !world.hasChunk(x >> 4, z >> 4);
    const bool replaced = world.setBlockIfAir(x, y, z, shulker);
    check(staleUnloadedReadLooksLikeAir && !replaced &&
              world.hasChunk(x >> 4, z >> 4) &&
              world.getBlock(x, y, z) == chest,
          "conditional placement loads a persisted chest and refuses stale unloaded-air replacement");
}

void testConditionalAirMutationAcceptsAirVariants() {
    using namespace cppfm;
    World world("minecraft:plains", LevelType::Flat, 0);
    const auto shulker = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:red_shulker_box"));
    const auto caveAir = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:cave_air"));
    const auto voidAir = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:void_air"));
    std::size_t placeEvents = 0;
    std::size_t targetNeighborEvents = 0;
    world.setOnBlockPlace(
        [&](std::int32_t x, std::int32_t, std::int32_t,
            std::uint16_t oldState, std::uint16_t newState) {
            if ((x == 2 || x == 8) && isAirBlockState(oldState) &&
                newState == shulker)
                ++placeEvents;
        });
    world.setOnBlockNeighborChange(
        [&](std::int32_t x, std::int32_t y, std::int32_t z,
            std::uint16_t neighborState) {
            if ((x == 2 || x == 8) && y == 64 && z == 0 &&
                neighborState == shulker)
                ++targetNeighborEvents;
        });

    const bool caveInitialized = world.setBlock(2, 64, 0, caveAir);
    const bool caveReplaced = caveInitialized &&
        world.setBlockIfAir(2, 64, 0, shulker);
    const bool voidInitialized = world.setBlock(8, 64, 0, voidAir);
    const bool voidReplaced = voidInitialized &&
        world.setBlockIfAir(8, 64, 0, shulker);
    check(caveReplaced && voidReplaced &&
              world.getBlock(2, 64, 0) == shulker &&
              world.getBlock(8, 64, 0) == shulker && placeEvents == 2 &&
              targetNeighborEvents == 0,
          "air-variant replacement uses the block-place hook");

    World savedWorld("minecraft:plains", LevelType::Flat, 0);
    constexpr std::int32_t savedX = 17;
    constexpr std::int32_t savedY = 64;
    savedWorld.setLoader([&](std::int32_t cx, std::int32_t cz,
                             ChunkBuildWriter& writer) {
        if (cx != (savedX >> 4) || cz != 0) return false;
        const auto storageY = savedY - savedWorld.minY();
        writer.blocks.set(Chunk::index(storageY >> 4, storageY & 15,
                                       0, savedX & 15), caveAir);
        return true;
    });
    const bool unloadedReadIsStale = savedWorld.getBlock(
        savedX, savedY, 0) == 0 && !savedWorld.hasChunk(savedX >> 4, 0);
    const bool savedCaveAirReplaced = savedWorld.setBlockIfAir(
        savedX, savedY, 0, shulker);
    check(unloadedReadIsStale && savedCaveAirReplaced &&
              savedWorld.getBlock(savedX, savedY, 0) == shulker,
          "conditional placement loads and accepts persisted cave air");
}

void testLoadedChunkMutationGateReturnsResidentLease() {
    using namespace cppfm;
    World world("minecraft:plains", LevelType::Flat, 0);
    constexpr std::int32_t cx = -3;
    constexpr std::int32_t cz = 5;
    auto lease = world.lockLoadedChunkMutationGate(cx, cz);
    if (!lease || !lease->owns_lock() || !world.hasChunk(cx, cz)) {
        check(false,
              "loaded-chunk mutation lease acquires a resident chunk");
        return;
    }

    bool erased = false;
    std::thread evictor([&] { erased = world.eraseChunk(cx, cz); });
    const auto waiterDeadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(2);
    while (WorldMutationContractTestAccess::mutationOrderWaiters(world) == 0 &&
           std::chrono::steady_clock::now() < waiterDeadline)
        std::this_thread::yield();
    const bool evictionWaited =
        WorldMutationContractTestAccess::mutationOrderWaiters(world) != 0;
    const bool residentWhileLeased = world.hasChunk(cx, cz);
    lease->unlock();
    evictor.join();

    check(evictionWaited && residentWhileLeased && erased &&
              !world.hasChunk(cx, cz),
          "loaded-chunk lease excludes eviction until compound mutation releases it");
}

void testChunkSnapshotQueuesBeforeConcurrentBlockDelta() {
    using namespace cppfm;
    World world("minecraft:plains", LevelType::Flat, 0);
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:dirt"));
    constexpr std::int32_t x = 2;
    constexpr std::int32_t y = 64;
    constexpr std::int32_t z = 3;
    world.setBlock(x, y, z, stone);

    std::mutex stateMutex;
    std::condition_variable stateChanged;
    bool snapshotCaptured = false;
    bool allowSnapshotQueue = false;
    std::mutex packetMutex;
    std::vector<std::pair<bool, std::uint16_t>> queuedPackets;

    std::thread snapshotSender([&] {
        auto lease = world.lockLoadedChunkMutationGate(0, 0);
        const auto capturedState = world.getBlock(x, y, z);
        {
            std::lock_guard lock(stateMutex);
            snapshotCaptured = true;
        }
        stateChanged.notify_all();
        {
            std::unique_lock lock(stateMutex);
            stateChanged.wait(lock, [&] { return allowSnapshotQueue; });
        }
        {
            std::lock_guard lock(packetMutex);
            queuedPackets.emplace_back(true, capturedState);
        }
        if (lease) lease->unlock();
    });

    std::thread blockEditor([&] {
        {
            std::unique_lock lock(stateMutex);
            stateChanged.wait(lock, [&] { return snapshotCaptured; });
        }
        const bool changed = world.setBlock(x, y, z, dirt);
        if (changed) {
            std::lock_guard lock(packetMutex);
            queuedPackets.emplace_back(false, dirt);
        }
    });

    const auto waiterDeadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(2);
    while (WorldMutationContractTestAccess::mutationOrderWaiters(world) == 0 &&
           std::chrono::steady_clock::now() < waiterDeadline)
        std::this_thread::yield();
    const bool editWaitedForSnapshotPublication =
        WorldMutationContractTestAccess::mutationOrderWaiters(world) != 0;
    {
        std::lock_guard lock(stateMutex);
        allowSnapshotQueue = true;
    }
    stateChanged.notify_all();
    snapshotSender.join();
    blockEditor.join();

    check(editWaitedForSnapshotPublication && queuedPackets.size() == 2 &&
              queuedPackets[0] == std::pair<bool, std::uint16_t>{true, stone} &&
              queuedPackets[1] == std::pair<bool, std::uint16_t>{false, dirt},
          "chunk snapshot enqueue under its mutation lease precedes a concurrent block delta");
}

void testCloneEntityFailureRestoresBlocksAndEntityBytes() {
    using namespace cppfm;
    World world("minecraft:plains", LevelType::Flat, 0);
    BlockEntityStore entities;
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:dirt"));
    const auto chest = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:chest"));
    constexpr std::int32_t firstX = 3;
    constexpr std::int32_t secondX = 4;
    constexpr std::int32_t blockY = 100;
    constexpr std::int32_t blockZ = 3;

    const bool chunkReady = world.generateChunkIfMissing(0, 0);
    const bool firstBlockReady = chunkReady &&
        world.setBlock(firstX, blockY, blockZ, stone);
    const bool secondBlockReady = firstBlockReady &&
        world.setBlock(secondX, blockY, blockZ, dirt);
    const auto makeEntity = [](std::int32_t x, std::string payload) {
        auto entity = nbt::Value::makeCompound();
        entity.set("id", nbt::Value::makeString("example:opaque_machine"));
        entity.set("x", nbt::Value::makeInt(x));
        entity.set("y", nbt::Value::makeInt(blockY));
        entity.set("z", nbt::Value::makeInt(blockZ));
        entity.set("TestPayload", nbt::Value::makeString(std::move(payload)));
        return entity;
    };
    const bool firstEntityReady = secondBlockReady && entities.replaceNbtAt(
        firstX, blockY, blockZ,
        makeEntity(firstX, "before-first-replacement"));
    const bool secondEntityReady = firstEntityReady && entities.replaceNbtAt(
        secondX, blockY, blockZ,
        makeEntity(secondX, "before-second-replacement"));
    check(secondEntityReady,
          "clone rollback fixture prepares both original block-entity records");
    if (!secondEntityReady) return;

    // Match the server's block-change cleanup: changing the block's registry
    // identity removes stale data, while a clone's already-staged compatible
    // typed/opaque replacement survives observer publication.
    int cloneNotifications = 0;
    world.setOnBlockChanged(
        [&](std::int32_t x, std::int32_t y, std::int32_t z,
            std::uint16_t oldState, std::uint16_t newState) {
            if ((x == firstX || x == secondX) && y == blockY && z == blockZ)
                ++cloneNotifications;
            const auto* oldBlock = gen::blockByState(oldState);
            const auto* newBlock = gen::blockByState(newState);
            if (!oldBlock || (newBlock && oldBlock->name == newBlock->name))
                return;
            const auto key = posKey(x, y, z);
            const auto replacementKind =
                blockEntityKindForBlockState(newState);
            const bool replacementAlreadyStaged = replacementKind &&
                (entities.hasCurrentKind(key, *replacementKind) ||
                 entities.hasOpaqueNbtAt(key));
            if (!replacementAlreadyStaged) entities.remove(key);
        });

    const std::vector<World::BlockUpdate> blockUpdates{
        {firstX, blockY, blockZ, chest},
        {secondX, blockY, blockZ, chest},
    };
    const std::vector<World::BlockUpdate> originalBlockStates{
        {firstX, blockY, blockZ, stone},
        {secondX, blockY, blockZ, dirt},
    };
    const std::vector<BlockEntityStore::NbtReplacement> entityUpdates{
        {firstX, blockY, blockZ,
         makeEntity(firstX, "cloned-first-entity")},
        {secondX, blockY, blockZ,
         makeEntity(secondX, "cloned-second-entity")},
    };
    check(entities.canReplaceNbtBatch(entityUpdates),
          "clone failure fixture passes the aggregate block-entity preflight");

    const auto serializeEntities = [&] {
        auto serialized = nbt::Value::makeList(nbt::Compound);
        entities.writeChunkNbt(0, 0, serialized);
        const auto coordinate = [](const nbt::Value& entity, const char* key) {
            const auto* value = entity.get(key);
            return value && value->tag == nbt::Int ? value->i : 0;
        };
        std::sort(serialized.list.begin(), serialized.list.end(),
                  [&](const nbt::Value& left, const nbt::Value& right) {
                      return std::tuple{coordinate(left, "x"),
                                        coordinate(left, "y"),
                                        coordinate(left, "z")} <
                             std::tuple{coordinate(right, "x"),
                                        coordinate(right, "y"),
                                        coordinate(right, "z")};
                  });
        auto root = nbt::Value::makeCompound();
        root.set("block_entities", std::move(serialized));
        WriteBuffer encoded;
        nbt::writeFileRoot(encoded, root);
        return encoded.data;
    };
    const auto originalEntityBytes = serializeEntities();

    int replaceCalls = 0;
    bool failureInjected = false;
    bool earlierEntityHadBeenPublished = false;
    const auto result = clone_transaction::applyStagedClone(
        world, entities, blockUpdates, originalBlockStates, entityUpdates,
        [&](const BlockEntityStore::NbtReplacement& replacement) {
            ++replaceCalls;
            // Fail the second insertion only after observing the first live
            // NBT write.
            if (replaceCalls == 2) {
                auto live = nbt::Value::makeList(nbt::Compound);
                entities.writeChunkNbt(0, 0, live);
                for (const auto& entity : live.list) {
                    const auto* x = entity.get("x");
                    const auto* payload = entity.get("TestPayload");
                    if (x && x->tag == nbt::Int && x->i == firstX &&
                        payload && payload->tag == nbt::String &&
                        payload->str == "cloned-first-entity")
                        earlierEntityHadBeenPublished = true;
                }
                failureInjected = true;
                return false;
            }
            return entities.replaceNbtAt(
                replacement.x, replacement.y, replacement.z,
                replacement.entity);
        },
        [](const std::vector<World::BlockUpdate>&,
           const std::vector<BlockEntityStore::NbtReplacement>&) {
            return true;
        });

    check(failureInjected && earlierEntityHadBeenPublished,
          "injected later replacement fails after an earlier entity was published");
    check(result == clone_transaction::ApplyResult::ApplyFailedRolledBack,
          "failed clone reports successful transactional rollback");
    check(world.getBlock(firstX, blockY, blockZ) == stone &&
              world.getBlock(secondX, blockY, blockZ) == dirt,
          "failed clone restores every destination block state");
    check(serializeEntities() == originalEntityBytes,
          "failed clone restores byte-identical serialized block-entity NBT");
    check(cloneNotifications == 0,
          "failed clone rollback publishes no transient block-change notifications");

    const auto successfulResult = clone_transaction::applyStagedClone(
        world, entities, blockUpdates, originalBlockStates, entityUpdates,
        [&](const BlockEntityStore::NbtReplacement& replacement) {
            return entities.replaceNbtAt(
                replacement.x, replacement.y, replacement.z,
                replacement.entity);
        },
        [](const std::vector<World::BlockUpdate>&,
           const std::vector<BlockEntityStore::NbtReplacement>&) {
            return true;
        });
    check(successfulResult == clone_transaction::ApplyResult::Applied &&
              cloneNotifications == 2,
          "successful clone publishes each changed block exactly once after block-entity commit");
    check(world.getBlock(firstX, blockY, blockZ) == chest &&
              world.getBlock(secondX, blockY, blockZ) == chest &&
              serializeEntities() != originalEntityBytes,
          "successful clone commits block states and block-entity NBT together");
}

void testCloneCommitIsNotReportedAsUnappliedWhenObserverCopyFails() {
    using namespace cppfm;
    struct ThrowingCopyObserver {
        std::shared_ptr<std::atomic<bool>> shouldThrow;
        explicit ThrowingCopyObserver(
            std::shared_ptr<std::atomic<bool>> flag)
            : shouldThrow(std::move(flag)) {}
        ThrowingCopyObserver(const ThrowingCopyObserver& other)
            : shouldThrow(other.shouldThrow) {
            if (shouldThrow->load(std::memory_order_relaxed))
                throw std::runtime_error("injected observer snapshot failure");
        }
        void operator()(std::int32_t, std::int32_t, std::int32_t,
                        std::uint16_t, std::uint16_t) const {}
    };

    World world("minecraft:plains", LevelType::Flat, 0);
    BlockEntityStore entities;
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    constexpr std::int32_t x = 3;
    constexpr std::int32_t y = 80;
    constexpr std::int32_t z = 3;
    auto shouldThrow = std::make_shared<std::atomic<bool>>(false);
    world.setOnBlockChanged(ThrowingCopyObserver{shouldThrow});
    shouldThrow->store(true, std::memory_order_relaxed);

    const std::vector<World::BlockUpdate> replacements{{x, y, z, stone}};
    const std::vector<World::BlockUpdate> originals{{x, y, z, 0}};
    const std::vector<BlockEntityStore::NbtReplacement> entityReplacements{
        {x, y, z, std::nullopt}};
    const auto result = clone_transaction::applyStagedClone(
        world, entities, replacements, originals, entityReplacements,
        [](const BlockEntityStore::NbtReplacement&) { return true; },
        [](const std::vector<World::BlockUpdate>&,
           const std::vector<BlockEntityStore::NbtReplacement>&) {
            return true;
        });
    check(result == clone_transaction::ApplyResult::
                        AppliedWithNotificationFailure &&
              world.getBlock(x, y, z) == stone,
          "clone reports a committed world even when an observer snapshot fails");
}

void testCloneRollbackClearsAllWhenInPlaceRestoreExceedsNbtBudget() {
    using namespace cppfm;
    World world("minecraft:plains", LevelType::Flat, 0);
    constexpr std::size_t nbtBudget = 8192;
    BlockEntityStore entities(-64, 320, 16, nbtBudget, 32, nbtBudget);
    const auto chest = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:chest"));
    constexpr std::int32_t firstX = 3;
    constexpr std::int32_t secondX = 4;
    constexpr std::int32_t blockY = 100;
    constexpr std::int32_t blockZ = 3;

    check(world.generateChunkIfMissing(0, 0) &&
              world.setBlock(firstX, blockY, blockZ, chest) &&
              world.setBlock(secondX, blockY, blockZ, chest),
          "capacity-fallback fixture prepares both destination blocks");

    const auto makeEntity = [](std::int32_t x, std::size_t payloadSize,
                               std::string marker) {
        auto entity = nbt::Value::makeCompound();
        entity.set("id", nbt::Value::makeString("example:opaque_machine"));
        entity.set("x", nbt::Value::makeInt(x));
        entity.set("y", nbt::Value::makeInt(blockY));
        entity.set("z", nbt::Value::makeInt(blockZ));
        auto& payload = entity.set("TestPayload", {});
        payload.tag = nbt::ByteArray;
        payload.byteArray.assign(payloadSize,
                                 static_cast<std::uint8_t>(marker.front()));
        entity.set("TestMarker", nbt::Value::makeString(std::move(marker)));
        return entity;
    };
    const std::vector<BlockEntityStore::NbtReplacement> originalEntities{
        {firstX, blockY, blockZ, makeEntity(firstX, 2200, "old-first")},
        {secondX, blockY, blockZ, makeEntity(secondX, 8, "old-second")},
    };
    const bool originalsReady =
        entities.canReplaceNbtBatch(originalEntities) &&
        entities.replaceNbtAt(firstX, blockY, blockZ,
                              originalEntities[0].entity) &&
        entities.replaceNbtAt(secondX, blockY, blockZ,
                              originalEntities[1].entity);
    check(originalsReady,
          "old large-plus-small block entities fit the configured NBT budget");
    if (!originalsReady) return;

    const auto serializeEntities = [&] {
        auto serialized = nbt::Value::makeList(nbt::Compound);
        check(entities.writeChunkNbt(0, 0, serialized),
              "capacity-fallback fixture snapshots chunk block-entity NBT");
        const auto coordinate = [](const nbt::Value& entity,
                                   const char* key) {
            const auto* value = entity.get(key);
            return value && value->tag == nbt::Int ? value->i : 0;
        };
        std::sort(serialized.list.begin(), serialized.list.end(),
                  [&](const nbt::Value& left, const nbt::Value& right) {
                      return std::tuple{coordinate(left, "x"),
                                        coordinate(left, "y"),
                                        coordinate(left, "z")} <
                             std::tuple{coordinate(right, "x"),
                                        coordinate(right, "y"),
                                        coordinate(right, "z")};
                  });
        auto root = nbt::Value::makeCompound();
        root.set("block_entities", std::move(serialized));
        WriteBuffer encoded;
        nbt::writeFileRoot(encoded, root);
        return encoded.data;
    };
    const auto originalEntityBytes = serializeEntities();

    const std::vector<World::BlockUpdate> blockUpdates{
        {firstX, blockY, blockZ, chest},
        {secondX, blockY, blockZ, chest},
    };
    const std::vector<World::BlockUpdate> originalBlockStates = blockUpdates;
    const std::vector<BlockEntityStore::NbtReplacement> entityUpdates{
        {firstX, blockY, blockZ, makeEntity(firstX, 8, "clone-first")},
        {secondX, blockY, blockZ, makeEntity(secondX, 2200, "clone-second")},
    };
    check(entities.canReplaceNbtBatch(entityUpdates),
          "swapping large and small block entities passes aggregate preflight");

    auto misalignedEntityUpdates = entityUpdates;
    ++misalignedEntityUpdates[1].x;
    int rejectedReplaceCalls = 0;
    int rejectedAdmissionCalls = 0;
    const auto misalignedResult = clone_transaction::applyStagedClone(
        world, entities, blockUpdates, originalBlockStates,
        misalignedEntityUpdates,
        [&](const BlockEntityStore::NbtReplacement&) {
            ++rejectedReplaceCalls;
            return true;
        },
        [&](const std::vector<World::BlockUpdate>&,
            const std::vector<BlockEntityStore::NbtReplacement>&) {
            ++rejectedAdmissionCalls;
            return true;
        });
    check(misalignedResult == clone_transaction::ApplyResult::Rejected &&
              rejectedReplaceCalls == 0 && rejectedAdmissionCalls == 0,
          "staging rejects misaligned block and entity coordinates before callbacks");
    check(serializeEntities() == originalEntityBytes,
          "rejected misaligned staging leaves prior block-entity bytes unchanged");

    std::vector<std::string> replacementOrder;
    bool failureInjectedAfterSecondPublication = false;
    bool aggregateRestoreFits = false;
    bool incrementalRestoreExceedsBudget = false;
    bool inPlaceRestoreRejectedForCapacity = false;
    const auto result = clone_transaction::applyStagedClone(
        world, entities, blockUpdates, originalBlockStates, entityUpdates,
        [&](const BlockEntityStore::NbtReplacement& replacement) {
            const auto* marker = replacement.entity
                ? replacement.entity->get("TestMarker") : nullptr;
            if (marker && marker->tag == nbt::String) {
                replacementOrder.push_back(marker->str);
            } else {
                replacementOrder.push_back(
                    replacement.x == firstX ? "clear-first" : "clear-second");
            }

            if (replacementOrder.size() == 3) {
                const std::vector<BlockEntityStore::NbtReplacement>
                    justFirstOriginal{originalEntities[0]};
                aggregateRestoreFits =
                    entities.canReplaceNbtBatch(originalEntities);
                incrementalRestoreExceedsBudget =
                    !entities.canReplaceNbtBatch(justFirstOriginal);
                const bool restored = entities.replaceNbtAt(
                    replacement.x, replacement.y, replacement.z,
                    replacement.entity);
                inPlaceRestoreRejectedForCapacity =
                    aggregateRestoreFits && incrementalRestoreExceedsBudget &&
                    !restored;
                return restored;
            }

            const bool published = entities.replaceNbtAt(
                replacement.x, replacement.y, replacement.z,
                replacement.entity);
            if (replacementOrder.size() == 2) {
                failureInjectedAfterSecondPublication = published;
                // Simulate a failure after the mutation callback has already
                // published this entity, forcing rollback to recover it.
                return false;
            }
            return published;
        },
        [](const std::vector<World::BlockUpdate>&,
           const std::vector<BlockEntityStore::NbtReplacement>&) {
            return true;
        });

    check(failureInjectedAfterSecondPublication,
          "clone fails after both swapped-footprint entities are published");
    check(aggregateRestoreFits && incrementalRestoreExceedsBudget &&
              inPlaceRestoreRejectedForCapacity,
          "restoring one large entity is rejected although the complete old set fits");
    check(replacementOrder == std::vector<std::string>{
              "clone-first", "clone-second", "old-first", "old-second",
              "clear-first", "clear-second", "old-first", "old-second"},
          "rollback takes the clear-all capacity fallback before restoring originals");
    check(result == clone_transaction::ApplyResult::ApplyFailedRolledBack,
          "capacity-fallback rollback reports a complete recovery");
    check(world.getBlock(firstX, blockY, blockZ) == chest &&
              world.getBlock(secondX, blockY, blockZ) == chest,
          "capacity-fallback rollback restores every prior block state");
    check(serializeEntities() == originalEntityBytes,
          "capacity-fallback rollback restores byte-identical prior block-entity NBT");
}

void testCloneAnvilAdmissionRejectsWithoutMutation() {
    using namespace cppfm;
    World world("minecraft:plains", LevelType::Flat, 0);
    BlockEntityStore entities;
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:dirt"));
    const auto chest = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:chest"));
    constexpr std::int32_t blockY = 100;
    constexpr std::int32_t blockZ = 3;
    constexpr std::int32_t sourceX = 2;
    constexpr std::int32_t firstDestinationX = 15;
    constexpr std::int32_t oversizedDestinationX = 16;
    check(world.generateChunkIfMissing(0, 0) &&
              world.generateChunkIfMissing(1, 0),
          "Anvil admission fixture loads both affected destination chunks");
    check(world.setBlock(sourceX, blockY, blockZ, stone) &&
              world.setBlock(firstDestinationX, blockY, blockZ, dirt) &&
              world.setBlock(oversizedDestinationX, blockY, blockZ, dirt),
          "Anvil admission fixture sets source and destination block states");

    const auto makeEntity = [](std::int32_t x, std::string marker) {
        auto entity = nbt::Value::makeCompound();
        entity.set("id", nbt::Value::makeString("example:opaque_machine"));
        entity.set("x", nbt::Value::makeInt(x));
        entity.set("y", nbt::Value::makeInt(blockY));
        entity.set("z", nbt::Value::makeInt(blockZ));
        entity.set("FutureOpaqueField", nbt::Value::makeString(
                                               std::move(marker)));
        return entity;
    };
    auto oversizedEntity = makeEntity(oversizedDestinationX,
                                     "retained-only-in-projection");
    auto& oversizedPayload = oversizedEntity.set("FutureOpaqueBytes", {});
    oversizedPayload.tag = nbt::ByteArray;
    oversizedPayload.byteArray.resize(1'150'000);
    std::mt19937 random(0x769255u);
    for (auto& byte : oversizedPayload.byteArray)
        byte = static_cast<std::uint8_t>(random() >> 24);

    check(entities.replaceNbtAt(sourceX, blockY, blockZ,
                                 makeEntity(sourceX, "source-opaque")) &&
              entities.replaceNbtAt(firstDestinationX, blockY, blockZ,
                                     makeEntity(firstDestinationX,
                                                "old-first-opaque")) &&
              entities.replaceNbtAt(oversizedDestinationX, blockY, blockZ,
                                     makeEntity(oversizedDestinationX,
                                                "old-second-opaque")),
          "Anvil admission fixture publishes source and old destination NBT");

    world.setOnBlockChanged(
        [&](std::int32_t x, std::int32_t y, std::int32_t z,
            std::uint16_t oldState, std::uint16_t newState) {
            const auto* oldBlock = gen::blockByState(oldState);
            const auto* newBlock = gen::blockByState(newState);
            if (oldBlock && (!newBlock || oldBlock->name != newBlock->name))
                entities.remove(posKey(x, y, z));
        });

    const std::vector<World::BlockUpdate> blockUpdates{
        {firstDestinationX, blockY, blockZ, chest},
        {oversizedDestinationX, blockY, blockZ, chest},
    };
    const std::vector<World::BlockUpdate> originalBlockStates{
        {firstDestinationX, blockY, blockZ, dirt},
        {oversizedDestinationX, blockY, blockZ, dirt},
    };
    const std::vector<BlockEntityStore::NbtReplacement> entityUpdates{
        {firstDestinationX, blockY, blockZ,
         makeEntity(firstDestinationX, "projected-first-opaque")},
        {oversizedDestinationX, blockY, blockZ, oversizedEntity},
    };

    const auto serializeEntities = [&] {
        auto serialized = nbt::Value::makeList(nbt::Compound);
        check(entities.writeChunkNbt(0, 0, serialized) &&
                  entities.writeChunkNbt(1, 0, serialized),
              "Anvil admission fixture snapshots both chunks' opaque NBT");
        const auto coordinate = [](const nbt::Value& entity,
                                   const char* key) {
            const auto* value = entity.get(key);
            return value && value->tag == nbt::Int ? value->i : 0;
        };
        std::sort(serialized.list.begin(), serialized.list.end(),
                  [&](const nbt::Value& left, const nbt::Value& right) {
                      return std::tuple{coordinate(left, "x"),
                                        coordinate(left, "y"),
                                        coordinate(left, "z")} <
                             std::tuple{coordinate(right, "x"),
                                        coordinate(right, "y"),
                                        coordinate(right, "z")};
                  });
        auto root = nbt::Value::makeCompound();
        root.set("block_entities", std::move(serialized));
        WriteBuffer encoded;
        nbt::writeFileRoot(encoded, root);
        return encoded.data;
    };
    const auto originalEntityBytes = serializeEntities();

    std::vector<std::pair<std::int32_t, std::int32_t>> checkedChunks;
    bool opaqueCandidateSurvivedProjection = false;
    bool oversizedProjectionRejectedByRegionLimit = false;
    int replaceCalls = 0;
    const auto result = clone_transaction::applyStagedClone(
        world, entities, blockUpdates, originalBlockStates, entityUpdates,
        [&](const BlockEntityStore::NbtReplacement& replacement) {
            ++replaceCalls;
            return entities.replaceNbtAt(replacement.x, replacement.y,
                                         replacement.z, replacement.entity);
        },
        [&](const std::vector<World::BlockUpdate>& projectedBlocks,
            const std::vector<BlockEntityStore::NbtReplacement>&
                projectedEntities) {
            std::vector<std::pair<std::int32_t, std::int32_t>> chunks;
            for (const auto& update : projectedBlocks)
                chunks.emplace_back(update.x >> 4, update.z >> 4);
            for (const auto& replacement : projectedEntities)
                chunks.emplace_back(replacement.x >> 4,
                                    replacement.z >> 4);
            std::sort(chunks.begin(), chunks.end());
            chunks.erase(std::unique(chunks.begin(), chunks.end()),
                         chunks.end());

            const auto geometry = world.verticalGeometry();
            for (const auto& [cx, cz] : chunks) {
                checkedChunks.emplace_back(cx, cz);
                auto projectedChunk = world.tryGetCopy(cx, cz);
                if (!projectedChunk) return false;
                auto root = chunkToNBT(cx, cz, *projectedChunk,
                                       "minecraft:plains", nullptr, geometry);
                std::vector<World::BlockUpdate> chunkUpdates;
                for (const auto& update : projectedBlocks) {
                    if ((update.x >> 4) == cx && (update.z >> 4) == cz)
                        chunkUpdates.push_back(update);
                }
                projectBlockUpdatesToAnvilNbt(
                    *projectedChunk, root, cx, cz, chunkUpdates, geometry);
                auto projectedEntityList = nbt::Value::makeList(nbt::Compound);
                if (!entities.writeChunkNbt(cx, cz, projectedEntityList, {},
                                            true))
                    return false;
                std::vector<const BlockEntityStore::NbtReplacement*>
                    chunkReplacements;
                for (const auto& replacement : projectedEntities) {
                    if ((replacement.x >> 4) == cx &&
                        (replacement.z >> 4) == cz)
                        chunkReplacements.push_back(&replacement);
                }
                projectBlockEntityNbtReplacements(projectedEntityList,
                                                  chunkReplacements);
                for (const auto& entity : projectedEntityList.list) {
                    const auto* x = entity.get("x");
                    const auto* opaque = entity.get("FutureOpaqueField");
                    const auto* payload = entity.get("FutureOpaqueBytes");
                    if (x && x->tag == nbt::Int &&
                        x->i == oversizedDestinationX && opaque &&
                        opaque->tag == nbt::String &&
                        opaque->str == "retained-only-in-projection" &&
                        payload && payload->tag == nbt::ByteArray &&
                        payload->byteArray.size() == 1'150'000)
                        opaqueCandidateSurvivedProjection = true;
                }
                root.set("block_entities", std::move(projectedEntityList));
                WriteBuffer bytes;
                nbt::writeFileRoot(bytes, root);
                const bool fits = RegionFile::canStoreBytes(bytes.data);
                if (!fits && cx == (oversizedDestinationX >> 4))
                    oversizedProjectionRejectedByRegionLimit = true;
                if (!fits) return false;
            }
            return true;
        });

    check(result == clone_transaction::ApplyResult::AnvilRejected,
          "clone reports Anvil admission rejection for a projected oversized chunk");
    check(checkedChunks ==
              std::vector<std::pair<std::int32_t, std::int32_t>>{{0, 0}, {1, 0}},
          "clone admission checks every destination chunk up to the rejected entry");
    check(opaqueCandidateSurvivedProjection,
          "raw candidate projection retains unknown opaque NBT fields");
    check(oversizedProjectionRejectedByRegionLimit,
          "projected chunk is rejected by RegionFile's actual 255-sector limit");
    check(replaceCalls == 0,
          "Anvil admission rejection occurs before any block-entity replacement");
    check(world.getBlock(sourceX, blockY, blockZ) == stone &&
              world.getBlock(firstDestinationX, blockY, blockZ) == dirt &&
              world.getBlock(oversizedDestinationX, blockY, blockZ) == dirt,
          "Anvil admission rejection preserves source and all destination blocks");
    check(serializeEntities() == originalEntityBytes,
          "Anvil admission rejection leaves serialized block-entity bytes unchanged");
}

void testUnknownPersistedBlockStatesAreNeverQuarantined() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-unsupported-block-state-" + std::to_string(unique));
    const auto regionPath = directory / "region" / "r.0.0.mca";
    std::filesystem::create_directories(regionPath.parent_path());

    Chunk unsupportedChunk;
    auto unsupportedRoot = chunkToNBT(0, 0, unsupportedChunk,
                                     "minecraft:plains");
    auto* sections = unsupportedRoot.get("sections");
    auto* blockStates = sections && !sections->list.empty()
        ? sections->list.front().get("block_states") : nullptr;
    auto* palette = blockStates ? blockStates->get("palette") : nullptr;
    check(palette && palette->tag == nbt::List && !palette->list.empty(),
          "unsupported block-state fixture has an Anvil palette");
    if (!palette || palette->tag != nbt::List || palette->list.empty()) {
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
        check(!cleanupError,
              "unsupported block-state fixture cleans up after setup failure");
        return;
    }
    palette->list.front().set(
        "Name", nbt::Value::makeString("example:modded_block"));
    WriteBuffer unsupportedBytes;
    nbt::writeFileRoot(unsupportedBytes, unsupportedRoot);

    Chunk stagedChunk;
    ChunkBuildWriter stagedWriter(stagedChunk);
    stagedWriter.blocks.set(Chunk::index(0, 0, 0, 0),
        static_cast<std::uint16_t>(gen::blockNameToState().at(
            "minecraft:stone")));
    WriteBuffer stagedBytes;
    nbt::writeFileRoot(stagedBytes,
        chunkToNBT(0, 0, stagedChunk, "minecraft:plains"));

    RegionFile primary(regionPath.string());
    RegionFile staged(regionPath.string() + ".new");
    primary.store(0, 0, unsupportedBytes.data);
    staged.store(0, 0, stagedBytes.data);
    const auto primaryBefore = primary.load(0, 0);
    const auto stagedBefore = staged.load(0, 0);

    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");
    Chunk decoded;
    ChunkBuildWriter decodedWriter(decoded);
    const auto status = persistence.loadChunkStatus(0, 0, decodedWriter);

    const bool sourcePreserved =
        std::filesystem::exists(regionPath) &&
        std::filesystem::exists(regionPath.string() + ".new") &&
        primary.load(0, 0) == primaryBefore &&
        staged.load(0, 0) == stagedBefore &&
        !std::filesystem::exists(regionPath.string() + ".corrupt");
    check(status == ChunkLoadStatus::Failed && sourcePreserved,
          "an unknown modded block state is rejected without quarantining or promoting over either source snapshot");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "unsupported block-state fixture removes its exact temporary directory");
}

void testUnsupportedStagedRegionIsPreservedDuringSave() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-unsupported-staged-state-" + std::to_string(unique));
    const auto regionPath = directory / "region" / "r.0.0.mca";
    std::filesystem::create_directories(regionPath.parent_path());

    Chunk validChunk;
    WriteBuffer validBytes;
    nbt::writeFileRoot(validBytes,
        chunkToNBT(0, 0, validChunk, "minecraft:plains"));
    auto unsupportedRoot = chunkToNBT(
        0, 0, validChunk, "minecraft:plains");
    auto* sections = unsupportedRoot.get("sections");
    auto* blockStates = sections && !sections->list.empty()
        ? sections->list.front().get("block_states") : nullptr;
    auto* palette = blockStates ? blockStates->get("palette") : nullptr;
    check(palette && palette->tag == nbt::List && !palette->list.empty(),
          "unsupported staged-state fixture has an Anvil palette");
    if (!palette || palette->tag != nbt::List || palette->list.empty()) {
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
        check(!cleanupError,
              "unsupported staged-state fixture cleans up after setup failure");
        return;
    }
    palette->list.front().set(
        "Name", nbt::Value::makeString("example:modded_block"));
    WriteBuffer unsupportedBytes;
    nbt::writeFileRoot(unsupportedBytes, unsupportedRoot);

    RegionFile primary(regionPath.string());
    RegionFile staged(regionPath.string() + ".new");
    primary.store(0, 0, validBytes.data);
    staged.store(0, 0, unsupportedBytes.data);
    const auto primaryBefore = primary.load(0, 0);
    const auto stagedBefore = staged.load(0, 0);

    World world("minecraft:plains", LevelType::Flat, 0);
    bool saveRejected = false;
    {
        Persistence persistence(world, directory.string(),
                               "minecraft:plains");
        Chunk otherChunk;
        ChunkBuildWriter otherWriter(otherChunk);
        WriteBuffer otherBytes;
        nbt::writeFileRoot(otherBytes,
            chunkToNBT(1, 0, otherChunk, "minecraft:plains"));
        try {
            persistence.storeChunkBytes(1, 0, otherBytes.data);
        } catch (const std::exception&) {
            saveRejected = true;
        }
    }

    check(saveRejected && std::filesystem::exists(regionPath) &&
              std::filesystem::exists(regionPath.string() + ".new") &&
              primary.load(0, 0) == primaryBefore &&
              staged.load(0, 0) == stagedBefore &&
              !std::filesystem::exists(regionPath.string() + ".corrupt"),
          "saving another chunk never quarantines or overwrites a staged region containing unsupported modded block states");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "unsupported staged-state fixture removes its exact temporary directory");
}

void testRegionFileHardLinkAliasesSerializeWriters() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-region-hardlink-lock-" + std::to_string(unique));
    std::filesystem::create_directories(directory);
    const auto originalPath = directory / "primary.mca";
    const auto aliasPath = directory / "hardlink.mca";
    RegionFile original(originalPath.string());
    const std::vector<std::uint8_t> seed{0x7f};
    original.store(31, 31, seed);
    std::error_code linkError;
    std::filesystem::create_hard_link(originalPath, aliasPath, linkError);
    if (linkError) {
        std::printf("SKIP: hard-link alias test unavailable: %s\n",
                    linkError.message().c_str());
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
        check(!cleanupError,
              "hard-link alias fixture removes its exact temporary directory");
        return;
    }

    check(RegionFile::transactionKey(originalPath.string()) ==
              RegionFile::transactionKey(aliasPath.string()),
          "whole-region persistence transactions share a registry key across hard links");
    RegionFile alias(aliasPath.string());
    std::atomic<bool> writeFailed{false};
    const auto payloadFor = [](std::int32_t x) {
        return std::vector<std::uint8_t>{
            static_cast<std::uint8_t>(x),
            static_cast<std::uint8_t>(x ^ 0xA5)};
    };
    std::thread firstWriter([&] {
        try {
            for (int pass = 0; pass < 8; ++pass)
                for (std::int32_t x = 0; x < 16; ++x)
                    original.store(x, 0, payloadFor(x));
        } catch (...) {
            writeFailed.store(true, std::memory_order_release);
        }
    });
    std::thread secondWriter([&] {
        try {
            for (int pass = 0; pass < 8; ++pass)
                for (std::int32_t x = 16; x < 32; ++x)
                    alias.store(x, 0, payloadFor(x));
        } catch (...) {
            writeFailed.store(true, std::memory_order_release);
        }
    });
    firstWriter.join();
    secondWriter.join();

    bool everySlotSurvived = !writeFailed.load(std::memory_order_acquire);
    for (std::int32_t x = 0; x < 32; ++x) {
        auto& reader = x % 2 == 0 ? original : alias;
        everySlotSurvived = everySlotSurvived &&
                            reader.load(x, 0) == payloadFor(x);
    }
    check(everySlotSurvived,
          "RegionFile writers opened through hard-link aliases preserve every concurrently updated location entry");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "hard-link alias fixture removes its exact temporary directory");
}

void testStalePersistencePublicationCallbacksAreRejected() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-stale-publication-" + std::to_string(unique));
    const auto regionPath = directory / "region" / "r.0.0.mca";
    std::filesystem::create_directories(regionPath.parent_path());

    Chunk fixture;
    WriteBuffer encoded;
    nbt::writeFileRoot(encoded,
        chunkToNBT(0, 0, fixture, "minecraft:plains"));
    RegionFile region(regionPath.string());
    region.store(0, 0, encoded.data);

    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");
    int extrasApplications = 0;
    persistence.setChunkExtras({}, [&](const nbt::Value&) {
        ++extrasApplications;
    });

    Chunk staleCandidate;
    ChunkBuildWriter staleWriter(staleCandidate);
    const auto staleLoad = persistence.loadChunkStatus(0, 0, staleWriter);
    auto stalePublish = staleWriter.takePublishCallback();
    stopPersistenceAndWait(persistence);
    world.installLoadedChunk(0, 0, std::move(staleCandidate),
                             std::move(stalePublish));
    const bool staleRejected = staleLoad == ChunkLoadStatus::Loaded &&
        !world.hasChunk(0, 0) && extrasApplications == 0;

    persistence.start();
    const bool newGenerationLoaded = world.generateChunkIfMissing(0, 0) &&
        world.hasChunk(0, 0) && extrasApplications == 1;
    stopPersistenceAndWait(persistence);
    check(staleRejected && newGenerationLoaded,
          "a deferred publication from a stopped Persistence generation is discarded and reloaded under the new generation");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "stale-publication fixture removes its exact temporary directory");
}

void testPersistenceStopRequestedByPublicationIsServiced() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-publication-deferred-stop-" + std::to_string(unique));
    const auto regionPath = directory / "region" / "r.0.0.mca";
    std::filesystem::create_directories(regionPath.parent_path());

    Chunk firstChunk;
    Chunk secondChunk;
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    ChunkBuildWriter secondWriter(secondChunk);
    secondWriter.blocks.set(Chunk::index(16, 8, 0, 0), stone);
    RegionFile region(regionPath.string());
    for (const auto [cx, chunk] :
         std::array<std::pair<std::int32_t, const Chunk*>, 2>{
             std::pair{0, &firstChunk}, std::pair{1, &secondChunk}}) {
        WriteBuffer encoded;
        nbt::writeFileRoot(encoded,
            chunkToNBT(cx, 0, *chunk, "minecraft:plains"));
        region.store(cx, 0, encoded.data);
    }

    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");
    int extrasApplications = 0;
    persistence.setChunkExtras({}, [&](const nbt::Value&) {
        ++extrasApplications;
        persistence.stop();
    });
    persistence.start();
    const bool firstRejected = !world.generateChunkIfMissing(0, 0) &&
        !world.hasChunk(0, 0);
    const bool callbackStopWasServiced =
        world.generateChunkIfMissing(1, 0) &&
        world.getBlock(16, 200, 0) != stone;
    stopPersistenceAndWait(persistence);
    check(firstRejected && callbackStopWasServiced && extrasApplications == 1,
          "stop requested inside a publication callback is serviced without publishing stale chunk state");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "deferred-stop fixture removes its exact temporary directory");
}

void testPreparedBlockEntityPublicationDoesNotLeakAcrossReentrantStop() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-prepared-extras-stop-" + std::to_string(unique));
    std::filesystem::create_directories(directory);

    Chunk fixture;
    auto root = chunkToNBT(0, 0, fixture, "minecraft:plains");
    nbt::Value blockEntities = nbt::Value::makeList(nbt::Compound);
    nbt::Value opaque = nbt::Value::makeCompound();
    opaque.set("id", nbt::Value::makeString("example:opaque"));
    opaque.set("x", nbt::Value::makeInt(2));
    opaque.set("y", nbt::Value::makeInt(80));
    opaque.set("z", nbt::Value::makeInt(3));
    blockEntities.list.push_back(std::move(opaque));
    root.set("block_entities", std::move(blockEntities));
    WriteBuffer encoded;
    nbt::writeFileRoot(encoded, root);

    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");
    BlockEntityStore liveEntities;
    int prepareCalls = 0;
    persistence.setChunkExtras(
        {}, {}, {}, [&](const nbt::Value& decodedRoot) {
            ++prepareCalls;
            // Simulate stop re-entering during detached extras preparation.
            // The resulting commit must be discarded with the stale chunk.
            persistence.stop();
            return liveEntities.prepareChunkNbt(decodedRoot);
        });
    Chunk candidate;
    ChunkBuildWriter writer(candidate);
    const bool decoded = persistence.decodeChunkBytes(
        encoded.data, writer, 0, 0);
    auto publish = writer.takePublishCallback();
    world.installLoadedChunk(0, 0, std::move(candidate), std::move(publish));
    check(decoded && prepareCalls == 1 && !world.hasChunk(0, 0) &&
              liveEntities.empty(),
          "reentrant stop discards detached block entities before they mutate the live store");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "prepared-extras stale-publication fixture removes its exact directory");
}

void testAnvilRejectsMalformedPalettes() {
    using namespace cppfm;
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    const auto makeRoot = [](std::vector<std::int64_t> blockWords,
                             std::vector<std::int64_t> biomeWords) {
        auto root = nbt::Value::makeCompound();
        root.set("Status", nbt::Value::makeString("minecraft:full"));
        root.set("xPos", nbt::Value::makeInt(0));
        root.set("zPos", nbt::Value::makeInt(0));

        auto blockPalette = nbt::Value::makeList(nbt::Compound);
        for (const auto* name : {"minecraft:stone", "minecraft:dirt"}) {
            auto entry = nbt::Value::makeCompound();
            entry.set("Name", nbt::Value::makeString(name));
            blockPalette.list.push_back(std::move(entry));
        }
        nbt::Value blockData;
        blockData.tag = nbt::LongArray;
        blockData.longArray = std::move(blockWords);
        auto blockStates = nbt::Value::makeCompound();
        blockStates.set("palette", std::move(blockPalette));
        blockStates.set("data", std::move(blockData));

        auto biomePalette = nbt::Value::makeList(nbt::String);
        for (const auto* name : {"minecraft:plains", "minecraft:desert",
                                 "minecraft:forest"})
            biomePalette.list.push_back(nbt::Value::makeString(name));
        nbt::Value biomeData;
        biomeData.tag = nbt::LongArray;
        biomeData.longArray = std::move(biomeWords);
        auto biomes = nbt::Value::makeCompound();
        biomes.set("palette", std::move(biomePalette));
        biomes.set("data", std::move(biomeData));

        auto section = nbt::Value::makeCompound();
        section.set("Y", nbt::Value::makeByte(-4));
        section.set("block_states", std::move(blockStates));
        section.set("biomes", std::move(biomes));
        auto sections = nbt::Value::makeList(nbt::Compound);
        sections.list.push_back(std::move(section));
        root.set("sections", std::move(sections));
        return root;
    };
    const auto decode = [](const nbt::Value& root) {
        Chunk detached;
        ChunkBuildWriter writer(detached);
        std::string biome;
        const std::unordered_map<std::string, std::uint32_t> biomeIds{
            {"minecraft:plains", 1}, {"minecraft:desert", 2},
            {"minecraft:forest", 3}};
        const auto resolveBiome = [](const std::string& key) -> std::int32_t {
            if (key == "minecraft:plains") return 1;
            if (key == "minecraft:desert") return 2;
            if (key == "minecraft:forest") return 3;
            return -1;
        };
        const bool accepted = chunkFromNBT(root, writer, biomeIds, biome,
                                            resolveBiome);
        const bool restored = writer.blocks.get(Chunk::index(0, 0, 0, 0)) ==
                                  gen::blockNameToState().at("minecraft:stone") &&
                              writer.biomes.get(Chunk::biomeIndex(0, 0, 0, 0)) == 1;
        return std::pair{accepted, restored};
    };

    std::vector<std::int64_t> validBlockWords(256, 0);
    const auto truncatedBlock = makeRoot({0}, {0, 0});
    check(!decode(truncatedBlock).first,
          "Anvil loader rejects a truncated block-state long array");

    auto invalidBlockWords = validBlockWords;
    invalidBlockWords[0] = 2; // first 4-bit palette index is out of range
    const auto invalidBlockIndex = makeRoot(invalidBlockWords, {0, 0});
    check(!decode(invalidBlockIndex).first,
          "Anvil loader rejects an out-of-range block palette index");

    const auto truncatedBiome = makeRoot(validBlockWords, {0});
    check(!decode(truncatedBiome).first,
          "Anvil loader rejects a truncated biome long array");

    auto duplicateInRangeSection = makeRoot(validBlockWords, {0, 0});
    auto* duplicateSections = duplicateInRangeSection.get("sections");
    auto repeatedSection = duplicateSections->list.front();
    duplicateSections->list.push_back(std::move(repeatedSection));
    check(!decode(duplicateInRangeSection).first,
          "Anvil loader rejects duplicate in-range section Y values");

    const auto invalidBiomeIndex = makeRoot(validBlockWords, {3, 0});
    check(!decode(invalidBiomeIndex).first,
          "Anvil loader rejects an out-of-range biome palette index");

    auto oversizedBlockPalette = makeRoot(validBlockWords, {0, 0});
    auto* oversizedSections = oversizedBlockPalette.get("sections");
    auto* oversizedBlockStates = oversizedSections->list.front().get(
        "block_states");
    auto* oversizedBlocks = oversizedBlockStates->get("palette");
    while (oversizedBlocks->list.size() <= 4096) {
        auto entry = nbt::Value::makeCompound();
        entry.set("Name", nbt::Value::makeString("minecraft:stone"));
        oversizedBlocks->list.push_back(std::move(entry));
    }
    check(!decode(oversizedBlockPalette).first,
          "Anvil loader rejects block palettes beyond the 4096-state section limit");

    auto oversizedBiomePalette = makeRoot(validBlockWords, {0, 0});
    auto* oversizedBiomeSections = oversizedBiomePalette.get("sections");
    auto* oversizedBiomes = oversizedBiomeSections->list.front().get("biomes");
    auto* oversizedBiomeEntries = oversizedBiomes->get("palette");
    while (oversizedBiomeEntries->list.size() <= 64)
        oversizedBiomeEntries->list.push_back(
            nbt::Value::makeString("minecraft:plains"));
    check(!decode(oversizedBiomePalette).first,
          "Anvil loader rejects biome palettes beyond the 64-entry section limit");

    auto malformedLaterSection = makeRoot(validBlockWords, {0, 0});
    auto* sections = malformedLaterSection.get("sections");
    auto lateSection = sections->list.front();
    lateSection.set("Y", nbt::Value::makeByte(-3));
    auto* lateBlockStates = lateSection.get("block_states");
    auto* lateData = lateBlockStates->get("data");
    lateData->longArray = {0};
    sections->list.push_back(std::move(lateSection));
    Chunk unchangedChunk;
    ChunkBuildWriter unchanged(unchangedChunk);
    const auto sentinelState = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:dirt"));
    unchanged.blocks.set(Chunk::index(0, 0, 0, 0), sentinelState);
    unchanged.biomes.set(Chunk::biomeIndex(0, 0, 0, 0), 42);
    std::string unchangedBiome = "unchanged";
    const std::unordered_map<std::string, std::uint32_t> biomeIds{
        {"minecraft:plains", 1}, {"minecraft:desert", 2},
        {"minecraft:forest", 3}};
    const auto resolveBiome = [](const std::string& key) -> std::int32_t {
        if (key == "minecraft:plains") return 1;
        if (key == "minecraft:desert") return 2;
        if (key == "minecraft:forest") return 3;
        return -1;
    };
    const bool lateMalformedAccepted = chunkFromNBT(
        malformedLaterSection, unchanged, biomeIds, unchangedBiome,
        resolveBiome);
    check(!lateMalformedAccepted &&
              unchanged.blocks.get(Chunk::index(0, 0, 0, 0)) == sentinelState &&
              unchanged.biomes.get(Chunk::biomeIndex(0, 0, 0, 0)) == 42 &&
              unchangedBiome == "unchanged",
          "failed multi-section Anvil decode leaves caller blocks, biomes, and biome key untouched");

    const auto validRoot = makeRoot(validBlockWords, {0, 0});
    const auto [accepted, restored] = decode(validRoot);
    check(accepted && restored,
          "Anvil loader still restores correctly packed block and biome palettes");
    auto misleadingStatus = validRoot;
    misleadingStatus.set("Status", nbt::Value::makeString("minecraft:not_full"));
    check(!decode(misleadingStatus).first,
          "Anvil rejects statuses containing the substring full but not equal to minecraft:full");
    (void)stone;
}

void testAnvilRoundTripsBlockStateProperties() {
    using namespace cppfm;
    const auto door = static_cast<std::uint16_t>(gen::stateWithPropsList(
        "minecraft:oak_door", {{"facing", "east"}, {"half", "upper"},
                                {"hinge", "right"}, {"open", "true"},
                                {"powered", "true"}}));
    const auto stairs = static_cast<std::uint16_t>(gen::stateWithPropsList(
        "minecraft:oak_stairs", {{"facing", "south"}, {"half", "top"},
                                  {"shape", "outer_left"},
                                  {"waterlogged", "true"}}));
    check(door != gen::blockNameToState().at("minecraft:oak_door") &&
              stairs != gen::blockNameToState().at("minecraft:oak_stairs"),
          "Anvil property fixture uses non-default door and stair states");

    Chunk source;
    ChunkBuildWriter sourceWriter(source);
    sourceWriter.blocks.set(Chunk::index(0, 0, 0, 0), door);
    sourceWriter.blocks.set(Chunk::index(0, 0, 0, 1), stairs);
    const auto root = chunkToNBT(0, 0, source, "minecraft:plains");
    Chunk restored;
    ChunkBuildWriter writer(restored);
    std::string biome;
    const bool decoded = chunkFromNBT(
        root, writer, {}, biome,
        [](const std::string&) -> std::int32_t { return 1; });
    check(decoded && restored.blockAt(Chunk::index(0, 0, 0, 0)) == door &&
              restored.blockAt(Chunk::index(0, 0, 0, 1)) == stairs,
          "Anvil round-trip preserves door and waterlogged-stair block state properties");

    auto malformed = root;
    auto* sections = malformed.get("sections");
    auto* blockStates = sections->list.front().get("block_states");
    auto* palette = blockStates->get("palette");
    bool editedDoor = false;
    for (auto& entry : palette->list) {
        const auto* name = entry.get("Name");
        if (!name || name->str != "minecraft:oak_door") continue;
        nbt::Value properties = nbt::Value::makeCompound();
        properties.set("facing", nbt::Value::makeString("diagonal"));
        entry.set("Properties", std::move(properties));
        editedDoor = true;
        break;
    }
    Chunk rejectedChunk;
    ChunkBuildWriter rejectedWriter(rejectedChunk);
    std::string rejectedBiome = "untouched";
    const bool acceptedMalformed = chunkFromNBT(
        malformed, rejectedWriter, {}, rejectedBiome,
        [](const std::string&) -> std::int32_t { return 1; });
    check(editedDoor && !acceptedMalformed && rejectedBiome == "untouched",
          "Anvil rejects unknown block-state property values without partial publication");

    Chunk wrongPositionChunk;
    ChunkBuildWriter wrongPositionWriter(wrongPositionChunk);
    std::string wrongPositionBiome = "untouched";
    auto wrongPosition = root;
    wrongPosition.set("xPos", nbt::Value::makeInt(1));
    check(!chunkFromNBT(
              wrongPosition, wrongPositionWriter, {}, wrongPositionBiome,
              [](const std::string&) -> std::int32_t { return 1; },
              kOverworldGeometry, std::pair{0, 0}) &&
              wrongPositionBiome == "untouched",
          "Anvil rejects a valid chunk payload stored under the wrong coordinates");
}

void testStairShapeUpdatesBroadcastInWorldDimension() {
    using namespace cppfm;
    World world("minecraft:nether_wastes", LevelType::Nether, 0x51A1);
    const auto placedStairs = static_cast<std::uint16_t>(
        gen::stateWithPropsList(
            "minecraft:oak_stairs",
            {{"facing", "east"}, {"half", "bottom"},
             {"shape", "straight"}, {"waterlogged", "false"}}));
    const auto neighborStairs = static_cast<std::uint16_t>(
        gen::stateWithPropsList(
            "minecraft:oak_stairs",
            {{"facing", "north"}, {"half", "bottom"},
             {"shape", "straight"}, {"waterlogged", "false"}}));
    const bool installed = world.setBlock(0, 64, 0, placedStairs) &&
        world.setBlock(1, 64, 0, neighborStairs) &&
        world.setBlock(1, 64, -1, placedStairs);
    std::vector<std::tuple<std::int8_t, int, int, int, std::uint16_t>> emitted;

    updateNeighborStairsShapes(
        world, 0, 64, 0,
        [&](std::int8_t dimension, int x, int y, int z,
            std::uint16_t state) {
            emitted.emplace_back(dimension, x, y, z, state);
        });

    const auto expectedShape = gen::stateWithPropsList(
        "minecraft:oak_stairs",
        {{"facing", "north"}, {"half", "bottom"},
         {"shape", "outer_right"}, {"waterlogged", "false"}});
    check(installed && world.dimensionId() == -1 &&
              world.getBlock(1, 64, 0) == expectedShape &&
              emitted.size() == 1 &&
              std::get<0>(emitted.front()) == world.dimensionId() &&
              std::get<1>(emitted.front()) == 1 &&
              std::get<2>(emitted.front()) == 64 &&
              std::get<3>(emitted.front()) == 0 &&
              std::get<4>(emitted.front()) == expectedShape,
          "neighbor-derived stair shape updates publish in the owning world dimension");
}

void testDetachedAndLoadedPublication() {
    cppfm::World generated("minecraft:plains", cppfm::LevelType::Flat, 0);
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    int generatedCallbacks = 0;
    generated.setOnBlockChanged(
        [&](int, int, int, std::uint16_t, std::uint16_t) {
            ++generatedCallbacks;
        });
    generated.setOnEdit([&](int, int) { ++generatedCallbacks; });
    generated.setLoader([&](int, int, cppfm::ChunkBuildWriter& detached) {
        detached.blocks.set(cppfm::Chunk::index(10, 4, 0, 0), stone);
        return true;
    });
    generated.generateChunkIfMissing(0, 0);
    check(generated.getBlock(0, 100, 0) == stone,
          "detached loader content is published");
    check(generated.revisionAt(0, 0) == 0,
          "generated chunk starts at revision zero");
    check(generatedCallbacks == 0,
          "detached generation emits no gameplay/edit callbacks");

    cppfm::World loaded("minecraft:plains", cppfm::LevelType::Flat, 0);
    int blockCallbacks = 0;
    int edits = 0;
    loaded.setOnBlockChanged(
        [&](int, int, int, std::uint16_t, std::uint16_t) { ++blockCallbacks; });
    loaded.setOnEdit([&](int, int) { ++edits; });
    cppfm::Chunk decoded;
    {
        cppfm::ChunkBuildWriter writer(decoded);
        writer.blocks.set(cppfm::Chunk::index(10, 4, 0, 0), stone);
    }
    loaded.installLoadedChunk(3, 4, std::move(decoded));
    check(loaded.getBlock(48, 100, 64) == stone,
          "decoded chunk is installed with its content");
    check(loaded.revisionAt(3, 4) == 1,
          "decoded-chunk installation increments revision once");
    check(blockCallbacks == 0 && edits == 0,
          "loading persisted data emits no gameplay or persistence edit callback");
}

void testSynchronousChunkPublishFailureIsRetryable() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    int callbackAttempts = 0;
    bool rejectFirstPublish = true;
    world.setLoader([&](int, int, cppfm::ChunkBuildWriter& writer) {
        writer.blocks.set(cppfm::Chunk::index(10, 4, 4, 4), stone);
        writer.setPublishCallback([&] {
            ++callbackAttempts;
            if (rejectFirstPublish)
                throw std::runtime_error("injected synchronous extras failure");
        });
        return true;
    });

    bool generated = true;
    try {
        generated = world.generateChunkIfMissing(0, 0);
    } catch (...) {
        generated = true;
    }
    check(!generated && !world.hasChunk(0, 0) && callbackAttempts == 1,
          "synchronous extras failure reports false without publishing a partial chunk");
    const bool blockChanged = world.setBlock(4, 100, 4, stone);
    const bool stateUpdated = world.updateBlockState(4, 100, 4, stone);
    check(!blockChanged && !stateUpdated && !world.hasChunk(0, 0) &&
              callbackAttempts == 3,
          "block mutations stop promptly when persisted chunk publication fails");

    rejectFirstPublish = false;
    generated = world.generateChunkIfMissing(0, 0);
    check(world.hasChunk(0, 0) && world.getBlock(4, 100, 4) == stone &&
              callbackAttempts == 4 && generated,
          "a later synchronous load retries and publishes the retained persisted chunk");
}

void testSupportMutationContract() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    const auto sand = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:sand"));
    world.setBlock(2, 100, 2, sand);
    int gameplayCallbacks = 0;
    int edits = 0;
    world.setOnBlockChanged(
        [&](int, int, int, std::uint16_t, std::uint16_t) {
            ++gameplayCallbacks;
        });
    world.setOnBlockPlace(
        [&](int, int, int, std::uint16_t, std::uint16_t) {
            ++gameplayCallbacks;
        });
    world.setOnBlockBreak(
        [&](int, int, int, std::uint16_t, std::uint16_t) {
            ++gameplayCallbacks;
        });
    world.setOnBlockNeighborChange(
        [&](int, int, int, std::uint16_t) { ++gameplayCallbacks; });
    world.setOnEdit([&](int, int) { ++edits; });
    const auto before = world.revisionAt(0, 0);

    world.scheduleNeighborUpdates(2, 99, 2);

    check(world.getBlock(2, 100, 2) == 0,
          "support update removes unsupported falling block");
    check(world.getBlock(2, -60, 2) == sand,
          "support update settles falling block on flat floor");
    check(world.revisionAt(0, 0) == before + 2,
          "support removal and settlement each increment revision");
    check(gameplayCallbacks == 0,
          "support writes do not trigger ordinary block callbacks");
    check(edits == 1,
          "support operation emits one enclosing edit callback");
}

void testReentrantMutation() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:dirt"));
    std::vector<std::string> trace;
    bool nested = false;
    world.setOnBlockChanged(
        [&](int x, int, int, std::uint16_t, std::uint16_t) {
            trace.emplace_back("changed:" + std::to_string(x) + "@" +
                               std::to_string(world.revisionAt(0, 0)));
            if (x == 0 && !nested) {
                nested = true;
                world.setBlock(1, 100, 0, dirt);
            }
        });
    world.setOnBlockPlace([&](int x, int, int, std::uint16_t, std::uint16_t) {
        trace.emplace_back("place:" + std::to_string(x) + "@" +
                           std::to_string(world.revisionAt(0, 0)));
    });
    world.setOnBlockNeighborChange(
        [&](int x, int y, int z, std::uint16_t) {
            trace.emplace_back("neighbor:" + std::to_string(x) + "," +
                               std::to_string(y) + "," + std::to_string(z) +
                               "@" + std::to_string(world.revisionAt(0, 0)));
        });
    world.setOnEdit([&](int, int) {
        trace.emplace_back("edit@" + std::to_string(world.revisionAt(0, 0)));
    });

    world.setBlock(0, 100, 0, stone);

    std::vector<std::string> expected{"changed:0@1", "changed:1@2",
                                      "place:1@2"};
    const auto appendMutationEvents = [&](int x) {
        static constexpr int dx[6] = {1, -1, 0, 0, 0, 0};
        static constexpr int dy[6] = {0, 0, 1, -1, 0, 0};
        static constexpr int dz[6] = {0, 0, 0, 0, 1, -1};
        for (int i = 0; i < 6; ++i) {
            expected.emplace_back("neighbor:" + std::to_string(x + dx[i]) +
                "," + std::to_string(100 + dy[i]) + "," +
                std::to_string(dz[i]) + "@2");
        }
        expected.emplace_back("edit@2");
    };
    appendMutationEvents(1);
    expected.emplace_back("place:0@2");
    appendMutationEvents(0);
    checkTrace(trace, expected,
               "reentrant hooks use documented depth-first callback ordering");
    check(world.revisionAt(0, 0) == 2 && world.getBlock(0, 100, 0) == stone &&
              world.getBlock(1, 100, 0) == dirt,
          "re-entrant mutation preserves both writes and revision updates");
}

void testConcurrentMutationCallbackOrder() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:dirt"));
    std::mutex mutex;
    std::condition_variable changed;
    bool firstHookEntered = false;
    bool releaseFirstHook = false;
    std::vector<std::pair<int, std::uint64_t>> callbackOrder;
    world.setOnBlockChanged(
        [&](int x, int, int, std::uint16_t, std::uint16_t) {
            std::unique_lock lock(mutex);
            callbackOrder.emplace_back(x, world.revisionAt(0, 0));
            if (x == 0) {
                firstHookEntered = true;
                changed.notify_all();
                changed.wait(lock, [&] { return releaseFirstHook; });
            }
            changed.notify_all();
        });
    std::thread first([&] { world.setBlock(0, 100, 0, stone); });
    {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return firstHookEntered; });
    }
    std::thread second([&] { world.setBlock(1, 100, 0, dirt); });

    const auto waiterDeadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(2);
    while (cppfm::WorldMutationContractTestAccess::mutationOrderWaiters(world) == 0 &&
           std::chrono::steady_clock::now() < waiterDeadline)
        std::this_thread::yield();
    const bool secondReachedOrderGate =
        cppfm::WorldMutationContractTestAccess::mutationOrderWaiters(world) > 0;
    bool secondCallbackEnteredWhileFirstBlocked = false;
    {
        std::lock_guard lock(mutex);
        secondCallbackEnteredWhileFirstBlocked = callbackOrder.size() > 1;
        releaseFirstHook = true;
        changed.notify_all();
    }
    first.join();
    second.join();

    check(secondReachedOrderGate,
          "second writer reached the mutation-order gate while first hook was held");
    check(!secondCallbackEnteredWhileFirstBlocked,
          "concurrent mutation waits for previous callback sequence");
    check(callbackOrder ==
              std::vector<std::pair<int, std::uint64_t>>{{0, 1}, {1, 2}},
          "concurrent commits and callbacks preserve revision order");
    check(world.getBlock(0, 100, 0) == stone &&
              world.getBlock(1, 100, 0) == dirt,
          "both serialized concurrent writes are committed");
}

void testDirtyMutationWinsConcurrentEviction() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    check(world.generateChunkIfMissing(0, 0),
          "dirty-eviction fixture preloads its chunk");

    std::atomic<bool> editHookEntered{false};
    std::atomic<bool> releaseEditHook{false};
    std::atomic<bool> dirty{false};
    world.setOnEdit([&](std::int32_t cx, std::int32_t cz) {
        if (cx != 0 || cz != 0) return;
        editHookEntered.store(true, std::memory_order_release);
        while (!releaseEditHook.load(std::memory_order_acquire))
            std::this_thread::yield();
        dirty.store(true, std::memory_order_release);
    });

    bool mutationSucceeded = false;
    std::thread mutator([&] {
        mutationSucceeded = world.setBlock(1, 100, 1, stone);
    });
    const auto hookDeadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(2);
    while (!editHookEntered.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < hookDeadline)
        std::this_thread::yield();

    bool erased = true;
    bool dirtyObservedByEviction = false;
    std::thread evictor;
    if (editHookEntered.load(std::memory_order_acquire)) {
        evictor = std::thread([&] {
            erased = world.eraseChunkIf(0, 0, [&] {
                dirtyObservedByEviction = dirty.load(
                    std::memory_order_acquire);
                return !dirtyObservedByEviction;
            });
        });
    }
    const auto waiterDeadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(2);
    while (cppfm::WorldMutationContractTestAccess::mutationOrderWaiters(world) == 0 &&
           std::chrono::steady_clock::now() < waiterDeadline)
        std::this_thread::yield();
    const bool evictionWaitedForEdit =
        cppfm::WorldMutationContractTestAccess::mutationOrderWaiters(world) > 0;
    releaseEditHook.store(true, std::memory_order_release);
    mutator.join();
    if (evictor.joinable()) evictor.join();
    world.setOnEdit({});

    check(evictionWaitedForEdit,
          "chunk eviction waits for a mutation that has committed but not yet marked dirty");
    check(mutationSucceeded && dirtyObservedByEviction && !erased &&
              world.hasChunk(0, 0) && world.getBlock(1, 100, 1) == stone,
          "dirty mutation prevents eviction and retains the edited resident chunk");
}

void testDirtyFlushRetriesChunkRecoveryFailure() {
    using namespace cppfm;
    World world("minecraft:plains", LevelType::Flat, 0);
    check(world.generateChunkIfMissing(0, 0),
          "dirty-retry fixture preloads its chunk before persistence starts");
    BlockEntityStore blockEntities;
    const auto key = posKey(1, 100, 1);
    auto owner = blockEntities.createShared(
        key, BlockEntity::Kind::Chest);

    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-dirty-reload-retry-" + std::to_string(unique));
    Persistence persistence(world, directory.string(), "minecraft:plains");
    std::mutex saveBarrierMutex;
    std::condition_variable saveBarrierChanged;
    bool allowRetrySave = false;
    bool saveBarrierEntered = false;
    persistence.setChunkSaveBarrier([&](std::int32_t, std::int32_t) {
        {
            std::lock_guard lock(saveBarrierMutex);
            saveBarrierEntered = true;
        }
        saveBarrierChanged.notify_all();
        std::unique_lock lock(saveBarrierMutex);
        saveBarrierChanged.wait(lock, [&] { return allowRetrySave; });
    });
    persistence.setChunkExtras(
        [&](std::int32_t cx, std::int32_t cz, nbt::Value& root) {
            nbt::Value entries = nbt::Value::makeList(nbt::Compound);
            blockEntities.writeChunkNbt(cx, cz, entries);
            if (!entries.list.empty())
                root.set("block_entities", std::move(entries));
        }, {});
    persistence.start();

    std::atomic<int> loadAttempts{0};
    world.setLoaderWithStatus(
        [&](std::int32_t cx, std::int32_t cz, ChunkBuildWriter&) {
            if (cx != 0 || cz != 0) return ChunkLoadStatus::Missing;
            if (loadAttempts.fetch_add(1, std::memory_order_acq_rel) == 0)
                return ChunkLoadStatus::Failed;
            return ChunkLoadStatus::Missing;
        });
    blockEntities.setDirtyCallback([&](std::int32_t cx, std::int32_t cz) {
        persistence.markDirty(cx, cz);
    });

    check(world.eraseChunkIf(0, 0, [] { return true; }),
          "dirty-retry fixture evicts the initially clean chunk");
    {
        std::lock_guard entityLock(*owner->stateMtx);
        owner->chest.slots[0] = ItemStack::ofName("minecraft:diamond", 5);
        blockEntities.markDirty(key);
    }
    check(!world.hasChunk(0, 0) && persistence.isDirty(0, 0),
          "post-eviction block-entity callback publishes a dirty generation without taking World locks");

    bool reachedSaveBarrier = false;
    {
        std::unique_lock lock(saveBarrierMutex);
        reachedSaveBarrier = saveBarrierChanged.wait_for(
            lock, std::chrono::seconds(10), [&] { return saveBarrierEntered; });
    }
    check(reachedSaveBarrier,
          "background persistence waits before retrying the unavailable chunk");
    {
        std::lock_guard lock(saveBarrierMutex);
        allowRetrySave = true;
    }
    saveBarrierChanged.notify_all();

    const auto recoveryDeadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(10);
    while (persistence.isDirty(0, 0) &&
           std::chrono::steady_clock::now() < recoveryDeadline)
        std::this_thread::yield();
    check(loadAttempts.load(std::memory_order_acquire) >= 2 &&
              world.hasChunk(0, 0) && !persistence.isDirty(0, 0),
          "persistence retries chunk recovery before saving a dirty generation");

    stopPersistenceAndWait(persistence);
    RegionFile saved((directory / "region" / "r.0.0.mca").string());
    const auto savedBytes = saved.load(0, 0);
    bool chestDataSaved = false;
    if (!savedBytes.empty()) {
        ReadBuffer input(savedBytes);
        nbt::Parser parser(input);
        const auto root = parser.readFileRoot();
        const auto* entries = root.get("block_entities");
        if (entries && entries->tag == nbt::List &&
            !entries->list.empty()) {
            const auto* items = entries->list.front().get("Items");
            if (items && items->tag == nbt::List && !items->list.empty()) {
                const auto* itemId = items->list.front().get("id");
                const auto* count = items->list.front().get("count");
                chestDataSaved = itemId && itemId->tag == nbt::String &&
                    itemId->str == "minecraft:diamond" && count &&
                    count->tag == nbt::Int && count->i == 5;
            }
        }
    }
    check(chestDataSaved,
          "dirty-retry snapshot durably contains the mutated chest inventory");
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "dirty-retry fixture removes its exact temporary world directory");
}

void testForcedChunkWinsConcurrentEviction() {
    using Access = cppfm::WorldMutationContractTestAccess;
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    check(world.generateChunkIfMissing(0, 0),
          "forced-eviction fixture preloads its chunk");
    auto heldMutationOrder = Access::holdMutationOrder(world);

    bool erased = true;
    std::thread evictor([&] {
        erased = world.eraseChunkIf(0, 0, [&] {
            return !world.isForcedKey(cppfm::chunkKey(0, 0)) &&
                   world.ticketLevel(0, 0) >
                       cppfm::constants::kTicketLevelSpawn;
        });
    });
    const auto waiterDeadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(2);
    while (Access::mutationOrderWaiters(world) == 0 &&
           std::chrono::steady_clock::now() < waiterDeadline)
        std::this_thread::yield();
    const bool evictionWaited = Access::mutationOrderWaiters(world) > 0;
    const bool forced = world.setChunkForced(0, 0, true);
    heldMutationOrder.unlock();
    evictor.join();

    check(evictionWaited,
          "eviction waits at the shared gate while a force ticket is added");
    check(forced && !erased && world.hasChunk(0, 0) && world.isForced(0, 0),
          "a newly forced chunk is retained by the final eviction check");
}

void testReentrantWriteDoesNotInvertChunkBuildLockOrder() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:dirt"));
    const auto emerald = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:emerald_block"));
    check(world.generateChunkIfMissing(0, 0),
          "reentrant lock-order fixture preloads the source chunk");

    std::mutex mutex;
    std::condition_variable changed;
    bool firstLoaderEntered = false;
    bool callbackEntered = false;
    bool abortLoaderWait = false;
    bool loaderWaitReleasedByHook = false;
    std::atomic<int> loaderCalls{0};
    std::atomic<int> publishCallbackCalls{0};
    world.setLoaderWithStatus([&](std::int32_t cx, std::int32_t cz,
                                  cppfm::ChunkBuildWriter& writer) {
        if (cx != 1 || cz != 0) return cppfm::ChunkLoadStatus::Missing;
        const int call = loaderCalls.fetch_add(1, std::memory_order_acq_rel);
        if (call != 0) return cppfm::ChunkLoadStatus::Missing;
        writer.setPublishCallback([&] {
            publishCallbackCalls.fetch_add(1, std::memory_order_acq_rel);
        });
        bool hookReleased = false;
        {
            std::unique_lock lock(mutex);
            firstLoaderEntered = true;
            changed.notify_all();
            hookReleased = changed.wait_for(
                lock, std::chrono::seconds(10), [&] {
                    return callbackEntered || abortLoaderWait;
                });
            loaderWaitReleasedByHook =
                hookReleased && callbackEntered && !abortLoaderWait;
        }
        if (!loaderWaitReleasedByHook) return cppfm::ChunkLoadStatus::Failed;
        writer.blocks.set(cppfm::Chunk::index(10, 0, 0, 0), emerald);
        return cppfm::ChunkLoadStatus::Loaded;
    });
    world.setOnBlockChanged(
        [&](int x, int, int, std::uint16_t oldState,
            std::uint16_t newState) {
            if (x != 15 || oldState != 0 || newState != stone) return;
            {
                std::lock_guard lock(mutex);
                callbackEntered = true;
                changed.notify_all();
            }
            (void)world.setBlock(16, 100, 0, dirt);
        });

    std::atomic<bool> generateSucceeded{false};
    std::atomic<bool> outerWriteSucceeded{false};
    std::thread generator([&] {
        generateSucceeded.store(world.generateChunkIfMissing(1, 0),
                                std::memory_order_release);
    });
    bool loaderWaitedWithBuildLock = false;
    {
        std::unique_lock lock(mutex);
        loaderWaitedWithBuildLock = changed.wait_for(
            lock, std::chrono::seconds(10), [&] { return firstLoaderEntered; });
    }
    std::thread writer;
    if (loaderWaitedWithBuildLock) {
        writer = std::thread([&] {
            outerWriteSucceeded.store(world.setBlock(15, 100, 0, stone),
                                      std::memory_order_release);
        });
    } else {
        {
            std::lock_guard lock(mutex);
            abortLoaderWait = true;
            changed.notify_all();
        }
    }
    generator.join();
    if (writer.joinable()) writer.join();
    check(loaderWaitedWithBuildLock,
          "competing chunk loader held its build stripe before the block hook");
    check(loaderWaitReleasedByHook,
          "the bounded loader wait was released by the re-entrant block hook");
    check(generateSucceeded.load(std::memory_order_acquire) &&
              outerWriteSucceeded.load(std::memory_order_acquire) &&
              world.getBlock(15, 100, 0) == stone &&
              world.getBlock(16, 100, 0) == dirt,
          "re-entrant cross-chunk write completes without a build/order lock cycle or lost mutation");
    check(loaderCalls.load(std::memory_order_acquire) == 1,
          "re-entrant load adopts the pending persisted candidate without a second loader call");
    check(world.getBlock(16, 96, 0) == emerald,
          "persisted emerald survives re-entrant chunk publication");
    check(publishCallbackCalls.load(std::memory_order_acquire) == 1,
          "accepted persisted candidate fires its publish callback exactly once");
}

void testResidentPublicationDiscardsPendingLoadedCandidate() {
    const auto emerald = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:emerald_block"));
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:dirt"));
    const auto makeChunk = [](std::uint16_t state) {
        cppfm::Chunk chunk;
        cppfm::ChunkBuildWriter writer(chunk);
        writer.blocks.set(cppfm::Chunk::index(10, 4, 0, 0), state);
        return chunk;
    };
    const auto waitFor = [](auto&& condition) {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(2);
        while (!condition() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        return condition();
    };
    using Access = cppfm::WorldMutationContractTestAccess;

    // A request that first sees a pending candidate drops the build stripe to
    // acquire mutation order. Make a resident authoritative during that gap.
    {
        cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
        constexpr int cx = 31;
        std::atomic<int> callbackCalls{0};
        auto mutationOrder = Access::holdMutationOrder(world);
        const bool pendingInserted = Access::injectPendingLoadedChunk(
            world, cx, 0, makeChunk(emerald),
            [&] { callbackCalls.fetch_add(1, std::memory_order_relaxed); });
        std::atomic<bool> generated{false};
        std::thread caller([&] {
            generated.store(world.generateChunkIfMissing(cx, 0),
                            std::memory_order_release);
        });
        const bool reachedMutationGate = waitFor([&] {
            return Access::mutationOrderWaiters(world) > 0;
        });
        const bool residentInserted =
            Access::injectResidentChunk(world, cx, 0, makeChunk(stone));
        mutationOrder.unlock();
        caller.join();
        check(pendingInserted && reachedMutationGate && residentInserted &&
                  generated.load(std::memory_order_acquire),
              "pending-first resident race reaches the ordered publication gate");
        check(!Access::hasPendingLoadedChunk(world, cx, 0) &&
                  callbackCalls.load(std::memory_order_relaxed) == 0 &&
                  world.getBlock(cx * 16, 100, 0) == stone,
              "resident early return discards only the stale pending candidate without firing its callback");
    }

    // Exercise the loaded-candidate branch: a persisted candidate is retained,
    // but a resident version wins before its owner reacquires mutation order.
    {
        cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
        constexpr int cx = 32;
        std::atomic<int> callbackCalls{0};
        auto mutationOrder = Access::holdMutationOrder(world);
        world.setLoaderWithStatus([&](std::int32_t, std::int32_t,
                                      cppfm::ChunkBuildWriter& writer) {
            writer.blocks.set(cppfm::Chunk::index(10, 4, 0, 0), emerald);
            writer.setPublishCallback([&] {
                callbackCalls.fetch_add(1, std::memory_order_relaxed);
            });
            return cppfm::ChunkLoadStatus::Loaded;
        });
        std::atomic<bool> generated{false};
        std::thread caller([&] {
            generated.store(world.generateChunkIfMissing(cx, 0),
                            std::memory_order_release);
        });
        const bool retainedAndWaiting = waitFor([&] {
            return Access::hasPendingLoadedChunk(world, cx, 0) &&
                   Access::mutationOrderWaiters(world) > 0;
        });
        const bool residentInserted =
            Access::injectResidentChunk(world, cx, 0, makeChunk(dirt));
        mutationOrder.unlock();
        caller.join();
        check(retainedAndWaiting && residentInserted &&
                  generated.load(std::memory_order_acquire),
              "loaded-path resident race is synchronized before publication");
        check(!Access::hasPendingLoadedChunk(world, cx, 0) &&
                  callbackCalls.load(std::memory_order_relaxed) == 0 &&
                  world.getBlock(cx * 16, 100, 0) == dirt,
              "loaded-path resident early return discards candidate and suppresses stale callback");
    }

    // A fallback releases its build stripe before waiting for mutation order.
    // Insert a concurrently decoded candidate and a resident while it waits.
    {
        cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
        constexpr int cx = 33;
        std::atomic<int> callbackCalls{0};
        auto mutationOrder = Access::holdMutationOrder(world);
        world.setLoaderWithStatus([](std::int32_t, std::int32_t,
                                     cppfm::ChunkBuildWriter&) {
            return cppfm::ChunkLoadStatus::Missing;
        });
        std::atomic<bool> generated{false};
        std::thread caller([&] {
            generated.store(world.generateChunkIfMissing(cx, 0),
                            std::memory_order_release);
        });
        const bool fallbackWaiting = waitFor([&] {
            return Access::mutationOrderWaiters(world) > 0;
        });
        const bool pendingInserted = Access::injectPendingLoadedChunk(
            world, cx, 0, makeChunk(emerald),
            [&] { callbackCalls.fetch_add(1, std::memory_order_relaxed); });
        const bool residentInserted =
            Access::injectResidentChunk(world, cx, 0, makeChunk(stone));
        mutationOrder.unlock();
        caller.join();
        check(fallbackWaiting && pendingInserted && residentInserted &&
                  generated.load(std::memory_order_acquire),
              "fallback-path resident race is synchronized after generation");
        check(!Access::hasPendingLoadedChunk(world, cx, 0) &&
                  callbackCalls.load(std::memory_order_relaxed) == 0 &&
                  world.getBlock(cx * 16, 100, 0) == stone,
              "fallback-path resident early return discards pending data without stale callback");
    }
}

void testConcurrentChunkLoadPublication() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    constexpr std::size_t callerCount = 4;
    const auto savedState = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:emerald_block"));
    constexpr std::size_t savedIndex =
        cppfm::Chunk::index(10, 4, 4, 4); // y=100
    std::atomic<int> loaderCalls{0};
    std::mutex mutex;
    std::condition_variable changed;
    bool firstLoaderEntered = false;
    bool releaseFirstLoader = false;
    world.setLoader([&](std::int32_t, std::int32_t,
                        cppfm::ChunkBuildWriter& writer) {
        const int call = loaderCalls.fetch_add(1, std::memory_order_acq_rel);
        if (call != 0) return false;
        std::unique_lock lock(mutex);
        firstLoaderEntered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return releaseFirstLoader; });
        writer.blocks.set(savedIndex, savedState);
        return true;
    });

    std::barrier start(static_cast<std::ptrdiff_t>(callerCount + 1));
    std::array<std::thread, callerCount> callers;
    for (auto& caller : callers) {
        caller = std::thread([&] {
            start.arrive_and_wait();
            world.generateChunkIfMissing(0, 0);
        });
    }
    start.arrive_and_wait();
    {
        std::unique_lock lock(mutex);
        changed.wait(lock, [&] { return firstLoaderEntered; });
    }

    const auto waiterDeadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(2);
    while (cppfm::WorldMutationContractTestAccess::chunkBuildWaiters(world) <
               callerCount - 1 &&
           std::chrono::steady_clock::now() < waiterDeadline)
        std::this_thread::yield();
    const bool allOtherCallersQueued =
        cppfm::WorldMutationContractTestAccess::chunkBuildWaiters(world) >=
        callerCount - 1;
    const int callsBeforeRelease = loaderCalls.load(std::memory_order_acquire);
    {
        std::lock_guard lock(mutex);
        releaseFirstLoader = true;
        changed.notify_all();
    }
    for (auto& caller : callers) caller.join();

    check(allOtherCallersQueued,
          "concurrent requests are waiting behind the same chunk build");
    check(callsBeforeRelease == 1 &&
              loaderCalls.load(std::memory_order_acquire) == 1,
          "one persisted-chunk loader owns a same-key build flight");
    check(world.getBlock(4, 100, 4) == savedState,
          "persisted chunk wins over concurrent generation fallback");
}

void testLateLoadDoesNotOverwriteEdit() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    world.generateChunkIfMissing(0, 0);
    const auto marker = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    world.setBlock(4, 100, 4, marker);

    cppfm::Chunk staleDiskChunk;
    cppfm::ChunkBuildWriter writer(staleDiskChunk);
    writer.blocks.set(cppfm::Chunk::index(10, 4, 4, 4), 0);
    world.installLoadedChunk(0, 0, std::move(staleDiskChunk));
    check(world.getBlock(4, 100, 4) == marker,
          "late async disk completion cannot overwrite a live block edit");
}

void testLateLoadDoesNotReplaceGeneratedChunk() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    world.generateChunkIfMissing(0, 0);
    const auto generatedRevision = world.revisionAt(0, 0);
    cppfm::Chunk staleDiskChunk;
    cppfm::ChunkBuildWriter writer(staleDiskChunk);
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    writer.blocks.set(cppfm::Chunk::index(10, 4, 4, 4), stone);
    bool publishSideEffectRan = false;
    world.installLoadedChunk(0, 0, std::move(staleDiskChunk), [&] {
        publishSideEffectRan = true;
    });
    check(generatedRevision == 0 && world.revisionAt(0, 0) == 0 &&
              world.getBlock(4, 100, 4) == 0,
          "late persisted data cannot replace a resident generated chunk");
    check(!publishSideEffectRan,
          "rejected late data does not publish its block-entity side effects");
}

void testSkyLightSnapshotOwnership() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    world.generateChunkIfMissing(0, 0);
    const auto sourceChunks =
        cppfm::WorldMutationContractTestAccess::captureChunkVersion(world, 0, 0);
    cppfm::Chunk::LightArray sky{};
    const auto index = cppfm::Chunk::index(0, 0, 0, 0);
    cppfm::Chunk::setNibble(sky, index, 11);
    check(cppfm::WorldMutationContractTestAccess::publishSkyLight(
              world, 0, 0, sourceChunks, sky),
          "current block-content generation accepts complete light result");

    std::uint8_t observed = 0;
    const bool found = world.withChunk(0, 0, [&](const cppfm::Chunk& chunk) {
        if (chunk.skyLightReady() && chunk.hasSkyLightStorage())
            observed = chunk.skyLightAt(index);
    });
    check(found && observed == 11 && world.hasSkyLightCache(0, 0),
          "published light is readable through the borrowed const view");

    const auto snapshot = world.tryGetCopy(0, 0);
    check(snapshot.has_value(), "world returns an owned value snapshot");
    if (!snapshot) return;
    const cppfm::Chunk ordinaryCopy = *snapshot;
    cppfm::Chunk::LightArray replacement{};
    cppfm::Chunk::setNibble(replacement, index, 4);
    world.generateChunkIfMissing(8, 8);
    check(cppfm::WorldMutationContractTestAccess::publishSkyLight(
              world, 0, 0, sourceChunks, replacement),
          "same source generation can replace derived light cache");
    check(snapshot && snapshot->skyLightAt(index) == 11 &&
              ordinaryCopy.skyLightAt(index) == 11 &&
              world.getSkyLight(0, cppfm::kMinY, 0) == 4,
          "both explicit and ordinary Chunk copies own stable light values");

    const auto staleSourceChunks = sourceChunks;
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    world.setBlock(1, 100, 1, stone);
    world.invalidateSkyLight(0, 0);
    check(!cppfm::WorldMutationContractTestAccess::publishSkyLight(
              world, 0, 0, staleSourceChunks, sky) &&
              !world.hasSkyLightCache(0, 0),
          "light calculated before a block mutation cannot become ready again");

    cppfm::World replaced("minecraft:plains", cppfm::LevelType::Flat, 0);
    replaced.generateChunkIfMissing(0, 0);
    const auto oldIncarnation =
        cppfm::WorldMutationContractTestAccess::captureChunkVersion(replaced, 0, 0);
    check(replaced.eraseChunk(0, 0), "chunk is evicted before identity regression");
    replaced.generateChunkIfMissing(0, 0);
    const auto newIncarnation =
        cppfm::WorldMutationContractTestAccess::captureChunkVersion(replaced, 0, 0);
    check(oldIncarnation.at(cppfm::chunkKey(0, 0))->revision ==
              newIncarnation.at(cppfm::chunkKey(0, 0))->revision &&
              oldIncarnation.at(cppfm::chunkKey(0, 0))->incarnation !=
                  newIncarnation.at(cppfm::chunkKey(0, 0))->incarnation,
          "reloaded chunk identity changes even when content revision resets");
    check(!cppfm::WorldMutationContractTestAccess::publishSkyLight(
              replaced, 0, 0, oldIncarnation, sky) &&
              cppfm::WorldMutationContractTestAccess::publishSkyLight(
                  replaced, 0, 0, newIncarnation, sky),
          "stale lighting from an evicted chunk cannot pass revision ABA");
}

void testChunkBuildStripeDistribution() {
    using Access = cppfm::WorldMutationContractTestAccess;
    check(Access::chunkBuildStripe(0, 3) != Access::chunkBuildStripe(1, 3),
          "same-row neighboring chunks are not forced onto one build stripe");
    const std::pair<std::int32_t, std::int32_t> extremes{
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max()};
    check(cppfm::chunkKeyDecode(cppfm::chunkKey(extremes.first,
                                                extremes.second)) == extremes,
          "packed chunk coordinates round-trip the full signed int32 domain");
}

void testSeedChangeRejectsAnInFlightChunkLoad() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 17);
    constexpr std::uint64_t selectedSeed = 0x5151ULL;
    constexpr std::uint64_t racingSeed = 0x6161ULL;
    const bool initialSeedAccepted = world.setSeed(selectedSeed);

    std::mutex mutex;
    std::condition_variable changed;
    bool loaderEntered = false;
    bool releaseLoader = false;
    world.setLoaderWithStatus(
        [&](std::int32_t, std::int32_t, cppfm::ChunkBuildWriter&) {
            std::unique_lock lock(mutex);
            loaderEntered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return releaseLoader; });
            return cppfm::ChunkLoadStatus::Failed;
        });
    std::thread loading([&] { (void)world.generateChunkIfMissing(8, -8); });
    bool observedLoader = false;
    {
        std::unique_lock lock(mutex);
        observedLoader = changed.wait_for(
            lock, std::chrono::seconds(2), [&] { return loaderEntered; });
    }
    const bool seedChangeAccepted = observedLoader && world.setSeed(racingSeed);
    {
        std::lock_guard lock(mutex);
        releaseLoader = true;
    }
    changed.notify_all();
    loading.join();

    check(initialSeedAccepted && observedLoader && !seedChangeAccepted &&
              world.seed() == selectedSeed,
          "seed replacement is rejected after a detached chunk load has started");
}

void testSeedChangeRejectsAnAsyncPersistenceRead() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 23);
    constexpr std::uint64_t selectedSeed = 0x7171ULL;
    constexpr std::uint64_t racingSeed = 0x8181ULL;
    const bool initialSeedAccepted = world.setSeed(selectedSeed);
    std::mutex mutex;
    std::condition_variable changed;
    bool readStarted = false;
    bool releaseRead = false;
    std::thread reading([&] {
        world.markChunkLoadStarted();
        std::unique_lock lock(mutex);
        readStarted = true;
        changed.notify_all();
        changed.wait(lock, [&] { return releaseRead; });
    });
    bool observedRead = false;
    {
        std::unique_lock lock(mutex);
        observedRead = changed.wait_for(
            lock, std::chrono::seconds(2), [&] { return readStarted; });
    }
    const bool seedChangeAccepted = observedRead && world.setSeed(racingSeed);
    {
        std::lock_guard lock(mutex);
        releaseRead = true;
    }
    changed.notify_all();
    reading.join();
    check(initialSeedAccepted && observedRead && !seedChangeAccepted &&
              world.seed() == selectedSeed,
          "seed replacement is rejected once an asynchronous disk read starts");
}

void testWorldBlockReplacementDoesNotRetainStaleBlockEntity() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    const auto chest = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:chest"));
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    constexpr std::int32_t x = 4;
    constexpr std::int32_t y = 64;
    constexpr std::int32_t z = 4;
    const auto key = cppfm::posKey(x, y, z);
    world.setBlock(x, y, z, chest);

    cppfm::BlockEntityStore blockEntities;
    blockEntities.createShared(key, cppfm::BlockEntity::Kind::Chest);
    blockEntities.setDirtyCallback([](std::int32_t, std::int32_t) {
        throw std::runtime_error("injected dirty admission failure");
    });
    bool dirtyAccepted = true;
    int dirtyWorldEdits = 0;
    world.setOnBlockChanged(
        [&](std::int32_t changedX, std::int32_t changedY,
            std::int32_t changedZ, std::uint16_t oldState,
            std::uint16_t newState) {
            if (changedX == x && changedY == y && changedZ == z &&
                oldState == chest && newState == stone)
                dirtyAccepted =
                    blockEntities.removeAfterWorldBlockReplacement(key);
        });
    world.setOnEdit([&](std::int32_t, std::int32_t) {
        ++dirtyWorldEdits;
    });
    world.setBlock(x, y, z, stone);

    check(!dirtyAccepted && blockEntities.empty() &&
              world.getBlock(x, y, z) == stone && dirtyWorldEdits == 1,
          "failed BE dirty admission cannot leave a stale entity or skip the block edit dirty callback");
}

void testChunkIoGenerationFence() {
    using Access = cppfm::WorldMutationContractTestAccess;
    check(Access::inFlightLoadBecomesStaleAfterSave(),
          "async chunk bytes read before a save cannot publish afterward");
    check(Access::loadWaitsForActiveSave(),
          "a new async chunk read waits until same-key saving completes");
    check(Access::sameRevisionSavesReceiveDistinctTickets(),
          "same-revision block-entity snapshots retain newest-save ordering");
}

void testPersistenceDirtyGenerationFence() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-dirty-generation-" + std::to_string(unique));
    cppfm::Persistence persistence(world, directory.string(),
                                   "minecraft:plains");
    persistence.start();
    persistence.markDirty(11, -7);
    const auto first = persistence.dirtyVersion(11, -7);
    persistence.markDirty(11, -7);
    const auto second = persistence.dirtyVersion(11, -7);
    check(first != 0 && second != 0 && first != second,
          "every dirty event receives a distinct persistence generation");
    check(!persistence.markCleanIfVersion(11, -7, first) &&
              persistence.isDirty(11, -7),
          "completion of an older snapshot cannot clear a newer dirty edit");
    check(persistence.markCleanIfVersion(11, -7, second) &&
              !persistence.isDirty(11, -7),
          "only the matching durable snapshot clears its dirty generation");
    stopPersistenceAndWait(persistence);
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "persistence dirty-generation fixture removes its exact temp directory");
}

void testPersistenceCallbacksAreConcurrentAndStopWaits() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-persistence-callback-leases-" + std::to_string(unique));
    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");

    constexpr std::int32_t firstCx = 0;
    constexpr std::int32_t cz = 0;
    std::int32_t secondCx = 32;
    while (WorldMutationContractTestAccess::chunkBuildStripe(firstCx, cz) ==
           WorldMutationContractTestAccess::chunkBuildStripe(secondCx, cz))
        secondCx += 32;

    const auto encodeChunk = [](std::int32_t cx, std::int32_t chunkZ) {
        Chunk chunk;
        ChunkBuildWriter writer(chunk);
        auto root = chunkToNBT(cx, chunkZ, chunk, "minecraft:plains");
        WriteBuffer encoded;
        nbt::writeFileRoot(encoded, root);
        return encoded.data;
    };
    persistence.storeChunkBytes(firstCx, cz, encodeChunk(firstCx, cz));
    persistence.storeChunkBytes(secondCx, cz, encodeChunk(secondCx, cz));

    std::mutex validationMutex;
    std::condition_variable validationChanged;
    int validationCalls = 0;
    int activeValidations = 0;
    int maximumActiveValidations = 0;
    bool holdValidation = true;
    bool releaseValidation = false;
    persistence.setChunkExtras({}, {}, [&](const nbt::Value&) {
        std::unique_lock lock(validationMutex);
        ++validationCalls;
        ++activeValidations;
        maximumActiveValidations =
            std::max(maximumActiveValidations, activeValidations);
        validationChanged.notify_all();
        validationChanged.wait(lock, [&] {
            return !holdValidation || releaseValidation;
        });
        --activeValidations;
        validationChanged.notify_all();
    });
    persistence.start();

    std::barrier start(3);
    bool firstLoaded = false;
    bool secondLoaded = false;
    std::thread firstLoader([&] {
        start.arrive_and_wait();
        firstLoaded = world.generateChunkIfMissing(firstCx, cz);
    });
    std::thread secondLoader([&] {
        start.arrive_and_wait();
        secondLoaded = world.generateChunkIfMissing(secondCx, cz);
    });
    start.arrive_and_wait();

    bool independentLoadsOverlapped = false;
    {
        std::unique_lock lock(validationMutex);
        independentLoadsOverlapped = validationChanged.wait_for(
            lock, std::chrono::seconds(3), [&] {
                return maximumActiveValidations >= 2;
            });
        releaseValidation = true;
        holdValidation = false;
    }
    validationChanged.notify_all();
    firstLoader.join();
    secondLoader.join();
    check(independentLoadsOverlapped && maximumActiveValidations >= 2,
          "persistence loaders for independent region files execute concurrently");
    check(firstLoaded && secondLoaded,
          "concurrent persistence loader callbacks publish both saved chunks");

    check(world.eraseChunkIf(firstCx, cz, [] { return true; }),
          "stop-wait fixture evicts the chunk before reloading it");
    {
        std::lock_guard lock(validationMutex);
        holdValidation = true;
        releaseValidation = false;
    }
    bool reloaded = false;
    std::thread heldLoader([&] {
        reloaded = world.generateChunkIfMissing(firstCx, cz);
    });
    bool enteredHeldValidation = false;
    {
        std::unique_lock lock(validationMutex);
        enteredHeldValidation = validationChanged.wait_for(
            lock, std::chrono::seconds(3), [&] {
                return validationCalls >= 3;
            });
    }
    std::atomic<bool> stopReturned{false};
    std::thread stopper([&] {
        stopPersistenceAndWait(persistence);
        stopReturned.store(true, std::memory_order_release);
    });
    const bool stopWaitedForCallback =
        WorldMutationContractTestAccess::waitForPersistenceCallbackWaiter(
            persistence, std::chrono::seconds(3));
    {
        std::lock_guard lock(validationMutex);
        releaseValidation = true;
        holdValidation = false;
    }
    validationChanged.notify_all();
    heldLoader.join();
    stopper.join();
    check(enteredHeldValidation && stopWaitedForCallback &&
              stopReturned.load(std::memory_order_acquire),
          "persistence stop waits for an active loader before invalidating its owner");
    const bool reloadPublishedConsistently =
        reloaded == world.hasChunk(firstCx, cz);
    bool retryAfterRestart = reloaded;
    if (!reloaded) {
        persistence.start();
        retryAfterRestart = world.generateChunkIfMissing(firstCx, cz) &&
                            world.hasChunk(firstCx, cz);
        stopPersistenceAndWait(persistence);
    }
    check(reloadPublishedConsistently && retryAfterRestart,
          "a concurrent stop either completes the loaded publication or cancels it for a successful retry after restart");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "callback lease fixture removes its exact temporary world directory");
}

void testPersistenceStopWaitsForEditLeaseAdmission() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-persistence-edit-stop-" + std::to_string(unique));
    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");

    std::mutex gateMutex;
    std::condition_variable gateChanged;
    bool editReachedLeaseBoundary = false;
    bool releaseEdit = false;
    WorldMutationContractTestAccess::setBeforeEditLeaseForTest(
        persistence, [&] {
            std::unique_lock lock(gateMutex);
            editReachedLeaseBoundary = true;
            gateChanged.notify_all();
            gateChanged.wait(lock, [&] { return releaseEdit; });
        });
    persistence.start();

    constexpr std::int32_t x = 6;
    constexpr std::int32_t y = 100;
    constexpr std::int32_t z = -3;
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    bool editCommitted = false;
    std::thread editor([&] {
        editCommitted = world.setBlock(x, y, z, stone);
    });
    bool reachedBoundary = false;
    {
        std::unique_lock lock(gateMutex);
        reachedBoundary = gateChanged.wait_for(
            lock, std::chrono::seconds(3),
            [&] { return editReachedLeaseBoundary; });
    }

    std::atomic<bool> stopReturned{false};
    std::thread stopper([&] {
        stopPersistenceAndWait(persistence);
        stopReturned.store(true, std::memory_order_release);
    });
    const auto stopWaitDeadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(2);
    while (WorldMutationContractTestAccess::mutationOrderWaiters(world) == 0 &&
           !stopReturned.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < stopWaitDeadline)
        std::this_thread::yield();
    const bool stopWaitedAtMutationBoundary =
        WorldMutationContractTestAccess::mutationOrderWaiters(world) > 0;
    {
        std::lock_guard lock(gateMutex);
        releaseEdit = true;
    }
    gateChanged.notify_all();
    editor.join();
    stopper.join();

    Chunk decoded;
    ChunkBuildWriter writer(decoded);
    const auto loadStatus = persistence.loadChunkStatus(x >> 4, z >> 4, writer);
    const int wy = y - kMinY;
    const bool savedEdit = loadStatus == ChunkLoadStatus::Loaded &&
        writer.blocks.get(Chunk::index(wy >> 4, wy & 15, z & 15, x & 15)) == stone;
    check(reachedBoundary && stopWaitedAtMutationBoundary && editCommitted &&
              savedEdit,
          "persistence stop waits at the World mutation gate before detaching its edit hook");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "edit-stop race fixture removes its exact temporary world directory");
}

void testPersistenceStopDrainsCommittedWorldMutation() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-persistence-committed-edit-stop-" +
         std::to_string(unique));
    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");

    std::mutex gateMutex;
    std::condition_variable gateChanged;
    bool blockHookReached = false;
    bool releaseBlockHook = false;
    world.setOnBlockChanged(
        [&](std::int32_t x, std::int32_t y, std::int32_t z,
            std::uint16_t, std::uint16_t) {
            if (x != 6 || y != 100 || z != -3) return;
            std::unique_lock lock(gateMutex);
            blockHookReached = true;
            gateChanged.notify_all();
            gateChanged.wait(lock, [&] { return releaseBlockHook; });
        });
    persistence.start();

    constexpr std::int32_t x = 6;
    constexpr std::int32_t y = 100;
    constexpr std::int32_t z = -3;
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    bool editCommitted = false;
    std::thread editor([&] {
        editCommitted = world.setBlock(x, y, z, stone);
    });
    bool reachedBlockHook = false;
    {
        std::unique_lock lock(gateMutex);
        reachedBlockHook = gateChanged.wait_for(
            lock, std::chrono::seconds(3), [&] { return blockHookReached; });
    }

    std::atomic<bool> stopReturned{false};
    std::thread stopper([&] {
        stopPersistenceAndWait(persistence);
        stopReturned.store(true, std::memory_order_release);
    });
    const auto waitDeadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(3);
    while (WorldMutationContractTestAccess::mutationOrderWaiters(world) == 0 &&
           !stopReturned.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < waitDeadline)
        std::this_thread::yield();
    const bool stopWaitedForCommittedMutation =
        WorldMutationContractTestAccess::mutationOrderWaiters(world) > 0;

    {
        std::lock_guard lock(gateMutex);
        releaseBlockHook = true;
    }
    gateChanged.notify_all();
    editor.join();
    stopper.join();
    world.setOnBlockChanged({});

    Chunk decoded;
    ChunkBuildWriter writer(decoded);
    const auto loadStatus = persistence.loadChunkStatus(x >> 4, z >> 4, writer);
    const auto wy = y - kMinY;
    const bool savedEdit = loadStatus == ChunkLoadStatus::Loaded &&
        writer.blocks.get(Chunk::index(wy >> 4, wy & 15, z & 15, x & 15)) ==
            stone;
    check(reachedBlockHook && stopWaitedForCommittedMutation &&
              editCommitted && stopReturned.load(std::memory_order_acquire) &&
              savedEdit,
          "persistence stop waits for a committed World mutation before detaching its dirty hook");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "committed-edit stop fixture removes its exact temporary world directory");
}

void testPersistenceReentrantStopDuringConcurrentStop() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-persistence-reentrant-stop-" + std::to_string(unique));
    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");

    Chunk fixture;
    ChunkBuildWriter fixtureWriter(fixture);
    const auto root = chunkToNBT(0, 0, fixture, "minecraft:plains");
    WriteBuffer bytes;
    nbt::writeFileRoot(bytes, root);
    persistence.storeChunkBytes(0, 0, bytes.data);

    std::mutex gateMutex;
    std::condition_variable gateChanged;
    bool enteredReadExtras = false;
    bool releaseReadExtras = false;
    std::atomic<bool> reentrantStopReturned{false};
    persistence.setChunkExtras({}, [&](const nbt::Value&) {
        std::unique_lock lock(gateMutex);
        enteredReadExtras = true;
        gateChanged.notify_all();
        gateChanged.wait(lock, [&] { return releaseReadExtras; });
        lock.unlock();
        persistence.stop();
        reentrantStopReturned.store(true, std::memory_order_release);
    });
    persistence.start();

    Chunk detached;
    ChunkBuildWriter detachedWriter(detached);
    const auto loadStatus = persistence.loadChunkStatus(0, 0, detachedWriter);
    auto publish = detachedWriter.takePublishCallback();
    std::thread publisher([&] { if (publish) publish(); });
    bool entered = false;
    {
        std::unique_lock lock(gateMutex);
        entered = gateChanged.wait_for(lock, std::chrono::seconds(3),
                                       [&] { return enteredReadExtras; });
    }
    std::atomic<bool> outerStopReturned{false};
    std::thread stopper([&] {
        stopPersistenceAndWait(persistence);
        outerStopReturned.store(true, std::memory_order_release);
    });
    const bool outerStopWaited =
        WorldMutationContractTestAccess::waitForPersistenceCallbackWaiter(
            persistence, std::chrono::seconds(3));
    {
        std::lock_guard lock(gateMutex);
        releaseReadExtras = true;
    }
    gateChanged.notify_all();
    publisher.join();
    stopper.join();

    check(loadStatus == ChunkLoadStatus::Loaded && entered && outerStopWaited &&
              reentrantStopReturned.load(std::memory_order_acquire) &&
              outerStopReturned.load(std::memory_order_acquire),
          "callback reentrant stop returns instead of deadlocking against concurrent shutdown");
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "reentrant-stop fixture removes its exact temporary world directory");
}

void testPersistenceRetainsBiomeAcrossPublishRetry() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-persistence-biome-retry-" + std::to_string(unique));
    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");
    const std::unordered_map<std::uint16_t, std::string> biomeCodec{
        {1, "minecraft:desert"}};
    persistence.setBiomeCodec(biomeCodec, 0);

    Chunk fixture;
    ChunkBuildWriter fixtureWriter(fixture);
    fixtureWriter.biomes.fill(1);
    const auto root = chunkToNBT(0, 0, fixture, "minecraft:plains",
                                 &biomeCodec);
    WriteBuffer bytes;
    nbt::writeFileRoot(bytes, root);
    persistence.storeChunkBytes(0, 0, bytes.data);

    std::atomic<int> readAttempts{0};
    persistence.setChunkExtras({}, [&](const nbt::Value&) {
        if (readAttempts.fetch_add(1, std::memory_order_relaxed) == 0)
            throw std::runtime_error("retry extras publication once");
    });
    persistence.start();
    const bool firstLoad = world.generateChunkIfMissing(0, 0);
    const bool retriedLoad = world.generateChunkIfMissing(0, 0);
    const auto biome = WorldMutationContractTestAccess::biomeOverride(persistence);
    check(!firstLoad && retriedLoad && readAttempts.load() == 2 && biome &&
              *biome == "minecraft:desert",
          "retrying chunk extras publication preserves the decoded biome override");
    stopPersistenceAndWait(persistence);

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "biome-retry fixture removes its exact temporary world directory");
}

void testChunkLoadDoesNotMarkPersistenceDirty() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-load-not-dirty-" + std::to_string(unique));
    cppfm::Persistence persistence(world, directory.string(),
                                   "minecraft:plains");
    persistence.start();

    cppfm::Chunk loaded;
    cppfm::ChunkBuildWriter writer(loaded);
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    writer.blocks.set(cppfm::Chunk::index(10, 4, 4, 4), stone);
    world.installLoadedChunk(3, 4, std::move(loaded));
    check(world.hasChunk(3, 4) && !persistence.isDirty(3, 4),
          "publishing persisted chunk data does not schedule an unnecessary rewrite");

    stopPersistenceAndWait(persistence);
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "load-not-dirty fixture removes its exact unique temporary directory");
}

void testConcurrentPersistenceSnapshotFence() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    constexpr std::int32_t chunkX = 11;
    constexpr std::int32_t chunkZ = -7;
    world.generateChunkIfMissing(chunkX, chunkZ);
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-concurrent-save-" + std::to_string(unique));
    cppfm::Persistence persistence(world, directory.string(),
                                   "minecraft:plains");
    persistence.start();
    persistence.markDirty(chunkX, chunkZ);
    const auto staleVersion = persistence.dirtyVersion(chunkX, chunkZ);
    const std::vector<std::uint8_t> staleSnapshot{0x31, 0x41, 0x59};
    const std::vector<std::uint8_t> currentSnapshot{0x26, 0x53, 0x58};
    const auto regionPath =
        (directory / "region" / "r.0.-1.mca").string();

    std::atomic<bool> staleWriterStarted{false};
    bool staleWriterStored = true;
    std::thread staleWriter;
    std::uint64_t currentVersion = 0;
    {
        auto regionGuard =
            cppfm::Persistence::lockRegionFileTransaction(regionPath);
        staleWriter = std::thread([&] {
            staleWriterStarted.store(true, std::memory_order_release);
            staleWriterStored = persistence.storeChunkBytesIfDirtyVersion(
                chunkX, chunkZ, staleVersion, staleSnapshot);
        });
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(2);
        while (!staleWriterStarted.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        check(staleWriterStarted.load(std::memory_order_acquire),
              "old background snapshot attempts the region transaction concurrently");
        persistence.markDirty(chunkX, chunkZ);
        currentVersion = persistence.dirtyVersion(chunkX, chunkZ);
    }
    staleWriter.join();
    check(currentVersion != staleVersion && !staleWriterStored,
          "old snapshot is rejected after waiting behind a newer dirty generation");
    check(persistence.storeChunkBytesIfDirtyVersion(
              chunkX, chunkZ, currentVersion, currentSnapshot),
          "current snapshot writes successfully after the stale writer is rejected");
    check(persistence.markCleanIfVersion(chunkX, chunkZ, currentVersion),
          "successful current snapshot clears only its matching dirty generation");
    stopPersistenceAndWait(persistence);

    cppfm::RegionFile stored(regionPath);
    check(stored.load(chunkX & 31, chunkZ & 31) == currentSnapshot,
          "stale write cannot replace the newer snapshot on disk");
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "concurrent-save fixture removes its exact unique temporary directory");
}

void testPersistenceFailedSaveCanRetry() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    constexpr std::int32_t chunkX = 11;
    constexpr std::int32_t chunkZ = -7;
    world.generateChunkIfMissing(chunkX, chunkZ);

    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-save-retry-" + std::to_string(unique));
    const auto regionPath = directory / "region" / "r.0.-1.mca";
    std::error_code setupError;
    std::filesystem::create_directories(regionPath, setupError);
    check(!setupError,
          "failed-save fixture creates a directory at the exact region-file path");
    if (setupError) {
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
        return;
    }

    cppfm::Persistence persistence(world, directory.string(),
                                   "minecraft:plains");
    persistence.start();
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    world.setBlock(chunkX * 16 + 4, 100, chunkZ * 16 + 4, stone);
    check(persistence.isDirty(chunkX, chunkZ),
          "post-start world edit marks the chunk dirty for durable persistence");

    check(!persistence.flushChunk(chunkX, chunkZ) &&
              persistence.isDirty(chunkX, chunkZ),
          "a real region write failure is reported and keeps the chunk dirty");

    std::error_code removeError;
    std::filesystem::remove_all(regionPath, removeError);
    check(!removeError,
          "failed-save fixture removes only its conflicting region-path directory");
    if (!removeError) {
        // The background worker is allowed to win this retry after the
        // conflicting path is removed. A concurrent successful write makes
        // this explicit flush correctly report no current dirty generation.
        (void)persistence.flushChunk(chunkX, chunkZ);
        const auto retryDeadline = std::chrono::steady_clock::now() +
                                   std::chrono::seconds(5);
        while (persistence.isDirty(chunkX, chunkZ) &&
               std::chrono::steady_clock::now() < retryDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(!persistence.isDirty(chunkX, chunkZ),
              "the retained dirty generation is persisted after the failure is cleared");
    }

    stopPersistenceAndWait(persistence);
    if (!removeError) {
        cppfm::RegionFile stored((directory / "region" / "r.0.-1.mca").string());
        check(!stored.load(chunkX & 31, chunkZ & 31).empty(),
              "successful retry leaves a readable saved chunk in the region file");
    }
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "save-retry fixture removes its exact unique temporary directory");
}


void testChunkExtrasRunOutsideWorldReadLock() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 0);
    world.generateChunkIfMissing(0, 0);
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-save-lock-order-" + std::to_string(unique));
    cppfm::Persistence persistence(world, directory.string(),
                                   "minecraft:plains");
    bool extraCallbackRan = false;
    persistence.setChunkExtras(
        [&](std::int32_t cx, std::int32_t cz, cppfm::nbt::Value&) {
            if (cx == 0 && cz == 0)
                extraCallbackRan = world.setBlock(1, 100, 1, stone);
        }, {});

    const bool persisted = persistence.flushChunk(0, 0);
    check(extraCallbackRan && world.getBlock(1, 100, 1) == stone,
          "chunk extras may safely mutate World without a nested read-lock deadlock");
    check(!persisted,
          "a snapshot without a dirty generation is not reported as durable");
    persistence.setChunkExtras({}, {});
    stopPersistenceAndWait(persistence);
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "save-lock-order fixture removes its exact unique temporary directory");
}

std::uint64_t hashChunk(const cppfm::Chunk& chunk) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto add = [&](std::uint8_t byte) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    };
    for (std::size_t i = 0; i < cppfm::kSectionsPerChunk * 4096; ++i) {
        const auto block = chunk.blockAt(i);
        add(static_cast<std::uint8_t>(block));
        add(static_cast<std::uint8_t>(block >> 8));
    }
    for (const auto biome : chunk.biomes) {
        add(static_cast<std::uint8_t>(biome));
        add(static_cast<std::uint8_t>(biome >> 8));
    }
    for (const auto light : chunk.blockLightNib) add(light);
    const bool hasSky = chunk.hasSkyLightStorage();
    add(hasSky ? 1 : 0);
    add(chunk.skyLightReady() ? 1 : 0);
    if (hasSky) {
        for (std::size_t i = 0; i < cppfm::kSectionsPerChunk * 2048; ++i) {
            const auto low = chunk.skyLightAt(i * 2);
            const auto high = chunk.skyLightAt(i * 2 + 1);
            add(static_cast<std::uint8_t>(low | (high << 4)));
        }
    }
    return hash;
}

void testChunkNbtDecodeWriterPath() {
    cppfm::World source("minecraft:plains", cppfm::LevelType::Flat, 123);
    source.generateChunkIfMissing(0, 0);
    const auto original = source.tryGetCopy(0, 0);
    check(original.has_value(), "flat chunk exists for NBT decode regression");
    if (!original) return;

    const std::unordered_map<std::uint16_t, std::string> biomeKeys{
        {0, "minecraft:plains"}};
    const auto root = cppfm::chunkToNBT(0, 0, *original,
                                        "minecraft:plains", &biomeKeys);
    cppfm::WriteBuffer encoded;
    cppfm::nbt::writeFileRoot(encoded, root);
    cppfm::ReadBuffer input(encoded.data);
    cppfm::nbt::Parser parser(input);
    const auto decodedRoot = parser.readFileRoot();

    cppfm::Chunk decoded;
    cppfm::ChunkBuildWriter writer(decoded);
    std::string biomeOverride;
    const bool loaded = cppfm::chunkFromNBT(
        decodedRoot, writer, {}, biomeOverride,
        [](const std::string&) -> std::int32_t { return 0; });
    check(loaded, "1.21.4 full-chunk NBT decodes through detached writer");
    check(hashChunk(decoded) == hashChunk(*original),
          "decoded block, biome, and light values match encoded chunk");
}

void testChunkExtrasWaitForAcceptedPublication() {
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto persistenceDirectory = std::filesystem::temp_directory_path() /
        ("cppfm-chunk-extras-publication-" + std::to_string(unique));
    cppfm::World source("minecraft:plains", cppfm::LevelType::Flat, 123);
    source.generateChunkIfMissing(0, 0);
    const auto original = source.tryGetCopy(0, 0);
    check(original.has_value(), "chunk extras fixture exists");
    if (!original) return;

    const std::unordered_map<std::uint16_t, std::string> biomeKeys{
        {0, "minecraft:plains"}};
    auto root = cppfm::chunkToNBT(0, 0, *original,
                                  "minecraft:plains", &biomeKeys);
    root.set("isLightOn", cppfm::nbt::Value::makeByte(1));
    cppfm::nbt::Value structures = cppfm::nbt::Value::makeCompound();
    structures.set("fixture_marker", cppfm::nbt::Value::makeInt(4189));
    root.set("structures", std::move(structures));
    root.set("modded_chunk_root",
             cppfm::nbt::Value::makeString("preserve across rewrite"));
    const std::vector<std::int64_t> worldgenHeightmapData(
        cppfm::kAnvilHeightmapLongCount, 0x123456789ABCDEFLL);
    if (auto* heightmaps = root.get("Heightmaps")) {
        cppfm::nbt::Value customHeightmap;
        customHeightmap.tag = cppfm::nbt::LongArray;
        customHeightmap.longArray = {4189, 769};
        heightmaps->set("example:custom_heightmap", std::move(customHeightmap));
        for (const char* name : {"WORLD_SURFACE_WG", "OCEAN_FLOOR_WG",
                                 "OCEAN_FLOOR",
                                 "MOTION_BLOCKING_NO_LEAVES"}) {
            cppfm::nbt::Value staleHeightmap;
            staleHeightmap.tag = cppfm::nbt::LongArray;
            staleHeightmap.longArray =
                std::string_view(name).ends_with("_WG")
                    ? worldgenHeightmapData
                    : std::vector<std::int64_t>{4189, 769};
            heightmaps->set(name, std::move(staleHeightmap));
        }
    }
    if (auto* sections = root.get("sections");
        sections && sections->tag == cppfm::nbt::List) {
        for (auto& section : sections->list) {
            const auto* y = section.get("Y");
            if (!y || y->tag != cppfm::nbt::Byte || y->b != 0) continue;
            section.set("example:section_marker",
                        cppfm::nbt::Value::makeString("keep section extension"));
            cppfm::nbt::Value staleBlockLight;
            staleBlockLight.tag = cppfm::nbt::ByteArray;
            staleBlockLight.byteArray = {0x41, 0x42, 0x43};
            section.set("BlockLight", std::move(staleBlockLight));
            cppfm::nbt::Value staleSkyLight;
            staleSkyLight.tag = cppfm::nbt::ByteArray;
            staleSkyLight.byteArray = {0x51, 0x52, 0x53};
            section.set("SkyLight", std::move(staleSkyLight));
            if (auto* states = section.get("block_states"))
                states->set("example:palette_extension",
                            cppfm::nbt::Value::makeInt(769));
            cppfm::nbt::Value unknownBiomes =
                cppfm::nbt::Value::makeCompound();
            cppfm::nbt::Value unknownBiomePalette =
                cppfm::nbt::Value::makeList(cppfm::nbt::String);
            unknownBiomePalette.list.push_back(
                cppfm::nbt::Value::makeString(
                    "example:unregistered_biome"));
            unknownBiomePalette.list.push_back(
                cppfm::nbt::Value::makeString("minecraft:plains"));
            unknownBiomes.set("palette", std::move(unknownBiomePalette));
            cppfm::nbt::Value unknownBiomeData;
            unknownBiomeData.tag = cppfm::nbt::LongArray;
            unknownBiomeData.longArray = {0};
            unknownBiomes.set("data", std::move(unknownBiomeData));
            section.set("biomes", std::move(unknownBiomes));
            break;
        }
        auto outOfRangeSection = cppfm::nbt::Value::makeCompound();
        outOfRangeSection.set("Y", cppfm::nbt::Value::makeByte(25));
        cppfm::nbt::Value outOfRangeMarker = cppfm::nbt::Value::makeString(
            "retain out-of-range extension");
        outOfRangeSection.set("example:out_of_range", std::move(outOfRangeMarker));
        cppfm::nbt::Value outOfRangeLight;
        outOfRangeLight.tag = cppfm::nbt::ByteArray;
        outOfRangeLight.byteArray = {0x61, 0x62};
        outOfRangeSection.set("BlockLight", std::move(outOfRangeLight));
        sections->list.push_back(std::move(outOfRangeSection));
    }
    auto blockEntities = cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
    auto sign = cppfm::nbt::Value::makeCompound();
    sign.set("id", cppfm::nbt::Value::makeString("minecraft:sign"));
    sign.set("x", cppfm::nbt::Value::makeInt(4));
    sign.set("y", cppfm::nbt::Value::makeInt(100));
    sign.set("z", cppfm::nbt::Value::makeInt(4));
    auto frontText = cppfm::nbt::Value::makeCompound();
    auto messages = cppfm::nbt::Value::makeList(cppfm::nbt::String, 4);
    messages.list.push_back(cppfm::nbt::Value::makeString("saved value"));
    for (int i = 1; i < 4; ++i)
        messages.list.push_back(cppfm::nbt::Value::makeString(""));
    frontText.set("messages", std::move(messages));
    sign.set("front_text", std::move(frontText));
    blockEntities.list.push_back(std::move(sign));
    root.set("block_entities", std::move(blockEntities));
    cppfm::WriteBuffer encoded;
    cppfm::nbt::writeFileRoot(encoded, root);

    cppfm::World edited("minecraft:plains", cppfm::LevelType::Flat, 123);
    cppfm::Persistence persistence(edited, persistenceDirectory.string(),
                                  "minecraft:plains");
    cppfm::BlockEntityStore liveBlockEntities;
    cppfm::BlockEntityStore freshBlockEntities;
    auto liveSign = liveBlockEntities.createShared(
        cppfm::posKey(4, 100, 4), cppfm::BlockEntity::Kind::Sign);
    liveSign->sign.front[0] = "live value";
    cppfm::BlockEntityStore* targetBlockEntities = &liveBlockEntities;
    int extrasApplied = 0;
    persistence.setChunkExtras({}, [&](const cppfm::nbt::Value& decodedRoot) {
        if (decodedRoot.get("block_entities")) {
            ++extrasApplied;
            targetBlockEntities->readChunkNbt(decodedRoot);
        }
    });

    cppfm::Chunk staleChunk;
    std::function<void()> stalePublishCallback;
    bool decoded = false;
    {
        cppfm::ChunkBuildWriter writer(staleChunk);
        decoded = persistence.decodeChunkBytes(encoded.data, writer, 0, 0);
        stalePublishCallback = writer.takePublishCallback();
    }
    check(decoded && static_cast<bool>(stalePublishCallback),
          "NBT extras decode into a deferred publication callback");
    check(extrasApplied == 0,
          "decoding detached NBT does not mutate live block entities");

    edited.generateChunkIfMissing(0, 0);
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    edited.setBlock(4, 100, 4, stone);
    edited.installLoadedChunk(0, 0, std::move(staleChunk),
                              std::move(stalePublishCallback));
    check(extrasApplied == 0 && edited.getBlock(4, 100, 4) == stone,
          "rejected stale chunk cannot apply old block-entity NBT");
    check(liveBlockEntities.getAt(4, 100, 4)->sign.front[0] == "live value",
          "rejected stale chunk leaves live block-entity edits untouched");

    cppfm::World accepted("minecraft:plains", cppfm::LevelType::Flat, 123);
    targetBlockEntities = &liveBlockEntities;
    cppfm::Chunk acceptedLiveChunk;
    std::function<void()> acceptedLiveCallback;
    {
        cppfm::ChunkBuildWriter writer(acceptedLiveChunk);
        decoded = persistence.decodeChunkBytes(encoded.data, writer, 0, 0);
        acceptedLiveCallback = writer.takePublishCallback();
    }
    accepted.installLoadedChunk(0, 0, std::move(acceptedLiveChunk),
                                std::move(acceptedLiveCallback));
    check(extrasApplied == 1 &&
              liveBlockEntities.getAt(4, 100, 4)->sign.front[0] == "live value",
          "accepted stale NBT preserves an already-dirty live block entity");

    cppfm::BlockEntityStore deletedBlockEntities;
    deletedBlockEntities.createShared(cppfm::posKey(4, 100, 4),
                                      cppfm::BlockEntity::Kind::Sign);
    deletedBlockEntities.remove(cppfm::posKey(4, 100, 4));
    deletedBlockEntities.readChunkNbt(root);
    check(!deletedBlockEntities.getAt(4, 100, 4),
          "deferred stale NBT cannot resurrect a removed block entity");

    targetBlockEntities = &freshBlockEntities;
    cppfm::World acceptedFresh("minecraft:plains", cppfm::LevelType::Flat, 123);
    cppfm::Chunk freshChunk;
    std::function<void()> freshPublishCallback;
    {
        cppfm::ChunkBuildWriter writer(freshChunk);
        decoded = persistence.decodeChunkBytes(encoded.data, writer, 0, 0);
        freshPublishCallback = writer.takePublishCallback();
    }
    acceptedFresh.installLoadedChunk(0, 0, std::move(freshChunk),
                                     std::move(freshPublishCallback));
    check(decoded && extrasApplied == 2 &&
              freshBlockEntities.getAt(4, 100, 4) &&
              freshBlockEntities.getAt(4, 100, 4)->sign.front[0] == "saved value",
          "accepted persisted chunk loads block-entity NBT into empty live state");

    const auto acceptedChunk = acceptedFresh.tryGetCopy(0, 0);
    if (acceptedChunk) {
        auto rewrittenRoot = cppfm::chunkToNBT(
            0, 0, *acceptedChunk, "minecraft:plains", &biomeKeys);
        persistence.mergeOpaqueChunkRootNbt(0, 0, rewrittenRoot);
        const auto* rewrittenStructures = rewrittenRoot.get("structures");
        const auto* marker = rewrittenStructures
            ? rewrittenStructures->get("fixture_marker") : nullptr;
        const auto* moddedRoot = rewrittenRoot.get("modded_chunk_root");
        const auto* rewrittenHeightmaps = rewrittenRoot.get("Heightmaps");
        const auto* customHeightmap = rewrittenHeightmaps
            ? rewrittenHeightmaps->get("example:custom_heightmap") : nullptr;
        bool regularHeightmapsRecomputed = rewrittenHeightmaps != nullptr;
        for (const char* name : {"WORLD_SURFACE", "OCEAN_FLOOR",
                                 "MOTION_BLOCKING",
                                 "MOTION_BLOCKING_NO_LEAVES"}) {
            const auto* map = rewrittenHeightmaps
                ? rewrittenHeightmaps->get(name) : nullptr;
            regularHeightmapsRecomputed = regularHeightmapsRecomputed && map &&
                map->tag == cppfm::nbt::LongArray &&
                map->longArray.size() == cppfm::kAnvilHeightmapLongCount &&
                map->longArray != std::vector<std::int64_t>{4189, 769};
        }
        bool worldgenHeightmapsPreserved = true;
        for (const char* name : {"WORLD_SURFACE_WG", "OCEAN_FLOOR_WG"}) {
            const auto* map = rewrittenHeightmaps
                ? rewrittenHeightmaps->get(name) : nullptr;
            worldgenHeightmapsPreserved = worldgenHeightmapsPreserved && map &&
                map->tag == cppfm::nbt::LongArray &&
                map->longArray == worldgenHeightmapData;
        }
        const auto* rewrittenLightFlag = rewrittenRoot.get("isLightOn");
        const auto* rewrittenSections = rewrittenRoot.get("sections");
        const cppfm::nbt::Value* sectionMarker = nullptr;
        const cppfm::nbt::Value* paletteExtension = nullptr;
        const cppfm::nbt::Value* preservedBiomePalette = nullptr;
        const cppfm::nbt::Value* preservedBiomeData = nullptr;
        bool staleLightOmitted = false;
        bool outOfRangeLightOmitted = false;
        bool outOfRangeExtensionPreserved = false;
        if (rewrittenSections && rewrittenSections->tag == cppfm::nbt::List) {
            for (const auto& section : rewrittenSections->list) {
                const auto* y = section.get("Y");
                if (y && y->tag == cppfm::nbt::Byte && y->b == 0) {
                    sectionMarker = section.get("example:section_marker");
                    staleLightOmitted = !section.get("BlockLight") &&
                                         !section.get("SkyLight");
                    const auto* states = section.get("block_states");
                    paletteExtension = states
                        ? states->get("example:palette_extension") : nullptr;
                    const auto* biomes = section.get("biomes");
                    preservedBiomePalette = biomes
                        ? biomes->get("palette") : nullptr;
                    preservedBiomeData = biomes ? biomes->get("data") : nullptr;
                }
                if (y && y->tag == cppfm::nbt::Byte && y->b == 25) {
                    outOfRangeLightOmitted = !section.get("BlockLight") &&
                                             !section.get("SkyLight");
                    const auto* extension = section.get("example:out_of_range");
                    outOfRangeExtensionPreserved = extension &&
                        extension->tag == cppfm::nbt::String &&
                        extension->str == "retain out-of-range extension";
                }
            }
        }
        check(marker && marker->tag == cppfm::nbt::Int && marker->i == 4189 &&
                  moddedRoot && moddedRoot->tag == cppfm::nbt::String &&
                  moddedRoot->str == "preserve across rewrite" &&
                  customHeightmap && customHeightmap->tag == cppfm::nbt::LongArray &&
                  customHeightmap->longArray == std::vector<std::int64_t>{4189, 769} &&
                  regularHeightmapsRecomputed && worldgenHeightmapsPreserved &&
                  staleLightOmitted &&
                  outOfRangeLightOmitted && outOfRangeExtensionPreserved &&
                  rewrittenLightFlag &&
                  rewrittenLightFlag->tag == cppfm::nbt::Byte &&
                  rewrittenLightFlag->b == 0 &&
                  sectionMarker && sectionMarker->tag == cppfm::nbt::String &&
                  sectionMarker->str == "keep section extension" &&
                  paletteExtension && paletteExtension->tag == cppfm::nbt::Int &&
                  paletteExtension->i == 769 && preservedBiomePalette &&
                  preservedBiomePalette->tag == cppfm::nbt::List &&
                  preservedBiomePalette->list.size() == 2 &&
                  preservedBiomePalette->list.front().tag == cppfm::nbt::String &&
                  preservedBiomePalette->list.front().str ==
                      "example:unregistered_biome" && preservedBiomeData &&
                  preservedBiomeData->tag == cppfm::nbt::LongArray &&
                  preservedBiomeData->longArray ==
                      std::vector<std::int64_t>{0},
              "unmodeled chunk, heightmap, section, and unknown-biome NBT survives a rewritten save");

        freshBlockEntities.remove(cppfm::posKey(4, 100, 4));
        auto afterLegacyDelete = cppfm::chunkToNBT(
            0, 0, *acceptedChunk, "minecraft:plains", &biomeKeys);
        cppfm::nbt::Value emptyEntities =
            cppfm::nbt::Value::makeList(cppfm::nbt::Compound);
        freshBlockEntities.writeChunkNbt(0, 0, emptyEntities);
        afterLegacyDelete.set("block_entities", std::move(emptyEntities));
        persistence.mergeOpaqueChunkRootNbt(0, 0, afterLegacyDelete);
        const auto* currentEntities = afterLegacyDelete.get("block_entities");
        check(!afterLegacyDelete.get("TileEntities") && currentEntities &&
                  currentEntities->tag == cppfm::nbt::List &&
                  currentEntities->list.empty(),
              "saving removed block-entity data cannot resurrect a stale list");

        auto malformedDestination = cppfm::nbt::Value::makeCompound();
        malformedDestination.set("sections",
            cppfm::nbt::Value::makeString("not a section list"));
        persistence.mergeOpaqueChunkRootNbt(0, 0, malformedDestination);
        const auto* stillMalformed = malformedDestination.get("sections");
        check(stillMalformed && stillMalformed->tag == cppfm::nbt::String &&
                  stillMalformed->str == "not a section list",
              "opaque section templates never replace an absent or malformed generated section list");
    } else {
        check(false, "accepted chunk exists for opaque-root save check");
    }

    cppfm::World callbackFailure("minecraft:plains", cppfm::LevelType::Flat, 123);
    const auto before = callbackFailure.tryGetCopy(0, 0);
    int edits = 0;
    callbackFailure.setOnEdit([&](int, int) { ++edits; });
    cppfm::Chunk replacement;
    {
        cppfm::ChunkBuildWriter writer(replacement);
        writer.blocks.set(cppfm::Chunk::index(10, 4, 4, 4), stone);
    }
    bool publishFailed = false;
    try {
        callbackFailure.installLoadedChunk(0, 0, std::move(replacement), [] {
            throw std::runtime_error("injected chunk extras failure");
        });
    } catch (const cppfm::ChunkPublishError&) {
        publishFailed = true;
    }
    const auto after = callbackFailure.tryGetCopy(0, 0);
    check(publishFailed && !before && !after,
          "failed extras publication leaves no partially installed chunk");
    check(edits == 0,
          "failed extras publication does not mark a partial chunk dirty");
    std::error_code cleanupError;
    std::filesystem::remove_all(persistenceDirectory, cleanupError);
    check(!cleanupError,
          "chunk extras publication fixture removes its exact temporary directory");
}


} // namespace

int main() {
    testSessionInteractionContextTracksPitchAndHeldItem();
    testOrdinaryMutationContract();
    testWorldMutationRegistersItsGateForCallbacks();
    testChunkSaveCompletionCallbacksRunUnlocked();
    testBlockBatchRejectsInvalidEditsBeforeMutation();
    testBlockBatchReadersSeeWholeCommitBeforeCallbacks();
    testBlockBatchDuplicateCoordinatesUseLastStateOnce();
    testConditionalAirMutationLoadsSavedTargetBeforeReplacement();
    testConditionalAirMutationAcceptsAirVariants();
    testLoadedChunkMutationGateReturnsResidentLease();
    testChunkSnapshotQueuesBeforeConcurrentBlockDelta();
    testCloneEntityFailureRestoresBlocksAndEntityBytes();
    testCloneCommitIsNotReportedAsUnappliedWhenObserverCopyFails();
    testCloneRollbackClearsAllWhenInPlaceRestoreExceedsNbtBudget();
    testCloneAnvilAdmissionRejectsWithoutMutation();
    testUnknownPersistedBlockStatesAreNeverQuarantined();
    testUnsupportedStagedRegionIsPreservedDuringSave();
    testRegionFileHardLinkAliasesSerializeWriters();
    testStalePersistencePublicationCallbacksAreRejected();
    testPersistenceStopRequestedByPublicationIsServiced();
    testPreparedBlockEntityPublicationDoesNotLeakAcrossReentrantStop();
    testAnvilRejectsMalformedPalettes();
    testAnvilRoundTripsBlockStateProperties();
    testStairShapeUpdatesBroadcastInWorldDimension();
    testDetachedAndLoadedPublication();
    testSynchronousChunkPublishFailureIsRetryable();
    testSupportMutationContract();
    testReentrantMutation();
    testConcurrentMutationCallbackOrder();
    testDirtyMutationWinsConcurrentEviction();
    testDirtyFlushRetriesChunkRecoveryFailure();
    testForcedChunkWinsConcurrentEviction();
    testReentrantWriteDoesNotInvertChunkBuildLockOrder();
    testResidentPublicationDiscardsPendingLoadedCandidate();
    testConcurrentChunkLoadPublication();
    testLateLoadDoesNotOverwriteEdit();
    testLateLoadDoesNotReplaceGeneratedChunk();
    testSkyLightSnapshotOwnership();
    testChunkBuildStripeDistribution();
    testSeedChangeRejectsAnInFlightChunkLoad();
    testSeedChangeRejectsAnAsyncPersistenceRead();
    testWorldBlockReplacementDoesNotRetainStaleBlockEntity();
    testChunkIoGenerationFence();
    testPersistenceDirtyGenerationFence();
    testPersistenceCallbacksAreConcurrentAndStopWaits();
    testPersistenceStopWaitsForEditLeaseAdmission();
    testPersistenceStopDrainsCommittedWorldMutation();
    testPersistenceReentrantStopDuringConcurrentStop();
    testPersistenceRetainsBiomeAcrossPublishRetry();
    testChunkLoadDoesNotMarkPersistenceDirty();
    testConcurrentPersistenceSnapshotFence();
    testPersistenceFailedSaveCanRetry();
    testChunkExtrasRunOutsideWorldReadLock();
    testChunkNbtDecodeWriterPath();
    testChunkExtrasWaitForAcceptedPublication();
    std::printf("world_mutation_contract: %d assertions, %d failures\n",
                assertions, failures);
    return failures == 0 ? 0 : 1;
}
