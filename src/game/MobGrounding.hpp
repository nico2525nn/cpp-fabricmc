#pragma once

#include "Entities.hpp"
#include "../generated/BlockCollisionShapes.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

namespace cppfm::mob_grounding {

// Mob grounding uses the collision footprint belonging to the entity's
// 1.21.4 type. The tolerance only covers floating-point roundoff at a contact
// plane; it is intentionally too small to turn visibly airborne poses into
// grounded ones.
inline constexpr double kContactTolerance = 1.0e-7;

inline bool checkedFloor(const double value, const int padding,
                         std::int32_t& result) noexcept {
    if (!std::isfinite(value)) return false;
    const double floored = std::floor(value);
    const double minimum = static_cast<double>(
        std::numeric_limits<std::int32_t>::min()) + padding;
    const double maximum = static_cast<double>(
        std::numeric_limits<std::int32_t>::max()) - padding;
    if (floored < minimum || floored > maximum) return false;
    result = static_cast<std::int32_t>(floored);
    return true;
}

inline bool checkedCeil(const double value, const int padding,
                        std::int32_t& result) noexcept {
    if (!std::isfinite(value)) return false;
    return checkedFloor(std::ceil(value), padding, result);
}

template <typename GetBlock>
bool isGroundedAt(const double x, const double feetY, const double z,
                  GetBlock&& getBlock,
                  const double halfWidth) {
    if (!std::isfinite(x) || !std::isfinite(feetY) || !std::isfinite(z) ||
        !std::isfinite(halfWidth) || halfWidth <= 0.0 || halfWidth > 16.0)
        return false;

    constexpr double scale =
        1.0 / gen::kCollisionCoordinateUnitsPerBlock;
    const double minOffsetX = gen::kMinCollisionShapeOffsets[0] * scale;
    const double minOffsetY = gen::kMinCollisionShapeOffsets[1] * scale;
    const double minOffsetZ = gen::kMinCollisionShapeOffsets[2] * scale;
    const double maxOffsetX = gen::kMaxCollisionShapeOffsets[0] * scale;
    const double maxOffsetY = gen::kMaxCollisionShapeOffsets[1] * scale;
    const double maxOffsetZ = gen::kMaxCollisionShapeOffsets[2] * scale;

    std::int32_t minBlockX = 0;
    std::int32_t maxBlockX = 0;
    std::int32_t minBlockY = 0;
    std::int32_t maxBlockY = 0;
    std::int32_t minBlockZ = 0;
    std::int32_t maxBlockZ = 0;
    if (!checkedFloor(x - halfWidth - maxOffsetX, 2, minBlockX) ||
        !checkedCeil(x + halfWidth - minOffsetX, 2, maxBlockX) ||
        !checkedCeil(feetY - maxOffsetY - kContactTolerance, 2,
                     minBlockY) ||
        !checkedFloor(feetY - minOffsetY + kContactTolerance, 2,
                      maxBlockY) ||
        !checkedFloor(z - halfWidth - maxOffsetZ, 2, minBlockZ) ||
        !checkedCeil(z + halfWidth - minOffsetZ, 2, maxBlockZ))
        return false;
    ++minBlockX;
    --maxBlockX;
    ++minBlockZ;
    --maxBlockZ;
    if (minBlockX > maxBlockX || minBlockY > maxBlockY ||
        minBlockZ > maxBlockZ)
        return false;

    const double bodyMinX = x - halfWidth;
    const double bodyMaxX = x + halfWidth;
    const double bodyMinZ = z - halfWidth;
    const double bodyMaxZ = z + halfWidth;
    for (std::int32_t blockY = minBlockY;; ++blockY) {
        for (std::int32_t blockZ = minBlockZ;; ++blockZ) {
            for (std::int32_t blockX = minBlockX;; ++blockX) {
                const auto state = static_cast<std::uint32_t>(
                    std::invoke(getBlock, blockX, blockY, blockZ));
                for (const auto& shape : gen::collisionBoxesForState(state)) {
                    const double shapeMinX = blockX + shape.minX * scale;
                    const double shapeMaxX = blockX + shape.maxX * scale;
                    const double shapeMinY = blockY + shape.minY * scale;
                    const double shapeTopY = blockY + shape.maxY * scale;
                    const double shapeMinZ = blockZ + shape.minZ * scale;
                    const double shapeMaxZ = blockZ + shape.maxZ * scale;
                    if (bodyMaxX > shapeMinX && bodyMinX < shapeMaxX &&
                        bodyMaxZ > shapeMinZ && bodyMinZ < shapeMaxZ &&
                        std::abs(shapeTopY - feetY) <= kContactTolerance &&
                        feetY >= shapeMinY - kContactTolerance)
                        return true;
                }
                if (blockX == maxBlockX) break;
            }
            if (blockZ == maxBlockZ) break;
        }
        if (blockY == maxBlockY) break;
    }
    return false;
}

// Return the highest collision-box top under an entity footprint in a caller-
// supplied vertical interval. Teleport landing selection uses the same voxel
// geometry as the on-ground bit, so partial blocks do not add an arbitrary
// half/full block to the feet position.
template <typename GetBlock>
std::optional<double> highestSupportSurfaceY(
    const double x, const double z, const double minFeetY,
    const double maxFeetY, GetBlock&& getBlock, const double halfWidth) {
    if (!std::isfinite(x) || !std::isfinite(z) ||
        !std::isfinite(minFeetY) || !std::isfinite(maxFeetY) ||
        minFeetY > maxFeetY || !std::isfinite(halfWidth) ||
        halfWidth <= 0.0 || halfWidth > 16.0)
        return std::nullopt;

    constexpr double scale =
        1.0 / gen::kCollisionCoordinateUnitsPerBlock;
    const double minOffsetX = gen::kMinCollisionShapeOffsets[0] * scale;
    const double minOffsetY = gen::kMinCollisionShapeOffsets[1] * scale;
    const double minOffsetZ = gen::kMinCollisionShapeOffsets[2] * scale;
    const double maxOffsetX = gen::kMaxCollisionShapeOffsets[0] * scale;
    const double maxOffsetY = gen::kMaxCollisionShapeOffsets[1] * scale;
    const double maxOffsetZ = gen::kMaxCollisionShapeOffsets[2] * scale;

    std::int32_t minBlockX = 0;
    std::int32_t maxBlockX = 0;
    std::int32_t minBlockY = 0;
    std::int32_t maxBlockY = 0;
    std::int32_t minBlockZ = 0;
    std::int32_t maxBlockZ = 0;
    if (!checkedFloor(x - halfWidth - maxOffsetX, 2, minBlockX) ||
        !checkedCeil(x + halfWidth - minOffsetX, 2, maxBlockX) ||
        !checkedFloor(minFeetY - maxOffsetY - kContactTolerance, 2,
                      minBlockY) ||
        !checkedCeil(maxFeetY - minOffsetY + kContactTolerance, 2,
                     maxBlockY) ||
        !checkedFloor(z - halfWidth - maxOffsetZ, 2, minBlockZ) ||
        !checkedCeil(z + halfWidth - minOffsetZ, 2, maxBlockZ))
        return std::nullopt;
    ++minBlockX;
    --maxBlockX;
    ++minBlockZ;
    --maxBlockZ;
    if (minBlockX > maxBlockX || minBlockY > maxBlockY ||
        minBlockZ > maxBlockZ)
        return std::nullopt;

    const double bodyMinX = x - halfWidth;
    const double bodyMaxX = x + halfWidth;
    const double bodyMinZ = z - halfWidth;
    const double bodyMaxZ = z + halfWidth;
    std::optional<double> highest;
    for (std::int32_t blockY = minBlockY;; ++blockY) {
        for (std::int32_t blockZ = minBlockZ;; ++blockZ) {
            for (std::int32_t blockX = minBlockX;; ++blockX) {
                const auto state = static_cast<std::uint32_t>(
                    std::invoke(getBlock, blockX, blockY, blockZ));
                for (const auto& shape : gen::collisionBoxesForState(state)) {
                    const double shapeMinX = blockX + shape.minX * scale;
                    const double shapeMaxX = blockX + shape.maxX * scale;
                    const double shapeTopY = blockY + shape.maxY * scale;
                    const double shapeMinZ = blockZ + shape.minZ * scale;
                    const double shapeMaxZ = blockZ + shape.maxZ * scale;
                    if (bodyMaxX > shapeMinX && bodyMinX < shapeMaxX &&
                        bodyMaxZ > shapeMinZ && bodyMinZ < shapeMaxZ &&
                        shapeTopY >= minFeetY - kContactTolerance &&
                        shapeTopY <= maxFeetY + kContactTolerance &&
                        (!highest || shapeTopY > *highest))
                        highest = shapeTopY;
                }
                if (blockX == maxBlockX) break;
            }
            if (blockZ == maxBlockZ) break;
        }
        if (blockY == maxBlockY) break;
    }
    return highest;
}

template <typename GetBlock>
std::optional<double> teleportLandingSurfaceY(
    const MobEntity& mob, const double x, const double z,
    const double candidateFeetY, GetBlock&& getBlock) {
    // The old candidate search requires the block below the candidate to be
    // present. Its collision top can be as low as the prior block's bottom or
    // as high as a 1.5-block support shape's top.
    return highestSupportSurfaceY(
        x, z, candidateFeetY - 1.0, candidateFeetY + 0.5,
        std::forward<GetBlock>(getBlock), mobGroundingHalfWidth(mob));
}

template <typename GetBlock>
bool isMobGroundedAt(const MobEntity& mob, GetBlock&& getBlock) {
    return isGroundedAt(
        mob.x, mob.y, mob.z, std::forward<GetBlock>(getBlock),
        mobGroundingHalfWidth(mob));
}

} // namespace cppfm::mob_grounding
