// Anvil persistence: chunk <-> NBT (1.21.4, DataVersion 4189) + region files.
#pragma once
#include "World.hpp"
#include "Heightmaps.hpp"
#include "RegionFile.hpp"
#include "../core/BitMath.hpp"
#include "../core/NBTValue.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace cppfm {

constexpr std::int32_t kDataVersion = 4189;      // 1.21.4
constexpr std::int32_t kMinYSections = -4;       // minY -64 / 16

// ------------------------------------------------------------- chunk -> NBT
inline nbt::Value blockStateEntry(std::uint16_t stateId) {
    const auto* definition = gen::blockByState(stateId);
    if (!definition)
        throw std::runtime_error("cannot persist an unknown block state id " +
                                 std::to_string(stateId));
    nbt::Value e = nbt::Value::makeCompound();
    e.set("Name", nbt::Value::makeString(std::string(definition->name)));
    // The palette's block name identifies only the block type. Properties
    // carry the state (open/facing/half/waterlogged/etc.) and are essential
    // for a lossless save/restart. Omitting Properties for the default state
    // is valid Anvil NBT and keeps old/default-only chunks compact.
    if (stateId != definition->defaultState) {
        nbt::Value properties = nbt::Value::makeCompound();
        for (const auto& [name, value] : gen::propsOf(stateId))
            properties.set(std::string(name),
                           nbt::Value::makeString(std::string(value)));
        e.set("Properties", std::move(properties));
    }
    return e;
}

enum class BlockStateEntryStatus { Decoded, Unsupported, Malformed };

inline BlockStateEntryStatus blockStateFromEntry(
    const nbt::Value& entry, std::uint32_t& stateId) {
    if (entry.tag != nbt::Compound)
        return BlockStateEntryStatus::Malformed;
    const auto* name = entry.get("Name");
    if (!name || name->tag != nbt::String)
        return BlockStateEntryStatus::Malformed;
    const auto namedState = gen::blockNameToState().find(name->str);
    if (namedState == gen::blockNameToState().end())
        return BlockStateEntryStatus::Unsupported;
    const auto* definition = gen::blockByState(namedState->second);
    if (!definition) return BlockStateEntryStatus::Malformed;

    const auto* properties = entry.get("Properties");
    if (!properties) {
        stateId = definition->defaultState;
        return BlockStateEntryStatus::Decoded;
    }
    if (properties->tag != nbt::Compound)
        return BlockStateEntryStatus::Malformed;
    if (properties->comp.size() > definition->propCount)
        return BlockStateEntryStatus::Unsupported;

    std::vector<std::pair<std::string_view, std::string_view>> requested;
    requested.reserve(properties->comp.size());
    for (const auto& [key, value] : properties->comp) {
        if (value.tag != nbt::String)
            return BlockStateEntryStatus::Malformed;
        if (std::any_of(requested.begin(), requested.end(),
                        [&](const auto& item) { return item.first == key; }))
            return BlockStateEntryStatus::Malformed;
        requested.emplace_back(key, value.str);
    }

    const auto resolved = gen::stateWithProps(*definition, requested);
    const auto actual = gen::propsOf(resolved);
    for (const auto& [key, value] : requested) {
        const auto found = std::find_if(actual.begin(), actual.end(),
            [&](const auto& item) { return item.first == key; });
        // stateWithProps deliberately defaults unknown inputs for general
        // gameplay callers; persistence must instead reject malformed or
        // future properties rather than silently loading a different state.
        if (found == actual.end() || found->second != value)
            return BlockStateEntryStatus::Unsupported;
    }
    stateId = resolved;
    return BlockStateEntryStatus::Decoded;
}

