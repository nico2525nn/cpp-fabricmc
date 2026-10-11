#pragma once

#include "World.hpp"
#include "../generated/BlockStates.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppfm {

inline bool isStairsBlock(const gen::BlockDef* block) {
    return block && std::string_view(block->name).find("_stairs") !=
                        std::string_view::npos;
}

inline std::string getPropStr(std::uint16_t state, const char* key) {
    for (const auto& [name, value] : gen::propsOf(state))
        if (name == key) return std::string(value);
    return {};
}

namespace stairs_detail {

inline char axisOf(std::string_view facing) {
    if (facing == "north" || facing == "south") return 'z';
    if (facing == "east" || facing == "west") return 'x';
    return 'y';
}

inline std::string opposite(std::string_view facing) {
    if (facing == "north") return "south";
    if (facing == "south") return "north";
    if (facing == "east") return "west";
    if (facing == "west") return "east";
    if (facing == "up") return "down";
    if (facing == "down") return "up";
    return std::string(facing);
}

inline std::string rotateCounterClockwise(std::string_view facing) {
    if (facing == "north") return "west";
    if (facing == "west") return "south";
    if (facing == "south") return "east";
    if (facing == "east") return "north";
    return std::string(facing);
}

inline bool horizontalOffset(std::string_view direction, int& dx, int& dz) {
    dx = 0;
    dz = 0;
    if (direction == "north") dz = -1;
    else if (direction == "south") dz = 1;
    else if (direction == "east") dx = 1;
    else if (direction == "west") dx = -1;
    else return false;
    return true;
}

} // namespace stairs_detail

inline bool isDifferentOrientation(World& world, int x, int y, int z,
                                   const std::string& stateFacing,
                                   const std::string& direction) {
    int dx = 0;
    int dz = 0;
    if (!stairs_detail::horizontalOffset(direction, dx, dz)) return true;

    const auto neighborState = world.getBlock(x + dx, y, z + dz);
    const auto* neighbor = gen::blockByState(neighborState);
    if (!isStairsBlock(neighbor)) return true;

    const auto neighborFacing = getPropStr(neighborState, "facing");
    if (stairs_detail::axisOf(neighborFacing) !=
        stairs_detail::axisOf(stateFacing))
        return true;
    if (neighborFacing == stairs_detail::opposite(stateFacing)) return true;
    if (neighborFacing == stateFacing) return false;
    return true;
}

inline std::string computeStairsShape(World& world, int x, int y, int z,
                                      const std::string& facing,
                                      const std::string& half) {
    int dx = 0;
    int dz = 0;
    if (stairs_detail::horizontalOffset(facing, dx, dz)) {
        const auto frontState = world.getBlock(x + dx, y, z + dz);
        const auto* front = gen::blockByState(frontState);
        if (isStairsBlock(front)) {
            const auto neighborFacing = getPropStr(frontState, "facing");
            const auto neighborHalf = getPropStr(frontState, "half");
            if (neighborHalf == half &&
                stairs_detail::axisOf(neighborFacing) !=
                    stairs_detail::axisOf(facing) &&
                isDifferentOrientation(world, x, y, z, facing,
                                      stairs_detail::opposite(neighborFacing))) {
                return neighborFacing ==
                               stairs_detail::rotateCounterClockwise(facing)
                    ? "outer_left" : "outer_right";
            }
        }
    }

    const auto backFacing = stairs_detail::opposite(facing);
    if (stairs_detail::horizontalOffset(backFacing, dx, dz)) {
        const auto backState = world.getBlock(x + dx, y, z + dz);
        const auto* back = gen::blockByState(backState);
        if (isStairsBlock(back)) {
            const auto neighborFacing = getPropStr(backState, "facing");
            const auto neighborHalf = getPropStr(backState, "half");
            if (neighborHalf == half &&
                stairs_detail::axisOf(neighborFacing) !=
                    stairs_detail::axisOf(facing) &&
                isDifferentOrientation(world, x, y, z, facing,
                                      neighborFacing)) {
                return neighborFacing ==
                               stairs_detail::rotateCounterClockwise(facing)
                    ? "inner_left" : "inner_right";
            }
        }
    }

    return "straight";
}

template <typename EmitBlockUpdate>
inline void updateNeighborStairsShapes(World& world, int x, int y, int z,
                                       EmitBlockUpdate&& emitBlockUpdate) {
    const auto updateNeighbor = [&](int neighborX, int neighborZ) {
        const auto state = world.getBlock(neighborX, y, neighborZ);
        const auto* definition = gen::blockByState(state);
        if (!isStairsBlock(definition)) return;

        const auto facing = getPropStr(state, "facing");
        const auto half = getPropStr(state, "half");
        const auto shape = computeStairsShape(
            world, neighborX, y, neighborZ, facing, half);
        if (getPropStr(state, "shape") == shape) return;

        std::vector<std::pair<std::string_view, std::string_view>> properties;
        for (const auto& [name, value] : gen::propsOf(state))
            if (name != "shape") properties.emplace_back(name, value);
        properties.emplace_back("shape", shape);

        const auto nextState = static_cast<std::uint16_t>(
            gen::stateWithProps(*definition, properties));
        if (!world.setBlock(neighborX, y, neighborZ, nextState)) return;

        // The emitter receives the dimension from the same World whose state
        // was mutated, preventing an implicit Overworld fallback.
        emitBlockUpdate(world.dimensionId(), neighborX, y, neighborZ,
                        nextState);
    };

    const auto placedState = world.getBlock(x, y, z);
    const auto* placed = gen::blockByState(placedState);
    if (!isStairsBlock(placed)) {
        updateNeighbor(x + 1, z);
        updateNeighbor(x - 1, z);
        updateNeighbor(x, z + 1);
        updateNeighbor(x, z - 1);
        return;
    }

    const auto facing = getPropStr(placedState, "facing");
    int dx = 0;
    int dz = 0;
    if (!stairs_detail::horizontalOffset(facing, dx, dz)) {
        updateNeighbor(x + 1, z);
        updateNeighbor(x - 1, z);
        updateNeighbor(x, z + 1);
        updateNeighbor(x, z - 1);
        return;
    }

    updateNeighbor(x + dx, z + dz);
    updateNeighbor(x - dx, z - dz);
}

} // namespace cppfm
