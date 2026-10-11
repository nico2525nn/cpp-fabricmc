#pragma once

#include "Constants.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace cppfm {

// Vanilla 1.21.4 EntityType.PLAYER max tracking range is 32 chunks. The
// tracker multiplies the type range by 16, applies the dedicated-server
// entity-broadcast-range-percentage using integer arithmetic, then caps it by
// the viewing player's effective view distance.
inline constexpr int kVanillaPlayerTrackingRangeChunks = 32;

inline int effectivePlayerTrackingRangeBlocks(
    int serverViewDistanceChunks, int clientViewDistanceChunks,
    int entityBroadcastRangePercentage) noexcept {
    const int viewDistanceChunks = std::clamp(
        std::min(serverViewDistanceChunks, clientViewDistanceChunks),
        constants::kViewDistanceMin, constants::kViewDistanceMax);
    const int percentage = std::clamp(
        entityBroadcastRangePercentage,
        constants::kEntityBroadcastRangePercentageMin,
        constants::kEntityBroadcastRangePercentageMax);

    constexpr int vanillaPlayerRangeBlocks =
        kVanillaPlayerTrackingRangeChunks * 16;
    // Match MinecraftDedicatedServer.adjustTrackingDistance(int): scale the
    // block distance first, using Java/C++ positive integer truncation.
    const int configuredRangeBlocks =
        vanillaPlayerRangeBlocks * percentage / 100;
    return std::min(configuredRangeBlocks, viewDistanceChunks * 16);
}

inline bool withinHorizontalPlayerTrackingRange(
    double subjectX, double subjectZ, double viewerX, double viewerZ,
    int rangeBlocks) noexcept {
    if (rangeBlocks < 0 || !std::isfinite(subjectX) ||
        !std::isfinite(subjectZ) || !std::isfinite(viewerX) ||
        !std::isfinite(viewerZ))
        return false;
    const double dx = subjectX - viewerX;
    const double dz = subjectZ - viewerZ;
    const double range = static_cast<double>(rangeBlocks);
    return dx * dx + dz * dz <= range * range;
}

inline std::optional<std::int32_t> playerTrackingChunkCoordinate(
    double blockCoordinate) noexcept {
    if (!std::isfinite(blockCoordinate)) return std::nullopt;
    const double chunkCoordinate = std::floor(blockCoordinate / 16.0);
    if (chunkCoordinate <
            static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        chunkCoordinate >
            static_cast<double>(std::numeric_limits<std::int32_t>::max()))
        return std::nullopt;
    return static_cast<std::int32_t>(chunkCoordinate);
}

struct PlayerTrackingCandidate {
    bool isSelf = false;
    bool viewerInPlay = false;
    bool subjectInPlay = false;
    bool sameDimension = false;
    bool subjectChunkSent = false;
    double viewerX = 0.0;
    double viewerZ = 0.0;
    double subjectX = 0.0;
    double subjectZ = 0.0;
    int rangeBlocks = 0;
};

inline bool shouldTrackPlayerEntity(
    const PlayerTrackingCandidate& candidate) noexcept {
    return !candidate.isSelf && candidate.viewerInPlay &&
           candidate.subjectInPlay && candidate.sameDimension &&
           candidate.subjectChunkSent &&
           withinHorizontalPlayerTrackingRange(
               candidate.subjectX, candidate.subjectZ,
               candidate.viewerX, candidate.viewerZ,
               candidate.rangeBlocks);
}

} // namespace cppfm