inline nbt::Value chunkToNBT(std::int32_t cx, std::int32_t cz,
                             const Chunk& chunk,
                             const std::string& biomeKey,
                             const std::unordered_map<std::uint16_t, std::string>* biomeIdxToKey = nullptr,
                             DimensionGeometry geometry = kOverworldGeometry) {
    namespace nv = nbt;
    nv::Value root = nv::Value::makeCompound();
    root.set("DataVersion", nv::Value::makeInt(kDataVersion));
    root.set("xPos", nv::Value::makeInt(cx));
    root.set("zPos", nv::Value::makeInt(cz));
    root.set("yPos", nv::Value::makeInt(geometry.minSection()));
    root.set("Status", nv::Value::makeString("minecraft:full"));
    root.set("LastUpdate", nv::Value::makeLong(0));
    // The Anvil projection below omits section light arrays; do not advertise
    // stale or implicitly valid persisted lighting to the next loader.
    root.set("isLightOn", nv::Value::makeByte(0));

    // Keep all six 1.21.4 persisted maps current. The two clientbound maps
    // are packed by the same shared algorithms used by the wire codec.
    const auto heightmaps = packAnvilHeightmaps(chunk, geometry);
    {
        nv::Value hms = nv::Value::makeCompound();
        const auto add = [&](const char* name,
                             const std::vector<std::int64_t>& values) {
            nv::Value map;
            map.tag = nv::LongArray;
            map.longArray = values;
            hms.set(name, std::move(map));
        };
        add("WORLD_SURFACE_WG", heightmaps.worldSurfaceWg);
        add("WORLD_SURFACE", heightmaps.worldSurface);
        add("OCEAN_FLOOR_WG", heightmaps.oceanFloorWg);
        add("OCEAN_FLOOR", heightmaps.oceanFloor);
        add("MOTION_BLOCKING", heightmaps.motionBlocking);
        add("MOTION_BLOCKING_NO_LEAVES", heightmaps.motionBlockingNoLeaves);
        root.set("Heightmaps", hms);
    }

    nv::Value sections = nv::Value::makeList(
        nv::Compound, static_cast<std::size_t>(geometry.sectionCount()));
    for (int slice = 0; slice < geometry.sectionCount(); ++slice) {
        const int worldSection = geometry.minSection() + slice;
        const int storageSection = geometry.storageSection(worldSection);
        const std::size_t base = static_cast<std::size_t>(storageSection) * 4096;
        // palette
        std::vector<std::uint32_t> pal;
        std::unordered_map<std::uint32_t, std::uint16_t> idx;
        for (int i = 0; i < 4096; ++i) {
            const std::uint32_t st = chunk.blockAt(base + i);
            if (!idx.count(st)) { idx.emplace(st, (std::uint16_t)pal.size()); pal.push_back(st); }
        }
        nv::Value sec = nv::Value::makeCompound();
        sec.set("Y", nv::Value::makeByte(static_cast<std::int8_t>(worldSection)));

        {   // biomes: per-cell palette from the chunk's biome cells
            nv::Value bio = nv::Value::makeCompound();
            // gather palette for this section
            std::vector<std::uint16_t> biomePalette;
            std::unordered_map<std::uint16_t, std::uint16_t> bidx;
            const std::size_t bBase = static_cast<std::size_t>(storageSection) * 64;
            for (std::size_t i = 0; i < 64; ++i) {
                const std::uint16_t v = chunk.biomes[bBase + i];
                if (!bidx.count(v)) {
                    bidx.emplace(v, static_cast<std::uint16_t>(biomePalette.size()));
                    biomePalette.push_back(v);
                }
            }
            nv::Value bp = nv::Value::makeList(nv::String);
            for (auto v : biomePalette) {
                std::string key = biomeKey;
                if (biomeIdxToKey) {
                    auto it = biomeIdxToKey->find(v);
                    if (it != biomeIdxToKey->end()) key = it->second;
                }
                bp.list.push_back(nv::Value::makeString(key));
            }
            bio.set("palette", bp);
            if (biomePalette.size() > 1) {
                const int bits = std::max(1, ceilLog2(
                    static_cast<std::uint32_t>(biomePalette.size())));
                const int per = 64 / bits;
                nv::Value data; data.tag = nv::LongArray;
                data.longArray.assign((64 + per - 1) / per, 0);
                for (int i = 0; i < 64; ++i) {
                    const std::uint16_t pi = bidx.at(chunk.biomes[bBase + i]);
                    data.longArray[i / per] |= static_cast<std::int64_t>(
                        static_cast<std::uint64_t>(pi) << ((i % per) * bits));
                }
                bio.set("data", data);
            }
            sec.set("biomes", bio);
        }
        {   // block_states
            nv::Value bs = nv::Value::makeCompound();
            nv::Value pl = nv::Value::makeList(nv::Compound, pal.size());
            for (auto id : pal) {
                pl.list.push_back(blockStateEntry(
                    static_cast<std::uint16_t>(id)));
            }
            bs.set("palette", pl);
            if (pal.size() > 1) {
                const int bits = std::max(4, ceilLog2((std::uint32_t)pal.size()));
                const int per = 64 / bits;
                nv::Value data; data.tag = nv::LongArray;
                data.longArray.assign((4096 + per - 1) / per, 0);
                for (int i = 0; i < 4096; ++i) {
                    const std::uint16_t pi = idx.at(chunk.blockAt(base + i));
                    data.longArray[i / per] |= static_cast<std::int64_t>(
                        static_cast<std::uint64_t>(pi) << ((i % per) * bits));
                }
                bs.set("data", data);
            }
            sec.set("block_states", bs);
        }
        sections.list.push_back(sec);
    }
    root.set("sections", sections);
    root.set("block_entities", nv::Value::makeList(nv::Compound));
    return root;
}

