// Shared 1.21.4 heightmap predicates and Anvil packing.
#pragma once

#include "World.hpp"
#include "../generated/BlockCollisionShapes.hpp"
#include "../generated/BlockStates.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cppfm {

inline constexpr int kHeightmapBitsPerEntry = 9;
inline constexpr std::size_t kHeightmapColumnCount = 16 * 16;
inline constexpr std::size_t kHeightmapValuesPerLong =
    64 / kHeightmapBitsPerEntry;
inline constexpr std::size_t kAnvilHeightmapLongCount =
    (kHeightmapColumnCount + kHeightmapValuesPerLong - 1) /
    kHeightmapValuesPerLong;

inline bool isAirBlockState(std::uint32_t stateId) {
    // The generated state table also carries cave air and void air.
    return stateId == 0 || stateId == 13971 || stateId == 13972;
}

inline int columnSurface(const Chunk& chunk, int localX, int localZ,
                         DimensionGeometry geometry = kOverworldGeometry) {
    for (int worldY = geometry.maxY() - 1; worldY >= geometry.minY; --worldY) {
        const int storageY = geometry.storageY(worldY);
        const auto state = chunk.blockAt(
            Chunk::index(storageY >> 4, storageY & 15, localZ, localX));
        if (!isAirBlockState(state)) return worldY + 1 - geometry.minY;
    }
    return 0;
}

// Collision-shape data is generated for every 1.21.4 block state. Empty
// shapes are never treated as motion-blocking; this avoids a dangerous
// default-solid assumption for newly added or uncommon vanilla states.
// A small set of non-empty, non-blocking plant/utility shapes remains
// explicit because collision geometry and Heightmap's blocksMovement
// predicate are not identical for those blocks.
inline bool blocksMotion(std::uint32_t stateId) {
    const auto* def = gen::blockByState(stateId);
    if (!def) return false;
    const std::string_view name = def->name;
    if (gen::collisionBoxesForState(stateId).empty()) return false;
    if (name == "minecraft:snow") {
        for (const auto& [key, value] : gen::propsOf(stateId))
            if (key == "layers") return value != "1";
        return false;
    }

    // These states have a collision shape, but vanilla's blocksMovement
    // predicate excludes them. Fully empty shapes need no per-name entries.
    static constexpr std::array<std::string_view, 16> kNonMotionShapes = {
        "minecraft:bamboo", "minecraft:moss_carpet",
        "minecraft:pale_moss_carpet", "minecraft:big_dripleaf",
        "minecraft:repeater", "minecraft:comparator", "minecraft:ladder",
        "minecraft:chain", "minecraft:pointed_dripstone",
        "minecraft:pitcher_crop", "minecraft:cocoa",
        "minecraft:chorus_plant", "minecraft:chorus_flower",
        "minecraft:scaffolding", "minecraft:azalea",
        "minecraft:flowering_azalea"};
    if (std::find(kNonMotionShapes.begin(), kNonMotionShapes.end(), name) !=
        kNonMotionShapes.end())
        return false;

    // Leaves intentionally remain motion-blocking in this heightmap.
    return true;
}

inline bool isMotionBlocking(std::uint32_t stateId) {
    if (isAirBlockState(stateId)) return false;
    const auto* def = gen::blockByState(stateId);
    if (!def) return false;
    const std::string_view name = def->name;
    if (name == "minecraft:water" || name == "minecraft:lava" ||
        name == "minecraft:bubble_column")
        return true;
    for (const auto& [key, value] : gen::propsOf(stateId))
        if (key == "waterlogged" && value == "true") return true;
    return blocksMotion(stateId);
}

inline bool isLeavesBlockState(std::uint32_t stateId) {
    const auto* def = gen::blockByState(stateId);
    constexpr std::string_view suffix = "_leaves";
    return def && def->name.size() >= suffix.size() &&
           def->name.substr(def->name.size() - suffix.size()) == suffix;
}

// OCEAN_FLOOR uses blocksMotion, unlike MOTION_BLOCKING which also counts
// non-empty fluids. Waterlogged solid blocks therefore still count here.
inline bool isOceanFloor(std::uint32_t stateId) {
    return blocksMotion(stateId);
}

inline bool isMotionBlockingNoLeaves(std::uint32_t stateId) {
    return !isLeavesBlockState(stateId) && isMotionBlocking(stateId);
}

inline int columnMotionBlocking(const Chunk& chunk, int localX, int localZ,
                                DimensionGeometry geometry = kOverworldGeometry) {
    for (int worldY = geometry.maxY() - 1; worldY >= geometry.minY; --worldY) {
        const int storageY = geometry.storageY(worldY);
        const auto state = chunk.blockAt(
            Chunk::index(storageY >> 4, storageY & 15, localZ, localX));
        if (isMotionBlocking(state)) return worldY + 1 - geometry.minY;
    }
    return 0;
}

inline int columnOceanFloor(const Chunk& chunk, int localX, int localZ,
                            DimensionGeometry geometry = kOverworldGeometry) {
    for (int worldY = geometry.maxY() - 1; worldY >= geometry.minY; --worldY) {
        const int storageY = geometry.storageY(worldY);
        const auto state = chunk.blockAt(
            Chunk::index(storageY >> 4, storageY & 15, localZ, localX));
        if (isOceanFloor(state)) return worldY + 1 - geometry.minY;
    }
    return 0;
}

