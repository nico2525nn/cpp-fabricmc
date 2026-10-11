#pragma once

#include "Anvil.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cppfm {

// Apply a detached set of block-state updates to the ordinary Anvil chunk
// projection. Gameplay preflight and tests share this serializer so the
// sector-limit decision uses the same section palette and heightmap bytes as
// persistence, without publishing the candidate into World.
inline void projectBlockUpdatesToAnvilNbt(
    Chunk& candidateChunk, nbt::Value& root,
    std::int32_t chunkX, std::int32_t chunkZ,
    std::span<const World::BlockUpdate> updates,
    DimensionGeometry geometry) {
    if (updates.empty()) return;

    ChunkBuildWriter writer(candidateChunk);
    std::unordered_set<int> changedSections;
    changedSections.reserve(updates.size());
    for (const auto& update : updates) {
        if ((update.x >> 4) != chunkX || (update.z >> 4) != chunkZ ||
            !geometry.containsY(update.y) ||
            !gen::blockByState(update.state))
            throw std::invalid_argument(
                "Anvil block projection received an invalid update");
        const int storageY = geometry.storageY(update.y);
        const int storageSection = storageY >> 4;
        writer.blocks.set(
            Chunk::index(storageSection, storageY & 15,
                         update.z & 15, update.x & 15),
            update.state);
        changedSections.insert(geometry.worldSection(storageSection));
    }

    auto* sections = root.get("sections");
    if (!sections || sections->tag != nbt::List)
        throw std::runtime_error(
            "Anvil block projection has no section list");
    for (const int sectionY : changedSections) {
        auto section = std::find_if(
            sections->list.begin(), sections->list.end(),
            [sectionY](const nbt::Value& value) {
                const auto* y = value.get("Y");
                return value.tag == nbt::Compound && y &&
                       y->tag == nbt::Byte &&
                       static_cast<int>(y->b) == sectionY;
            });
        if (section == sections->list.end())
            throw std::runtime_error(
                "Anvil block projection section is missing");

        const auto storageSection = static_cast<std::size_t>(
            geometry.storageSection(sectionY));
        const std::size_t base = storageSection * 4096;
        std::vector<std::uint32_t> palette;
        palette.reserve(16);
        std::unordered_map<std::uint32_t, std::uint16_t> paletteIndices;
        paletteIndices.reserve(16);
        for (std::size_t index = 0; index < 4096; ++index) {
            const auto state = static_cast<std::uint32_t>(
                candidateChunk.blockAt(base + index));
            if (!paletteIndices.contains(state)) {
                const auto paletteIndex =
                    static_cast<std::uint16_t>(palette.size());
                paletteIndices.emplace(state, paletteIndex);
                palette.push_back(state);
            }
        }

        nbt::Value blockStates = nbt::Value::makeCompound();
        if (const auto* existing = section->get("block_states");
            existing && existing->tag == nbt::Compound)
            blockStates = *existing;
        nbt::Value entries = nbt::Value::makeList(nbt::Compound,
                                                  palette.size());
        for (const auto state : palette)
            entries.list.push_back(blockStateEntry(
                static_cast<std::uint16_t>(state)));
        blockStates.set("palette", std::move(entries));

        if (palette.size() > 1) {
            const int bits = std::max(
                4, ceilLog2(static_cast<std::uint32_t>(palette.size())));
            const auto valuesPerLong = static_cast<std::size_t>(64 / bits);
            nbt::Value data;
            data.tag = nbt::LongArray;
            data.longArray.assign((4096 + valuesPerLong - 1) / valuesPerLong,
                                  0);
            for (std::size_t index = 0; index < 4096; ++index) {
                const auto state = static_cast<std::uint32_t>(
                    candidateChunk.blockAt(base + index));
                const auto paletteIndex = paletteIndices.at(state);
                const auto shift = static_cast<unsigned>(
                    (index % valuesPerLong) * static_cast<std::size_t>(bits));
                data.longArray[index / valuesPerLong] |=
                    static_cast<std::int64_t>(
                        static_cast<std::uint64_t>(paletteIndex) << shift);
            }
            blockStates.set("data", std::move(data));
        } else {
            std::erase_if(blockStates.comp, [](const auto& field) {
                return field.first == "data";
            });
        }
        section->set("block_states", std::move(blockStates));
    }

    // WG maps retain the values captured by world generation; normal maps
    // follow the projected gameplay edits through the shared packer.
    const auto heightmaps = packAnvilHeightmaps(candidateChunk, geometry);
    auto* packedHeightmaps = root.get("Heightmaps");
    if (!packedHeightmaps || packedHeightmaps->tag != nbt::Compound) {
        root.set("Heightmaps", nbt::Value::makeCompound());
        packedHeightmaps = root.get("Heightmaps");
    }
    const auto setHeightmap = [packedHeightmaps](
        const char* name, const std::vector<std::int64_t>& values) {
        nbt::Value packed;
        packed.tag = nbt::LongArray;
        packed.longArray = values;
        packedHeightmaps->set(name, std::move(packed));
    };
    setHeightmap("WORLD_SURFACE_WG", heightmaps.worldSurfaceWg);
    setHeightmap("WORLD_SURFACE", heightmaps.worldSurface);
    setHeightmap("OCEAN_FLOOR_WG", heightmaps.oceanFloorWg);
    setHeightmap("OCEAN_FLOOR", heightmaps.oceanFloor);
    setHeightmap("MOTION_BLOCKING", heightmaps.motionBlocking);
    setHeightmap("MOTION_BLOCKING_NO_LEAVES",
                 heightmaps.motionBlockingNoLeaves);
}

} // namespace cppfm