// ------------------------------------------------------------- NBT -> chunk
inline bool decodePaletteIndices(const nbt::Value* packedData,
                                 std::size_t valueCount,
                                 std::size_t paletteSize, int minimumBits,
                                 std::vector<std::uint32_t>& indices) {
    if (paletteSize == 0 || paletteSize >
                                std::numeric_limits<std::uint32_t>::max())
        return false;
    indices.assign(valueCount, 0);
    if (paletteSize == 1) {
        return !packedData ||
               (packedData->tag == nbt::LongArray &&
                packedData->longArray.empty());
    }
    if (!packedData || packedData->tag != nbt::LongArray) return false;

    const int bits = std::max(
        minimumBits,
        ceilLog2(static_cast<std::uint32_t>(paletteSize)));
    if (bits <= 0 || bits >= 32) return false;
    const std::size_t valuesPerLong = static_cast<std::size_t>(64 / bits);
    if (valuesPerLong == 0) return false;
    const std::size_t requiredLongs =
        (valueCount + valuesPerLong - 1) / valuesPerLong;
    if (packedData->longArray.size() != requiredLongs) return false;

    const std::uint64_t valueMask = (std::uint64_t{1} << bits) - 1;
    for (std::size_t i = 0; i < valueCount; ++i) {
        const auto word = static_cast<std::uint64_t>(
            packedData->longArray[i / valuesPerLong]);
        const auto shift = static_cast<unsigned>(
            (i % valuesPerLong) * static_cast<std::size_t>(bits));
        const auto paletteIndex = static_cast<std::uint32_t>(
            (word >> shift) & valueMask);
        if (paletteIndex >= paletteSize) return false;
        indices[i] = paletteIndex;
    }
    return true;
}

