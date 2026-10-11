#pragma once

#include "Heightmaps.hpp"
#include "World.hpp"

#include <cmath>
#include <cstdint>

namespace cppfm::player_collision {

inline bool solidBlock(std::uint16_t state) {
    if (block_state_collision::hasKnownEmptyCollisionShape(state)) return false;
    return isMotionBlocking(state);
}

inline bool intersects(const World& world, double x, double y, double z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return true;
    constexpr double kHalfWidth = 0.3;
    constexpr double kHeight = 1.8;
    constexpr double kEpsilon = 1e-7;
    const int minX = static_cast<int>(std::floor(x - kHalfWidth + kEpsilon));
    const int maxX = static_cast<int>(std::floor(x + kHalfWidth - kEpsilon));
    const int minY = static_cast<int>(std::floor(y + kEpsilon));
    const int maxY = static_cast<int>(std::floor(y + kHeight - kEpsilon));
    const int minZ = static_cast<int>(std::floor(z - kHalfWidth + kEpsilon));
    const int maxZ = static_cast<int>(std::floor(z + kHalfWidth - kEpsilon));
    for (int by = minY; by <= maxY; ++by)
        for (int bz = minZ; bz <= maxZ; ++bz)
            for (int bx = minX; bx <= maxX; ++bx)
                if (solidBlock(world.getBlock(bx, by, bz))) return true;
    return false;
}

}  // namespace cppfm::player_collision
