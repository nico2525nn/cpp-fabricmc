// power source (15) * lever – toggleable source, right-click interaction * button – pulsed source (30 ticks), right-click interaction *
// redstone_wire – conducts 15 → 1 across a connected network * redstone_torch – source; turns OFF when the block it stands on is strongly
// powered by an adjacent wire/lamp chain * redstone_lamp – lights while any adjacent wire carries power * repeater – output-only booster
// with 1-4 delay (facing aware)
#pragma once
#include <cstdint>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <optional>
#include <utility>
#include "../game/World.hpp"
#include "../game/BlockEntities.hpp"

namespace cppfm {

struct RedstoneTick {
    std::int32_t x, y, z;
    std::int64_t dueTick;
    bool operator>(const RedstoneTick& o) const { return dueTick > o.dueTick; }
};

struct PistonEntity {
    std::int32_t x=0, y=0, z=0;
    float progress = 0.f; // 0-1
    bool extended = false;
    std::int64_t dueTick = 0;
    int face = 0; // 0-5
};
class BlockTickScheduler;
class RedstoneEngine {
public:
    explicit RedstoneEngine(World& world) : world_(world) {}

    // React to a block change: if the position or its neighbours host redstone components the surrounding network is recomputed.
    void onBlockChanged(std::int32_t x, std::int32_t y, std::int32_t z);
    // World edit hooks execute while World owns its mutation-order gate. Queue
    // their redstone work without taking operationMutex_; the simulation or
    // persistence boundary drains it after establishing the global lock order.
    bool enqueueBlockChanged(std::int32_t x, std::int32_t y,
                             std::int32_t z);

    // Right-click interaction with levers/buttons; returns true when consumed.
    bool onInteract(std::int32_t x, std::int32_t y, std::int32_t z,
                    std::int64_t now);

    void tick(std::int64_t now);                         // delayed updates
    // Save barrier: finish scheduled piston actions and in-flight animations
    // that can touch the snapshot chunk before it is serialized. Throws if
    // relevant piston work cannot be drained within the bounded barrier.
    void flushPendingPistons(std::int32_t chunkX, std::int32_t chunkZ);
    // Piston transactions can mutate chunks other than the one being flushed.
    // Notify persistence explicitly so save barriers do not depend on the
    // World's edit hook still being installed.
    void setPistonChunkDirtyFn(
        std::function<void(std::int32_t, std::int32_t)> fn);
    std::size_t pendingCount() const;
    // True when any adjacent source/wire carries power (dispenser gates).
    bool isPoweredHere(std::int32_t x, std::int32_t y, std::int32_t z);
    // JE quasi-connectivity: piston/dispenser powered if y+1 would be powered
    bool isQuasiPowered(std::int32_t x, std::int32_t y, std::int32_t z);

    void setBlockEntityStore(BlockEntityStore* s);
    void setTickRef(std::int64_t* t);
    void setTickRef(const std::atomic<std::int64_t>* t);
    void setBlockTickScheduler(BlockTickScheduler* bts);
    void setGameServer(void* srv);
    void setBroadcastFn(std::function<void(std::int32_t,std::int32_t,std::int32_t,std::uint16_t)> fn);

private:
    enum class Comp {
        None, Wire, LeverOn, LeverOff, ButtonOn, TorchOn, TorchOff,
        LampLit, LampOff, BlockSource, Repeater, Comparator, Observer,
        PoweredRail, DetectorRail, ActivatorRail, Rail, Piston, StickyPiston
    };

    static Comp classify(std::uint16_t state);
    static int maxEmissionFor(Comp c);
    int emissionLevel(std::uint16_t state, std::int32_t x, std::int32_t y, std::int32_t z);
    int analogOutputForContainer(BlockEntity* be);
    int analogOutputAt(std::int32_t x, std::int32_t y, std::int32_t z);
    bool isPoweredHereImpl(std::int32_t x, std::int32_t y, std::int32_t z);
    void drainBlockChanges();
    void processBlockChanged(std::int32_t x, std::int32_t y, std::int32_t z);
    void recomputeAround(std::int32_t x, std::int32_t y, std::int32_t z);
    void reactToPower(std::int32_t x, std::int32_t y, std::int32_t z);
    // Flood-fill wire power from all sources reachable within the network containing the seed wire.
    void updateWireNetwork(std::int32_t sx, std::int32_t sy, std::int32_t sz);
    void setPoweredAt(std::int32_t x, std::int32_t y, std::int32_t z,
                      std::uint8_t level);
    void handleRepeaterDelay(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t now);
    void handleComparator(std::int32_t x, std::int32_t y, std::int32_t z);
    void handleObserverTrigger(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t now);
    void recomputeRailShape(std::int32_t x, std::int32_t y, std::int32_t z);
    void handlePiston(std::int32_t x, std::int32_t y, std::int32_t z);
    void handlePistonScheduled(std::int32_t x, std::int32_t y, std::int32_t z, bool extendNow);
    void processPistonQueue(std::int64_t now);
    void notifyPistonChunksDirty(
        const std::vector<std::pair<std::int32_t, std::int32_t>>& chunks);
    void handleDoor(std::int32_t x, std::int32_t y, std::int32_t z);
    void setBlockAndBroadcast(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state);

