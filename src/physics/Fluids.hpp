// Fluids: event-driven water/lava simulation (plan3.md "水流シミュレーション").
#pragma once
#include <cstdint>
#include <queue>
#include <vector>
#include <string>
#include "../game/World.hpp"

namespace cppfm {

class LightEngine;

struct FluidTick {
    std::int32_t x, y, z;
    std::int64_t dueTick;
    bool operator>(const FluidTick& o) const { return dueTick > o.dueTick; }
};

enum class FluidId { Empty, Water, Lava };
struct FluidState { FluidId id = FluidId::Empty; int level = 0; bool falling = false; bool isWater() const { return id==FluidId::Water; } bool isLava() const { return id==FluidId::Lava; } };

class FluidSim {
public:
    explicit FluidSim(World& world) : world_(world) {}

    void touch(std::int32_t x, std::int32_t y, std::int32_t z);
    void tick(std::int64_t now);
    std::size_t pending() const { return queue_.size(); }
    static FluidState getFluidState(World& w, std::int32_t x, std::int32_t y, std::int32_t z);
    void checkInteraction(World& w, std::int32_t x, std::int32_t y, std::int32_t z, FluidState a, FluidState b);
    bool canConvertToSource(std::int32_t x, std::int32_t y, std::int32_t z);

private:
    enum class Kind { Water, Lava };
    static constexpr int kWater = 0;
    static constexpr int kLava = 1;
    int kindAt(std::uint16_t state, int& levelOut) const;
    std::uint16_t fluidState(Kind k, int level) const;
    void apply(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t now);
    void schedule(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t at) {
        queue_.push({x, y, z, at});
    }
    World& world_;
    std::priority_queue<FluidTick, std::vector<FluidTick>, std::greater<FluidTick>> queue_;
};

class WaterloggableHelper {
public:
    static bool isWaterloggable(const std::string& blockName);
    static bool isWaterloggable(std::uint16_t state);
    static bool getWaterlogged(std::uint16_t state);
    static std::uint16_t withWaterlogged(std::uint16_t state, bool v);
};

} // namespace cppfm
