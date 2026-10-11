#pragma once

#include "../generated/EntityTrackingRanges.hpp"
#include "Constants.hpp"
#include "Entities.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace cppfm {

inline int effectiveEntityTrackingViewDistanceChunks(
    int serverViewDistanceChunks, int clientViewDistanceChunks) noexcept {
    return std::clamp(std::min(serverViewDistanceChunks,
                               clientViewDistanceChunks),
                      constants::kViewDistanceMin,
                      constants::kViewDistanceMax);
}

inline std::uint8_t entityTrackingRangeChunks(MobKind kind) noexcept {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= gen::kEntityTrackingRanges1_21_4.size()) return 0;
    return gen::kEntityTrackingRanges1_21_4[index].chunks;
}

inline int effectiveEntityTrackingRangeBlocks(
    MobKind kind, int effectiveViewDistanceChunks,
    int broadcastRangePercentage = 100) noexcept {
    const int typeRange = entityTrackingRangeChunks(kind);
    const int viewRange = std::max(0, effectiveViewDistanceChunks);
    const int percentage = std::clamp(
        broadcastRangePercentage,
        constants::kEntityBroadcastRangePercentageMin,
        constants::kEntityBroadcastRangePercentageMax);
    // EntityType stores its base range in chunks, but vanilla converts that
    // range to blocks before MinecraftDedicatedServer.adjustTrackingDistance
    // applies the configured percentage. Keeping the multiplication order is
    // observable for odd chunk ranges and small percentages.
    const int typeRangeBlocks = typeRange * 16;
    const int configuredRangeBlocks = typeRangeBlocks * percentage / 100;
    return std::min(configuredRangeBlocks, viewRange * 16);
}

inline bool withinHorizontalEntityTrackingRange(
    double entityX, double entityZ, double viewerX, double viewerZ,
    int rangeBlocks) noexcept {
    if (rangeBlocks < 0 || !std::isfinite(entityX) ||
        !std::isfinite(entityZ) || !std::isfinite(viewerX) ||
        !std::isfinite(viewerZ))
        return false;
    const double dx = entityX - viewerX;
    const double dz = entityZ - viewerZ;
    const double range = static_cast<double>(rangeBlocks);
    return dx * dx + dz * dz <= range * range;
}

} // namespace cppfm
