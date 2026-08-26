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
#include "../game/World.hpp"

namespace cppfm {

struct RedstoneTick {
    std::int32_t x, y, z;
    std::int64_t dueTick;
    bool operator>(const RedstoneTick& o) const { return dueTick > o.dueTick; }
};

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

private:
    enum class Comp {
        None, Wire, LeverOn, LeverOff, ButtonOn, TorchOn, TorchOff,
        LampLit, LampOff, BlockSource, Repeater
    };

    static Comp classify(std::uint16_t state);
    static int maxEmissionFor(Comp c);
    void recomputeAround(std::int32_t x, std::int32_t y, std::int32_t z);
    void reactToPower(std::int32_t x, std::int32_t y, std::int32_t z);
    // Flood-fill wire power from all sources reachable within the network
    // containing the seed wire.
    void updateWireNetwork(std::int32_t sx, std::int32_t sy, std::int32_t sz);
    void setPoweredAt(std::int32_t x, std::int32_t y, std::int32_t z,
                      std::uint8_t level);

    World& world_;
    std::priority_queue<RedstoneTick, std::vector<RedstoneTick>,
                        std::greater<RedstoneTick>> queue_;
};

} // namespace cppfm
