// Fluids: event-driven water/lava simulation (plan3.md "水流シミュレーション").
// A priority queue of scheduled block ticks; spreading recomputes flow levels
// from neighbours exactly like the vanilla update-order-free model:
//   * source blocks keep level 0
//   * flowing water takes min(neighbour level) + 1 (max 7); falls downward
//     as a full-level stream
//   * lava mirrors this with double steps and slower ticks
//   * water meeting lava yields cobblestone / obsidian
#pragma once
#include <cstdint>
#include <queue>
#include <vector>
#include "../game/World.hpp"

namespace cppfm {

class LightEngine;

struct FluidTick {
    std::int32_t x, y, z;
    std::int64_t dueTick;
    bool operator>(const FluidTick& o) const { return dueTick > o.dueTick; }
};

class FluidSim {
public:
    explicit FluidSim(World& world) : world_(world) {}

    // Queue an update for a position (call after ANY block change there or in
    // its neighbourhood; redundant entries are harmless).
    void touch(std::int32_t x, std::int32_t y, std::int32_t z);

    // Process all ticks whose time has come; `now` is the server tick.
    void tick(std::int64_t now);

    std::size_t pending() const { return queue_.size(); }

private:
    enum class Kind { Water, Lava };

    static constexpr int kWater = 0;
    static constexpr int kLava = 1;

    // Returns 0 for water, 1 for lava, -1 otherwise; sets levelOut.
    int kindAt(std::uint16_t state, int& levelOut) const;
    std::uint16_t fluidState(Kind k, int level) const;
    void apply(std::int32_t x, std::int32_t y, std::int32_t z,
               std::int64_t now);
    void schedule(std::int32_t x, std::int32_t y, std::int32_t z,
                  std::int64_t at) {
        queue_.push({x, y, z, at});
    }

    World& world_;
    std::priority_queue<FluidTick, std::vector<FluidTick>,
                        std::greater<FluidTick>> queue_;
};

} // namespace cppfm
