#pragma once

#include "../generated/BlockCollisionShapes.hpp"
#include "World.hpp"

#include <array>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <vector>

namespace cppfm::player_collision {

struct Aabb {
    double minX, minY, minZ;
    double maxX, maxY, maxZ;
};

inline Aabb playerBox(double x, double y, double z, double height) {
    constexpr double kHalfWidth = 0.3;
    return {x - kHalfWidth, y, z - kHalfWidth,
            x + kHalfWidth, y + height, z + kHalfWidth};
}

inline bool overlaps(const Aabb& lhs, const Aabb& rhs) {
    return lhs.maxX > rhs.minX && lhs.minX < rhs.maxX &&
           lhs.maxY > rhs.minY && lhs.minY < rhs.maxY &&
           lhs.maxZ > rhs.minZ && lhs.minZ < rhs.maxZ;
}

// An accepted player position can become embedded when a block is placed
// around them. In that case permit monotonic escape through the nearest face,
// but do not let a single client move use the overlap as permission to cross
// the whole shape. Ties are deterministic: X, then Y, then Z; negative face
// before positive face. While still embedded, no axis' penetration may grow.
inline bool canEscapeInitialOverlaps(const Aabb& start, const Aabb& finish,
                                    const std::vector<Aabb>& obstacles,
                                    double dx, double dy, double dz) {
    if (obstacles.empty()) return false;
    constexpr double kPenetrationTolerance = 1e-7;
    const std::array<double, 3> startMin = {start.minX, start.minY, start.minZ};
    const std::array<double, 3> startMax = {start.maxX, start.maxY, start.maxZ};
    const std::array<double, 3> finishMin = {
        finish.minX, finish.minY, finish.minZ};
    const std::array<double, 3> finishMax = {
        finish.maxX, finish.maxY, finish.maxZ};
    const std::array<double, 3> delta = {dx, dy, dz};

    std::size_t escapeAxis = 0;
    int escapeDirection = -1;
    double shortestEscape = std::numeric_limits<double>::infinity();
    for (std::size_t axis = 0; axis < startMin.size(); ++axis) {
        double unionMin = std::numeric_limits<double>::infinity();
        double unionMax = -std::numeric_limits<double>::infinity();
        for (const auto& obstacle : obstacles) {
            const std::array<double, 3> obstacleMin = {
                obstacle.minX, obstacle.minY, obstacle.minZ};
            const std::array<double, 3> obstacleMax = {
                obstacle.maxX, obstacle.maxY, obstacle.maxZ};
            unionMin = std::min(unionMin, obstacleMin[axis]);
            unionMax = std::max(unionMax, obstacleMax[axis]);
        }
        const double toNegativeFace = startMax[axis] - unionMin;
        if (toNegativeFace < shortestEscape) {
            shortestEscape = toNegativeFace;
            escapeAxis = axis;
            escapeDirection = -1;
        }
        const double toPositiveFace = unionMax - startMin[axis];
        if (toPositiveFace < shortestEscape) {
            shortestEscape = toPositiveFace;
            escapeAxis = axis;
            escapeDirection = 1;
        }
    }

    // Moving deeper along the selected minimum-translation axis is never an
    // escape, even if a large displacement would eventually emerge on the
    // opposite side of the obstacle.
    if (delta[escapeAxis] * escapeDirection < -kPenetrationTolerance)
        return false;

    bool finishOverlapsInitialObstacle = false;
    for (const auto& obstacle : obstacles) {
        const std::array<double, 3> obstacleMin = {
            obstacle.minX, obstacle.minY, obstacle.minZ};
        const std::array<double, 3> obstacleMax = {
            obstacle.maxX, obstacle.maxY, obstacle.maxZ};
        for (std::size_t axis = 0; axis < startMin.size(); ++axis) {
            const double startPenetration = std::min(
                startMax[axis], obstacleMax[axis]) -
                std::max(startMin[axis], obstacleMin[axis]);
            const double finishPenetration = std::max(0.0,
                std::min(finishMax[axis], obstacleMax[axis]) -
                std::max(finishMin[axis], obstacleMin[axis]));
            if (finishPenetration >
                startPenetration + kPenetrationTolerance)
                return false;
        }
        finishOverlapsInitialObstacle =
            finishOverlapsInitialObstacle || overlaps(finish, obstacle);
    }

    if (finishOverlapsInitialObstacle) return true;

    // Choose one shared exit face for the complete set of initial overlaps.
    // Per-obstacle faces can point in opposite directions for adjacent blocks,
    // otherwise leaving a player trapped between individually safe exits.
    double unionMin = std::numeric_limits<double>::infinity();
    double unionMax = -std::numeric_limits<double>::infinity();
    for (const auto& obstacle : obstacles) {
        const std::array<double, 3> obstacleMin = {
            obstacle.minX, obstacle.minY, obstacle.minZ};
        const std::array<double, 3> obstacleMax = {
            obstacle.maxX, obstacle.maxY, obstacle.maxZ};
        unionMin = std::min(unionMin, obstacleMin[escapeAxis]);
        unionMax = std::max(unionMax, obstacleMax[escapeAxis]);
    }
    if (escapeDirection < 0)
        return finishMax[escapeAxis] <=
               unionMin + kPenetrationTolerance;
    return finishMin[escapeAxis] >=
           unionMax - kPenetrationTolerance;
}

// Continuous collision test for a translated player AABB against one static
// voxel AABB. This prevents a client from stepping over a thin shape between
// two legal endpoint positions without introducing frame-rate-dependent
// samples.
inline bool sweptAabbOverlaps(const Aabb& moving, const Aabb& obstacle,
                              double dx, double dy, double dz) {
    double entry = -std::numeric_limits<double>::infinity();
    double exit = std::numeric_limits<double>::infinity();
    const auto clipAxis = [&entry, &exit](double movingMin, double movingMax,
                                         double obstacleMin,
                                         double obstacleMax, double delta) {
        if (delta == 0.0)
            return movingMax > obstacleMin && movingMin < obstacleMax;

        double axisEntry;
        double axisExit;
        if (delta > 0.0) {
            axisEntry = (obstacleMin - movingMax) / delta;
            axisExit = (obstacleMax - movingMin) / delta;
        } else {
            axisEntry = (obstacleMax - movingMin) / delta;
            axisExit = (obstacleMin - movingMax) / delta;
        }
        entry = std::max(entry, axisEntry);
        exit = std::min(exit, axisExit);
        return entry < exit;
    };

    if (!clipAxis(moving.minX, moving.maxX, obstacle.minX, obstacle.maxX, dx) ||
        !clipAxis(moving.minY, moving.maxY, obstacle.minY, obstacle.maxY, dy) ||
        !clipAxis(moving.minZ, moving.maxZ, obstacle.minZ, obstacle.maxZ, dz))
        return false;
    return exit > 0.0 && entry < 1.0;
}

inline bool solidBlock(std::uint16_t state) {
    return !gen::collisionBoxesForState(state).empty();
}

inline bool intersectsBlockShape(std::uint32_t state, int blockX, int blockY,
                                 int blockZ, const Aabb& box) {
    constexpr double kCoordinateScale =
        1.0 / gen::kCollisionCoordinateUnitsPerBlock;
    for (const auto& shapeBox : gen::collisionBoxesForState(state)) {
        const double shapeMinX = blockX + shapeBox.minX * kCoordinateScale;
        const double shapeMinY = blockY + shapeBox.minY * kCoordinateScale;
        const double shapeMinZ = blockZ + shapeBox.minZ * kCoordinateScale;
        const double shapeMaxX = blockX + shapeBox.maxX * kCoordinateScale;
        const double shapeMaxY = blockY + shapeBox.maxY * kCoordinateScale;
        const double shapeMaxZ = blockZ + shapeBox.maxZ * kCoordinateScale;
        if (box.maxX > shapeMinX && box.minX < shapeMaxX &&
            box.maxY > shapeMinY && box.minY < shapeMaxY &&
            box.maxZ > shapeMinZ && box.minZ < shapeMaxZ)
            return true;
    }
    return false;
}

template <typename GetBlock>
    requires std::invocable<GetBlock&, int, int, int>
inline bool intersectsAt(GetBlock&& getBlock, double x, double y, double z,
                         double height = 1.8) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(height) || height <= 0.0 || height > 3.0)
        return true;
    constexpr double kEpsilon = 1e-7;
    constexpr double kCoordinateScale =
        1.0 / gen::kCollisionCoordinateUnitsPerBlock;
    const Aabb box = playerBox(x, y, z, height);
    const std::array<double, 3> boxMin = {box.minX, box.minY, box.minZ};
    const std::array<double, 3> boxMax = {box.maxX, box.maxY, box.maxZ};
    std::array<int, 3> minBlock{};
    std::array<int, 3> maxBlock{};
    for (std::size_t axis = 0; axis < boxMin.size(); ++axis) {
        const double firstCandidate = std::floor(
            boxMin[axis] - gen::kMaxCollisionShapeOffsets[axis] *
                kCoordinateScale + kEpsilon) + 1.0;
        const double lastCandidate = std::ceil(
            boxMax[axis] - gen::kMinCollisionShapeOffsets[axis] *
                kCoordinateScale - kEpsilon) - 1.0;
        if (firstCandidate < std::numeric_limits<int>::min() + 2.0 ||
            lastCandidate > std::numeric_limits<int>::max() - 2.0)
            return true;
        minBlock[axis] = static_cast<int>(firstCandidate);
        maxBlock[axis] = static_cast<int>(lastCandidate);
    }
    if (minBlock[0] < std::numeric_limits<int>::min() + 2 ||
        minBlock[1] < std::numeric_limits<int>::min() + 2 ||
        minBlock[2] < std::numeric_limits<int>::min() + 2 ||
        maxBlock[0] > std::numeric_limits<int>::max() - 2 ||
        maxBlock[1] > std::numeric_limits<int>::max() - 2 ||
        maxBlock[2] > std::numeric_limits<int>::max() - 2)
        return true;

    for (int by = minBlock[1]; by <= maxBlock[1]; ++by) {
        for (int bz = minBlock[2]; bz <= maxBlock[2]; ++bz) {
            for (int bx = minBlock[0]; bx <= maxBlock[0]; ++bx) {
                const auto state = getBlock(bx, by, bz);
                if (intersectsBlockShape(state, bx, by, bz, box)) return true;
            }
        }
    }
    return false;
}