inline bool chunkFromNBT(const nbt::Value& root, ChunkBuildWriter& chunk,
                         const std::unordered_map<std::string, std::uint32_t>& biomeIds,
                         std::string& biomeOut,
                             const std::function<std::int32_t(const std::string&)>&
                                 keyToBiomeIndex = nullptr,
                         DimensionGeometry geometry = kOverworldGeometry,
                         std::optional<std::pair<std::int32_t, std::int32_t>>
                             expectedChunk = std::nullopt,
    bool* unsupportedBlockState = nullptr) {
    if (unsupportedBlockState) *unsupportedBlockState = false;
    const auto* status = root.get("Status");
    if (!status || status->tag != nbt::String ||
        status->str != "minecraft:full")
        return false;                                       // unfinished: fall back to gen
    const auto* xs = root.get("xPos"), * zs = root.get("zPos");
    if (!xs || xs->tag != nbt::Int || !zs || zs->tag != nbt::Int)
        return false;
    if (expectedChunk &&
        (xs->i != expectedChunk->first || zs->i != expectedChunk->second))
        return false;
    const auto* sections = root.get("sections");
    if (!sections || sections->tag != nbt::List) return false;

    // A later malformed section must not leave an earlier section's blocks
    // in the caller's build buffer: the caller reuses that same buffer for
    // procedural generation when persistence decoding is rejected.
    auto stagedChunk = std::make_unique<Chunk>();
    ChunkBuildWriter staged(*stagedChunk);
    std::array<bool, kSectionsPerChunk> seenSections{};

    for (const auto& section : sections->list) {
        if (section.tag != nbt::Compound) return false;
        const auto* sectionY = section.get("Y");
        if (!sectionY || sectionY->tag != nbt::Byte) return false;
        const int worldSection = sectionY->b;
        if (worldSection < geometry.minSection() ||
            worldSection >= geometry.minSection() + geometry.sectionCount())
            continue;

        const auto sectionIndex = static_cast<std::size_t>(
            worldSection - geometry.minSection());
        if (seenSections[sectionIndex]) return false;
        seenSections[sectionIndex] = true;

        const int storageSection = geometry.storageSection(worldSection);
        const auto base = static_cast<std::size_t>(storageSection) * 4096;
        if (const auto* blockStates = section.get("block_states")) {
            if (blockStates->tag != nbt::Compound) return false;
            const auto* palette = blockStates->get("palette");
            if (!palette || palette->tag != nbt::List ||
                palette->list.empty() || palette->list.size() > 4096)
                return false;

            std::vector<std::uint32_t> blockPalette;
            blockPalette.reserve(palette->list.size());
            for (const auto& entry : palette->list) {
                std::uint32_t state = 0;
                const auto status = blockStateFromEntry(entry, state);
                if (status != BlockStateEntryStatus::Decoded) {
                    if (unsupportedBlockState &&
                        status == BlockStateEntryStatus::Unsupported)
                        *unsupportedBlockState = true;
                    return false;
                }
                blockPalette.push_back(state);
            }

            const auto* packedData = blockStates->get("data");
            std::vector<std::uint32_t> indices;
            if (!decodePaletteIndices(packedData, 4096,
                                     blockPalette.size(), 4, indices))
                return false;
            for (std::size_t i = 0; i < indices.size(); ++i)
                staged.blocks.set(base + i, static_cast<std::uint16_t>(
                                                blockPalette[indices[i]]));
        }

        if (const auto* biomeStates = section.get("biomes")) {
            if (biomeStates->tag != nbt::Compound) return false;
            const auto* palette = biomeStates->get("palette");
            if (!palette || palette->tag != nbt::List ||
                palette->list.empty() || palette->list.size() > 64)
                return false;

            std::vector<std::uint16_t> biomePalette;
            biomePalette.reserve(palette->list.size());
            for (const auto& entry : palette->list) {
                if (entry.tag != nbt::String) return false;
                std::int32_t index = 0;
                if (keyToBiomeIndex) {
                    index = keyToBiomeIndex(entry.str);
                } else if (const auto found = biomeIds.find(entry.str);
                           found != biomeIds.end()) {
                    if (found->second >
                        std::numeric_limits<std::uint16_t>::max())
                        return false;
                    index = static_cast<std::int32_t>(found->second);
                }
                if (index < 0 ||
                    index > std::numeric_limits<std::uint16_t>::max())
                    return false;
                biomePalette.push_back(static_cast<std::uint16_t>(index));
            }

            const auto* packedData = biomeStates->get("data");
            std::vector<std::uint32_t> indices;
            if (!decodePaletteIndices(packedData, 64,
                                     biomePalette.size(), 1, indices))
                return false;
            const auto biomeBase = static_cast<std::size_t>(storageSection) * 64;
            for (std::size_t i = 0; i < indices.size(); ++i)
                staged.biomes.set(biomeBase + i, biomePalette[indices[i]]);
        }
    }
    // biome from first section's palette (uniform worlds only)
    std::string decodedBiome = biomeOut;
    if (!sections->list.empty()) {
        if (const auto* biomes = sections->list.front().get("biomes")) {
            if (const auto* palette = biomes->get("palette");
                palette && palette->tag == nbt::List &&
                !palette->list.empty() &&
                palette->list.front().tag == nbt::String)
                decodedBiome = palette->list.front().str;
        }
    }

    for (std::size_t i = 0; i < kSectionsPerChunk * 4096; ++i)
        chunk.blocks.set(i, staged.blocks.get(i));
    for (std::size_t i = 0; i < kSectionsPerChunk * 64; ++i)
        chunk.biomes.set(i, staged.biomes.get(i));
    biomeOut = std::move(decodedBiome);
    return true;
}

} // namespace cppfm