    World& world_;
    BlockEntityStore* beStore_ = nullptr;
    std::int64_t* tickRef_ = nullptr;
    const std::atomic<std::int64_t>* atomicTickRef_ = nullptr;
    BlockTickScheduler* blockTicks_ = nullptr;
    void* gameServer_ = nullptr;
    std::function<void(std::int32_t,std::int32_t,std::int32_t,std::uint16_t)> broadcastFn_;
    std::function<void(std::int32_t,std::int32_t)> pistonChunkDirtyFn_;
    struct PendingPistonCommit {
        std::int32_t pistonX=0, pistonY=0, pistonZ=0;
        std::int32_t hx=0, hy=0, hz=0;
        std::uint16_t originalPistonState=0;
        std::uint16_t stagedPistonState=0;
        bool extend=false;
        int face=0;
        std::string facing;
        std::int64_t dueTick=0;
        struct Entry { std::int32_t x,y,z; std::uint16_t state; };
        struct ExpectedCell {
            std::int32_t x=0, y=0, z=0;
            std::uint16_t state=0;
            BlockEntityStore::Owner owner;
            std::optional<BlockEntity::Kind> ownerKind;
            std::uint16_t movingState=0;
            std::int32_t movingFacing=0;
            bool movingExtending=false;
            bool movingSource=false;
            std::int64_t movingFinishTick=0;
        };
        std::vector<Entry> entries; // original positions/states for extend (or head for retract)
        std::vector<Entry> pullEntries; // sticky pull blocks (retract only, original states)
        std::vector<ExpectedCell> expectedCells;
        int dx=0, dy=0, dz=0;
        bool isSticky=false;
    };
    void preparePendingPistonExpectations(PendingPistonCommit& commit);
    void capturePendingPistonExpectations(PendingPistonCommit& commit);
    bool pendingPistonOwnerMatches(
        const PendingPistonCommit::ExpectedCell& cell) const;
    bool pendingPistonExpectationsMatch(
        const PendingPistonCommit& commit) const;
    bool cancelPendingPistonCommit(PendingPistonCommit& commit);
    std::size_t processPendingPistonCommits(
        std::int64_t now,
        std::optional<std::pair<std::int32_t, std::int32_t>> chunk,
        std::size_t budget,
        bool updateMovingProgress);
    bool pistonQueueActionTouchesChunk(
        const PistonEntity& action,
        const std::pair<std::int32_t, std::int32_t>& chunk) const;
    bool pendingPistonCommitTouchesChunk(
        const PendingPistonCommit& commit,
        const std::pair<std::int32_t, std::int32_t>& chunk) const;
    bool hasPistonWorkForChunk(
        const std::pair<std::int32_t, std::int32_t>& chunk) const;
    std::vector<PistonEntity> pistonQueue_;
    std::vector<PendingPistonCommit> pendingPistonCommits_;
    std::priority_queue<RedstoneTick, std::vector<RedstoneTick>,
                        std::greater<RedstoneTick>> queue_;
    std::unordered_map<std::int64_t, std::int64_t> pendingRepeater_;
    std::unordered_map<std::int64_t, std::int64_t> observerPulseEnd_;
    std::unordered_set<std::int64_t> pendingBlockChanges_;
    bool processingBlockChanges_ = false;

    // Engine operations are serialized because they both mutate delayed
    // queues and inspect/mutate World.  The notification mutex is separate:
    // World::setBlock callbacks only enqueue a position while an operation is
    // in progress, so they never wait for this operation lock and therefore
    // cannot deadlock against the operation that caused the callback.
    mutable std::recursive_mutex operationMutex_;
    mutable std::mutex notificationMutex_;
};

} // namespace cppfm