inline bool intersects(const World& world, double x, double y, double z,
                       double height = 1.8) {
    return intersectsAt([&world](int bx, int by, int bz) {
        return world.getBlock(bx, by, bz);
    }, x, y, z, height);
}

template <typename GetBlock>
    requires std::invocable<GetBlock&, int, int, int>
inline bool intersectsSwept(GetBlock&& getBlock,
                            double oldX, double oldY, double oldZ,
                            double newX, double newY, double newZ,
                            double height = 1.8) {
    if (!std::isfinite(oldX) || !std::isfinite(oldY) ||
        !std::isfinite(oldZ) || !std::isfinite(newX) ||
        !std::isfinite(newY) || !std::isfinite(newZ) ||
        !std::isfinite(height) || height <= 0.0 || height > 3.0)
        return true;

    const double dx = newX - oldX;
    const double dy = newY - oldY;
    const double dz = newZ - oldZ;
    if (dx * dx + dy * dy + dz * dz > 100.0) return true;

    constexpr double kEpsilon = 1e-7;
    constexpr double kCoordinateScale =
        1.0 / gen::kCollisionCoordinateUnitsPerBlock;
    const Aabb start = playerBox(oldX, oldY, oldZ, height);
    const Aabb finish = playerBox(newX, newY, newZ, height);
    const std::array<double, 3> sweptMin = {
        std::min(start.minX, finish.minX),
        std::min(start.minY, finish.minY),
        std::min(start.minZ, finish.minZ),
    };
    const std::array<double, 3> sweptMax = {
        std::max(start.maxX, finish.maxX),
        std::max(start.maxY, finish.maxY),
        std::max(start.maxZ, finish.maxZ),
    };
    std::array<int, 3> minBlock{};
    std::array<int, 3> maxBlock{};
    for (std::size_t axis = 0; axis < sweptMin.size(); ++axis) {
        const double firstCandidate = std::floor(
            sweptMin[axis] - gen::kMaxCollisionShapeOffsets[axis] *
                kCoordinateScale + kEpsilon) + 1.0;
        const double lastCandidate = std::ceil(
            sweptMax[axis] - gen::kMinCollisionShapeOffsets[axis] *
                kCoordinateScale - kEpsilon) - 1.0;
        if (firstCandidate < std::numeric_limits<int>::min() + 2.0 ||
            lastCandidate > std::numeric_limits<int>::max() - 2.0)
            return true;
        minBlock[axis] = static_cast<int>(firstCandidate);
        maxBlock[axis] = static_cast<int>(lastCandidate);
    }
    if (minBlock[0] < std::numeric_limits<int>::min() + 2 ||
        minBlock[1] < std::numeric_limits<int>::min() + 2 ||
        minBlock[2] < std::numeric_limits<int>::min() + 2 ||
        maxBlock[0] > std::numeric_limits<int>::max() - 2 ||
        maxBlock[1] > std::numeric_limits<int>::max() - 2 ||
        maxBlock[2] > std::numeric_limits<int>::max() - 2)
        return true;

    std::vector<Aabb> initialOverlaps;
    for (int by = minBlock[1]; by <= maxBlock[1]; ++by) {
        for (int bz = minBlock[2]; bz <= maxBlock[2]; ++bz) {
            for (int bx = minBlock[0]; bx <= maxBlock[0]; ++bx) {
                for (const auto& shapeBox : gen::collisionBoxesForState(
                         getBlock(bx, by, bz))) {
                    const Aabb obstacle{
                        bx + shapeBox.minX * kCoordinateScale,
                        by + shapeBox.minY * kCoordinateScale,
                        bz + shapeBox.minZ * kCoordinateScale,
                        bx + shapeBox.maxX * kCoordinateScale,
                        by + shapeBox.maxY * kCoordinateScale,
                        bz + shapeBox.maxZ * kCoordinateScale,
                    };
                    if (overlaps(start, obstacle)) {
                        initialOverlaps.push_back(obstacle);
                    } else if (sweptAabbOverlaps(
                                   start, obstacle, dx, dy, dz)) {
                        return true;
                    }
                }
            }
        }
    }
    return !initialOverlaps.empty() &&
           !canEscapeInitialOverlaps(
               start, finish, initialOverlaps, dx, dy, dz);
}

inline bool intersectsSwept(const World& world,
                            double oldX, double oldY, double oldZ,
                            double newX, double newY, double newZ,
                            double height = 1.8) {
    return intersectsSwept([&world](int bx, int by, int bz) {
        return world.getBlock(bx, by, bz);
    }, oldX, oldY, oldZ, newX, newY, newZ, height);
}

}  // namespace cppfm::player_collision
