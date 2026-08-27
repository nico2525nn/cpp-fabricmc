// Redstone: wire-network power propagation with tick-delayed updates
// (plan3.md "レッドストーン"). Devices modelled:
//   * redstone_block  – constant power source (15)
//   * lever           – toggleable source, right-click interaction
//   * button          – pulsed source (30 ticks), right-click interaction
//   * redstone_wire   – conducts 15 → 1 across a connected network
//   * redstone_torch  – source; turns OFF when the block it stands on is
//                       strongly powered by an adjacent wire/lamp chain
//   * redstone_lamp   – lights while any adjacent wire carries power
//   * repeater        – output-only booster with 1-4 delay (facing aware)
#pragma once
#include <cstdint>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include "../game/World.hpp"
#include "../game/BlockEntities.hpp"

namespace cppfm {

struct RedstoneTick {
    std::int32_t x, y, z;
    std::int64_t dueTick;
    bool operator>(const RedstoneTick& o) const { return dueTick > o.dueTick; }
};

// MovingPiston entity (plan6 item 19): piston animation with progress 0-1
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

    // React to a block change: if the position or its neighbours host redstone
    // components the surrounding network is recomputed.
    void onBlockChanged(std::int32_t x, std::int32_t y, std::int32_t z);

    // Right-click interaction with levers/buttons; returns true when consumed.
    bool onInteract(std::int32_t x, std::int32_t y, std::int32_t z,
                    std::int64_t now);

    void tick(std::int64_t now);                         // delayed updates
    // True when any adjacent source/wire carries power (dispenser gates).
    bool isPoweredHere(std::int32_t x, std::int32_t y, std::int32_t z);

    void setBlockEntityStore(BlockEntityStore* s) { beStore_ = s; }
    void setTickRef(std::int64_t* t) { tickRef_ = t; }
    void setBlockTickScheduler(BlockTickScheduler* bts) { blockTicks_ = bts; }
    void setGameServer(void* srv) { gameServer_ = srv; }

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
    // Flood-fill wire power from all sources reachable within the network
    // containing the seed wire.
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

    World& world_;
    BlockEntityStore* beStore_ = nullptr;
    std::int64_t* tickRef_ = nullptr;
    BlockTickScheduler* blockTicks_ = nullptr;
    void* gameServer_ = nullptr;
    std::vector<PistonEntity> pistonQueue_;
    std::priority_queue<RedstoneTick, std::vector<RedstoneTick>,
                        std::greater<RedstoneTick>> queue_;
    std::unordered_map<std::int64_t, std::int64_t> pendingRepeater_;
    std::unordered_map<std::int64_t, std::uint16_t> observerPrev_;
    std::unordered_map<std::int64_t, std::int64_t> observerPulseEnd_;
};

} // namespace cppfm
