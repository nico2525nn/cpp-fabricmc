#pragma once
#include <cstdint>
#include <queue>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include "../game/World.hpp"

namespace cppfm {

class LightEngine;

struct FluidTick {
    std::int32_t x, y, z;
    std::int64_t dueTick;
    bool operator>(const FluidTick& o) const { return dueTick > o.dueTick; }
};

enum class FluidId { Empty, Water, Lava };
struct FluidState { FluidId id = FluidId::Empty; int level = 0; bool falling = false; bool isWater() const { return id==FluidId::Water; } bool isLava() const { return id==FluidId::Lava; } bool isStillWater() const { return id==FluidId::Water && level==0 && !falling; } };

class FluidSim {
public:
    explicit FluidSim(World& world) : world_(world) {}

    void touch(std::int32_t x, std::int32_t y, std::int32_t z);
    void tick(std::int64_t now);
    std::size_t pending() const;
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
    static std::uint64_t queueKey(std::int32_t x, std::int32_t y,
                                  std::int32_t z) noexcept {
        // Match Minecraft's 26/12/26 block-position packing.  All valid
        // world coordinates fit these fields, and the stable key lets the
        // scheduler discard stale duplicate entries after an earlier tick is
        // brought forward.
        return ((static_cast<std::uint64_t>(
                     static_cast<std::uint32_t>(x)) & 0x3FFFFFFULL) << 38) |
               ((static_cast<std::uint64_t>(
                     static_cast<std::uint32_t>(z)) & 0x3FFFFFFULL) << 12) |
               (static_cast<std::uint64_t>(y) & 0xFFFULL);
    }
    void schedule(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t at) {
        // Only the scheduling queue is shared between the World callback
        // thread and the simulation tick.  Never hold this lock while
        // apply() is touching World: World::setBlock may synchronously call
        // back into touch().
        std::lock_guard<std::mutex> lock(queueMutex_);
        const auto key = queueKey(x, y, z);
        const auto existing = scheduledDue_.find(key);
        if (existing != scheduledDue_.end() && existing->second <= at)
            return;
        scheduledDue_[key] = at;
        queue_.push({x, y, z, at});
    }
    World& world_;
    std::priority_queue<FluidTick, std::vector<FluidTick>, std::greater<FluidTick>> queue_;
    std::unordered_map<std::uint64_t, std::int64_t> scheduledDue_;
    mutable std::mutex queueMutex_;
};

class WaterloggableHelper {
public:
    static bool isWaterloggable(const std::string& blockName);
    static bool isWaterloggable(std::uint16_t state);
    static bool getWaterlogged(std::uint16_t state);
    static std::uint16_t withWaterlogged(std::uint16_t state, bool v);
};

} // namespace cppfm
