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
#include <memory>
#include <vector>
#include <functional>
#include "../game/World.hpp"
#include "../game/BlockEntities.hpp"

namespace cppfm {

class IRedstoneBehavior {
public:
    virtual ~IRedstoneBehavior() = default;
    virtual int calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) = 0;
    virtual void onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) = 0;
    virtual int maxSignal() const { return 15; }
};

class RedstoneComponent : public IRedstoneBehavior {
public:
    explicit RedstoneComponent(std::string name) : name_(std::move(name)) {}
    int calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) override;
    void onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) override;
private:
    std::string name_;
};

class RedstoneWireBehavior : public IRedstoneBehavior {
public:
    int calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) override;
    void onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) override;
};

class LeverBehavior : public IRedstoneBehavior {
public:
    int calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) override;
    void onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) override;
};

class ObserverBehavior : public IRedstoneBehavior {
public:
    int calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) override;
    void onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) override;
};

class ButtonBehavior : public IRedstoneBehavior {
public:
    int calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) override;
    void onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) override;
};

class TorchBehavior : public IRedstoneBehavior {
public:
    int calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) override;
    void onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) override;
};

class RepeaterBehavior : public IRedstoneBehavior {
public:
    int calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) override;
    void onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) override;
};

class ComparatorBehavior : public IRedstoneBehavior {
public:
    int calculateOutputSignal(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) override;
    void onBlockChanged(World& world, std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state, std::int64_t now) override;
};

struct RedstoneBehaviorRegistry {
    static IRedstoneBehavior* forBlock(const std::string& blockName);
    static void initDefaults();
};

} // namespace cppfm

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
    explicit RedstoneEngine(World& world) : world_(world) { RedstoneBehaviorRegistry::initDefaults(); }

    // React to a block change: if the position or its neighbours host redstone components the surrounding network is recomputed.
    void onBlockChanged(std::int32_t x, std::int32_t y, std::int32_t z);

    // Right-click interaction with levers/buttons; returns true when consumed.
    bool onInteract(std::int32_t x, std::int32_t y, std::int32_t z,
                    std::int64_t now);

    void tick(std::int64_t now);                         // delayed updates
    std::size_t pendingCount() const {
        return pistonQueue_.size() + pendingPistonCommits_.size() + queue_.size()
             + pendingRepeater_.size() + observerPrev_.size() + observerPulseEnd_.size();
    }
    // True when any adjacent source/wire carries power (dispenser gates).
    bool isPoweredHere(std::int32_t x, std::int32_t y, std::int32_t z);
    // JE quasi-connectivity: piston/dispenser powered if y+1 would be powered
    bool isQuasiPowered(std::int32_t x, std::int32_t y, std::int32_t z) {
        if (isPoweredHere(x, y, z)) return true;
        if (isPoweredHere(x, y + 1, z)) return true;
        return false;
    }

    void setBlockEntityStore(BlockEntityStore* s) { beStore_ = s; }
    void setTickRef(std::int64_t* t) { tickRef_ = t; }
    void setBlockTickScheduler(BlockTickScheduler* bts) { blockTicks_ = bts; }
    void setGameServer(void* srv) { gameServer_ = srv; }
    void setBroadcastFn(std::function<void(std::int32_t,std::int32_t,std::int32_t,std::uint16_t)> fn) { broadcastFn_ = std::move(fn); }

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
    void processPendingPistonCommits(std::int64_t now);
    void handleDoor(std::int32_t x, std::int32_t y, std::int32_t z);
    void setBlockAndBroadcast(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state);

    World& world_;
    BlockEntityStore* beStore_ = nullptr;
    std::int64_t* tickRef_ = nullptr;
    BlockTickScheduler* blockTicks_ = nullptr;
    void* gameServer_ = nullptr;
    std::function<void(std::int32_t,std::int32_t,std::int32_t,std::uint16_t)> broadcastFn_;
    struct PendingPistonCommit {
        std::int32_t pistonX=0, pistonY=0, pistonZ=0;
        std::int32_t hx=0, hy=0, hz=0;
        bool extend=false;
        int face=0;
        std::string facing;
        std::int64_t dueTick=0;
        struct Entry { std::int32_t x,y,z; std::uint16_t state; };
        std::vector<Entry> entries; // original positions/states for extend (or head for retract)
        std::vector<Entry> pullEntries; // sticky pull blocks (retract only, original states)
        int dx=0, dy=0, dz=0;
        bool isSticky=false;
    };
    std::vector<PistonEntity> pistonQueue_;
    std::vector<PendingPistonCommit> pendingPistonCommits_;
    std::priority_queue<RedstoneTick, std::vector<RedstoneTick>,
                        std::greater<RedstoneTick>> queue_;
    std::unordered_map<std::int64_t, std::int64_t> pendingRepeater_;
    std::unordered_map<std::int64_t, std::uint16_t> observerPrev_;
    std::unordered_map<std::int64_t, std::int64_t> observerPulseEnd_;
};

} // namespace cppfm