inline int columnMotionBlockingNoLeaves(const Chunk& chunk, int localX,
                                        int localZ,
                                        DimensionGeometry geometry = kOverworldGeometry) {
    for (int worldY = geometry.maxY() - 1; worldY >= geometry.minY; --worldY) {
        const int storageY = geometry.storageY(worldY);
        const auto state = chunk.blockAt(
            Chunk::index(storageY >> 4, storageY & 15, localZ, localX));
        if (isMotionBlockingNoLeaves(state)) return worldY + 1 - geometry.minY;
    }
    return 0;
}

template <typename HeightAt>
inline void packHeightmapGeneric(std::vector<std::int64_t>& out,
                                 HeightAt&& heightAt) {
    // Mojang's PackedIntegerArray stores seven 9-bit entries per long; entries
    // never straddle words, leaving one padding bit in each 64-bit word.
    out.assign(kAnvilHeightmapLongCount, 0);
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            const int index = z * 16 + x;
            const auto word = index / kHeightmapValuesPerLong;
            const auto shift = (index % kHeightmapValuesPerLong) *
                               kHeightmapBitsPerEntry;
            const auto value = static_cast<std::uint64_t>(
                heightAt(x, z) & ((1 << kHeightmapBitsPerEntry) - 1));
            out[word] |= static_cast<std::int64_t>(value << shift);
        }
}

inline void packHeightmaps(std::vector<std::int64_t>& worldSurface,
                           std::vector<std::int64_t>& motionBlocking,
                           const Chunk& chunk,
                           DimensionGeometry geometry = kOverworldGeometry) {
    packHeightmapGeneric(worldSurface, [&](int x, int z) {
        return columnSurface(chunk, x, z, geometry);
    });
    packHeightmapGeneric(motionBlocking, [&](int x, int z) {
        return columnMotionBlocking(chunk, x, z, geometry);
    });
}

struct PackedAnvilHeightmaps {
    std::vector<std::int64_t> worldSurfaceWg;
    std::vector<std::int64_t> worldSurface;
    std::vector<std::int64_t> oceanFloorWg;
    std::vector<std::int64_t> oceanFloor;
    std::vector<std::int64_t> motionBlocking;
    std::vector<std::int64_t> motionBlockingNoLeaves;
};

inline PackedAnvilHeightmaps packAnvilHeightmaps(
    const Chunk& chunk, DimensionGeometry geometry = kOverworldGeometry) {
    PackedAnvilHeightmaps packed;
    packHeightmapGeneric(packed.worldSurfaceWg, [&](int x, int z) {
        if (chunk.hasWorldgenHeightmaps())
            return static_cast<int>(
                chunk.worldSurfaceWg()[static_cast<std::size_t>(z * 16 + x)]);
        return columnSurface(chunk, x, z, geometry);
    });
    packHeightmapGeneric(packed.worldSurface, [&](int x, int z) {
        return columnSurface(chunk, x, z, geometry);
    });
    packHeightmapGeneric(packed.oceanFloorWg, [&](int x, int z) {
        if (chunk.hasWorldgenHeightmaps())
            return static_cast<int>(
                chunk.oceanFloorWg()[static_cast<std::size_t>(z * 16 + x)]);
        return columnOceanFloor(chunk, x, z, geometry);
    });
    packHeightmapGeneric(packed.oceanFloor, [&](int x, int z) {
        return columnOceanFloor(chunk, x, z, geometry);
    });
    packHeightmapGeneric(packed.motionBlocking, [&](int x, int z) {
        return columnMotionBlocking(chunk, x, z, geometry);
    });
    packHeightmapGeneric(packed.motionBlockingNoLeaves, [&](int x, int z) {
        return columnMotionBlockingNoLeaves(chunk, x, z, geometry);
    });
    return packed;
}

inline void captureWorldgenHeightmapColumn(
    ChunkBuildWriter& writer, Chunk::HeightmapValues& worldSurface,
    Chunk::HeightmapValues& oceanFloor, int localX, int localZ,
    DimensionGeometry geometry = kOverworldGeometry) {
    std::uint16_t surface = 0;
    std::uint16_t floor = 0;
    for (int worldY = geometry.maxY() - 1;
         worldY >= geometry.minY && (!surface || !floor); --worldY) {
        const int storageY = geometry.storageY(worldY);
        const auto state = writer.blocks.get(
            Chunk::index(storageY >> 4, storageY & 15, localZ, localX));
        if (!surface && !isAirBlockState(state))
            surface = static_cast<std::uint16_t>(worldY + 1 - geometry.minY);
        if (!floor && isOceanFloor(state))
            floor = static_cast<std::uint16_t>(worldY + 1 - geometry.minY);
    }
    const auto column = static_cast<std::size_t>(localZ * 16 + localX);
    worldSurface[column] = surface;
    oceanFloor[column] = floor;
}

inline void captureWorldgenHeightmaps(
    ChunkBuildWriter& writer,
    DimensionGeometry geometry = kOverworldGeometry) {
    Chunk::HeightmapValues worldSurface{};
    Chunk::HeightmapValues oceanFloor{};
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x)
            captureWorldgenHeightmapColumn(writer, worldSurface, oceanFloor,
                                           x, z, geometry);
    writer.setWorldgenHeightmaps(worldSurface, oceanFloor);
}

inline void packHeightmap(std::vector<std::int64_t>& out, const Chunk& chunk,
                          DimensionGeometry geometry = kOverworldGeometry) {
    packHeightmapGeneric(out, [&](int x, int z) {
        return columnSurface(chunk, x, z, geometry);
    });
}

} // namespace cppfm
