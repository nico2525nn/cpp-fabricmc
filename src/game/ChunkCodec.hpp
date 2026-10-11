// ChunkCodec: serializes chunks into the 1.21.4 LevelChunkWithLight wire format.
//
// Wire layout (verified byte-for-byte against captures of a reference server):
//   x: i32, z: i32
//   heightmaps: anonymous NBT compound { MOTION_BLOCKING: long[], WORLD_SURFACE: long[] }
//   dataLen: varint, data blob:
//         blockCount: i16
//         blocks: paletted container (min indirect bits = 4)
//         biomes: paletted container (min indirect bits = 1)
//   blockEntityCount: varint + packed block entity positions/type/update NBT
//   skyLightMask/blockLightMask/emptySkyLightMask/emptyBlockLightMask:
//       varint arrayLen + i64 words (bit N = section N)
//   skyLight / blockLight arrays: varint count of { varint len + bytes }
//
// Paletted container encoding (empirically confirmed against captures):
//   single-valued : 0x00, value varint, longCount varint (=0)
//   indirect      : bits, paletteSize varint, ids..., longCount varint, packed longs
//   direct        : bits(>8), longCount varint, packed global-id longs
#pragma once
#include "World.hpp"
#include "Heightmaps.hpp"
#include "BlockEntityChunkWire.hpp"
#include "../core/BitMath.hpp"
#include "../core/NBT.hpp"
#include <algorithm>
#include <atomic>
#include <climits>
#include <vector>

namespace cppfm {

// D5: live registry size for global bits (vanilla computes ceilLog2(registry size))
// Atomic override for tests / modded servers; 0 means fallback to spec.globalMaxId+1 (27865→15)
inline std::atomic<std::uint32_t> g_liveBlockRegistrySize{0};
inline void setLiveBlockRegistrySizeForTest(std::uint32_t v){ g_liveBlockRegistrySize.store(v, std::memory_order_relaxed); }
inline std::uint32_t liveBlockRegistrySize(){ return g_liveBlockRegistrySize.load(std::memory_order_relaxed); }
inline std::uint32_t worldLiveBlockRegistrySize(){ return liveBlockRegistrySize(); }
inline int gbitsFromRegistry(std::uint32_t liveSize, std::uint32_t fallbackMaxId){
    if(liveSize > 0) return ceilLog2(liveSize);
    return ceilLog2(fallbackMaxId + 1);
}

struct PaletteSpec {
    int minBits;                    // blocks:4, biomes:1
    bool allowDirect;               // blocks:true, biomes:false
    std::uint32_t globalMaxId;      // for direct mode (fallback when live size 0)
};

// Packs `entryCount` entries of `bits` width into longs (no straddling) after a varint count.
// entryCount is 4096 for blocks, 64 for biomes (D1 fix: biomes were incorrectly 4096).
template <typename Fn>
inline void writePackedEntries(WriteBuffer& out, int bits, int entryCount, Fn&& valueAt) {
    const int per = 64 / bits;
    const int nLongs = (entryCount + per - 1) / per;
    out.varint(nLongs);
    std::uint64_t cur = 0;
    int filled = 0;
    for (int i = 0; i < entryCount; ++i) {
        cur |= static_cast<std::uint64_t>(valueAt(i) & ((1ULL << bits) - 1)) << (filled * bits);
        if (++filled == per) { out.u64(cur); cur = 0; filled = 0; }
    }
    if (filled) out.u64(cur);
}
// Backward-compatible overload for blocks (4096 entries)
template <typename Fn>
inline void writePackedEntries(WriteBuffer& out, int bits, Fn&& valueAt) {
    writePackedEntries(out, bits, 4096, std::forward<Fn>(valueAt));
}

// Getter maps linear index 0..entryCount-1 -> state id (blocks: global id; biomes: registry index). entryCount 4096 for blocks, 64 for
// biomes (D1 fix). D5: global bits from live registry size (fallback 27865→15). D6: deterministic palette (vector linear, no hash salt).
template <typename Getter>
inline void writePalettedContainer(WriteBuffer& out, Getter&& get, const PaletteSpec& spec, int entryCount = 4096) {
    // D6: deterministic palette — first-appearance order via linear search (palette ≤256, no hash salt)
    std::vector<std::uint32_t> palette;
    palette.reserve(16);
    for (int i = 0; i < entryCount; ++i) {
        const std::uint32_t v = get(i);
        bool found = false;
        for (auto p : palette) if (p == v) { found = true; break; }
        if (!found) palette.push_back(v);
    }

    if (palette.size() <= 1) {                       // single valued
        out.u8(0);
        out.varint(palette.empty() ? 0 : static_cast<std::int32_t>(palette[0]));
        out.varint(0);                               // longCount ALWAYS present
        return;
    }

    int bits = std::max(spec.minBits, ceilLog2(static_cast<std::uint32_t>(palette.size())));
    if (spec.allowDirect && bits > 8) {              // direct/global ids
        const int gbits = gbitsFromRegistry(worldLiveBlockRegistrySize(), spec.globalMaxId);
        out.u8(static_cast<std::uint8_t>(gbits));
        writePackedEntries(out, gbits, entryCount, get);
        return;
    }
    out.u8(static_cast<std::uint8_t>(bits));         // indirect
    out.varint(static_cast<std::int32_t>(palette.size()));
    for (auto id : palette) out.varint(static_cast<std::int32_t>(id));
    // D6: deterministic index lookup via linear search (palette ≤16 typical)
    auto indexOf = [&](std::uint32_t v) -> std::uint32_t {
        for (size_t i = 0; i < palette.size(); ++i) if (palette[i] == v) return static_cast<std::uint32_t>(i);
        return 0;
    };
    writePackedEntries(out, bits, entryCount, [&](int i) { return indexOf(get(i)); });
}

// Sky-light bytes for a transition section (lit at/above surface).
inline void sectionSkyLight(std::vector<std::uint8_t>& out, const Chunk& c,
                            int storageSection, DimensionGeometry geometry) {
    out.assign(2048, 0);
    const int baseWY = geometry.worldSection(storageSection) * 16;
    for (int yi = 0; yi < 16; ++yi)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                const bool lit = (baseWY + yi - geometry.minY) >=
                                 columnSurface(c, x, z, geometry);
                if (!lit) continue;
                const int linear = yi * 256 + z * 16 + x;
                if (linear & 1) out[linear >> 1] |= 0xF0;
                else            out[linear >> 1] |= 0x0F;
            }
}

inline void writeMaskArray(WriteBuffer& out, const std::vector<std::int64_t>& mask) {
    // Vanilla serializes BitSet::toLongArray(), which omits trailing zero
    // words. In particular, an all-zero mask is a zero-length long array.
    std::size_t wordCount = mask.size();
    while (wordCount > 0 && mask[wordCount - 1] == 0) --wordCount;
    out.varint(static_cast<std::int32_t>(wordCount));
    for (std::size_t i = 0; i < wordCount; ++i) out.i64(mask[i]);
}
inline void writeLightArrays(WriteBuffer& out, const std::vector<std::vector<std::uint8_t>>& arrays) {
    out.varint(static_cast<std::int32_t>(arrays.size()));
    for (auto& a : arrays) {
        out.varint(static_cast<std::int32_t>(a.size()));
        out.raw(a.data(), a.size());
    }
}

// Section-data blob (24 sections), shared by chunk packets & golden tests.
inline void serializeSectionData(WriteBuffer& blob, const Chunk* chunk,
                                 std::uint32_t biomeRegistryIndex,
                                 DimensionGeometry geometry = kOverworldGeometry) {
    for (int slice = 0; slice < geometry.sectionCount(); ++slice) {
        const int storageSection = geometry.storageSection(
            geometry.minSection() + slice);
        std::size_t base = static_cast<std::size_t>(storageSection) * 4096;
        std::uint16_t nonAir = 0;
        for (int i = 0; i < 4096; ++i)
            if (chunk->blockAt(base + i)) ++nonAir;
        blob.i16(static_cast<std::int16_t>(nonAir));

        writePalettedContainer(blob,
            [&](int i) -> std::uint32_t {
                return chunk->blockAt(base + static_cast<std::size_t>(i));
            },
            PaletteSpec{4, true, gen::kMaxBlockStateId}, 4096);

        const std::size_t bBase = static_cast<std::size_t>(storageSection) * 64;
        bool uniform = true;
        const std::uint16_t first = chunk->biomes[bBase];
        for (std::size_t i = 1; i < 64; ++i)
            if (chunk->biomes[bBase + i] != first) { uniform = false; break; }
        if (uniform) {
            writePalettedContainer(blob,
                [&](int) -> std::uint32_t { return first; },
                PaletteSpec{1, false, 0}, 64);
        } else {
            writePalettedContainer(blob,
                [&](int i) -> std::uint32_t {
                    return chunk->biomes[bBase + static_cast<std::size_t>(i)];
                },
                PaletteSpec{1, false, 0}, 64);
        }
    }
}

inline void packHeightmapsNbt(WriteBuffer& out, const Chunk* chunk,
                              DimensionGeometry geometry = kOverworldGeometry) {
    std::vector<std::int64_t> ws, mo;
    packHeightmaps(ws, mo, *chunk, geometry);
    nbt::Writer w(out);
    w.rootCompound();
    w.namedLongArray("MOTION_BLOCKING", mo);
    w.namedLongArray("WORLD_SURFACE", ws);
    w.endCompound();
}

// Light payload shared by LevelChunkWithLight and UpdateLight packets. Uses engine-maintained arrays when available; falls back to the
// column heuristic for sky light and zeros for block light.
inline void serializeLightPayload(WriteBuffer& out, const Chunk& chunk,
                                  DimensionGeometry geometry = kOverworldGeometry) {
    const int sectionCount = geometry.sectionCount();
    // LightData addresses the section below minY at bit 0, physical sections
    // at bits 1..sectionCount, and the section above maxY at bit sectionCount+1.
    const int lightSectionCount = sectionCount + 2;
    const auto maskWords =
        static_cast<std::size_t>((lightSectionCount + 63) / 64);
    std::vector<std::int64_t> skyMask(maskWords, 0);
    std::vector<std::int64_t> blockMask(maskWords, 0);
    std::vector<std::int64_t> emptySkyMask(maskWords, 0);
    std::vector<std::int64_t> emptyBlockMask(maskWords, 0);
    std::vector<std::vector<std::uint8_t>> skyArrays;
    std::vector<std::vector<std::uint8_t>> blockArrays;
    const bool haveSky = geometry.hasSkylight && chunk.skyLightReady() &&
                         chunk.hasSkyLightStorage();
    const auto markBit = [](std::vector<std::int64_t>& mask, int bit) {
        const auto word = static_cast<std::size_t>(bit / 64);
        const auto offset = static_cast<unsigned>(bit % 64);
        mask[word] |= static_cast<std::int64_t>(std::uint64_t{1} << offset);
    };
    const auto appendFullSkySection = [&](int bit) {
        markBit(skyMask, bit);
        skyArrays.emplace_back(2048, 0xFF);
    };

    for (int bit = 0; bit < lightSectionCount; ++bit) {
        const int worldSection = geometry.minSection() + bit - 1;
        const int storageSection = geometry.storageSection(worldSection);
        const bool hasBackingSection =
            storageSection >= 0 && storageSection < kSectionsPerChunk;
        const bool belowWorld = bit == 0;
        const bool aboveWorld = bit == lightSectionCount - 1;

        // The no-skylight LightEngine is a dummy listener in vanilla and
        // returns no section data; its two sky masks therefore remain unset.
        if (geometry.hasSkylight) {
            if (belowWorld) {
                // The section below the build range has no sky contribution.
                markBit(emptySkyMask, bit);
            } else if (aboveWorld) {
                // Sky beyond the build ceiling is fully exposed.
                appendFullSkySection(bit);
            } else if (!haveSky) {
                const int slice = bit - 1;
                int minH = INT_MAX, maxH = INT_MIN;
                for (int z = 0; z < 16; ++z)
                    for (int x = 0; x < 16; ++x) {
                        const int h = columnSurface(chunk, x, z, geometry);
                        minH = std::min(minH, h);
                        maxH = std::max(maxH, h);
                    }
                const int secBot = slice * 16;
                const int secTop = secBot + 16;
                if (minH >= secTop) {
                    // The whole section is beneath every column surface.
                    markBit(emptySkyMask, bit);
                } else if (maxH <= secBot) {
                    // The whole section is above every column surface.
                    appendFullSkySection(bit);
                } else {
                    markBit(skyMask, bit);
                    std::vector<std::uint8_t> arr;
                    sectionSkyLight(arr, chunk, storageSection, geometry);
                    skyArrays.push_back(std::move(arr));
                }
            } else {
                const std::size_t base =
                    static_cast<std::size_t>(storageSection) * 4096;
                bool anyLit = false;
                std::vector<std::uint8_t> arr(2048, 0);
                for (int i = 0; i < 4096; i += 2) {
                    const std::uint8_t lo = chunk.skyLightAt(base + i);
                    const std::uint8_t hi = chunk.skyLightAt(base + i + 1);
                    arr[i >> 1] = static_cast<std::uint8_t>(lo | (hi << 4));
                    anyLit = anyLit || lo != 0 || hi != 0;
                }
                if (!anyLit) {
                    markBit(emptySkyMask, bit);
                } else {
                    markBit(skyMask, bit);
                    skyArrays.push_back(std::move(arr));
                }
            }
        }

        // Blocks only inhabit the physical world range. The one-section light
        // padding can overlap the fixed backing allocation in Nether/End.
        if (!hasBackingSection) {
            markBit(emptyBlockMask, bit);
            continue;
        }
        const std::size_t base =
            static_cast<std::size_t>(storageSection) * 4096;
        bool any = false;
        std::vector<std::uint8_t> arr(2048, 0);
        for (int i = 0; i < 4096; i += 2) {
            const std::uint8_t lo =
                Chunk::getNibble(chunk.blockLightNib, base + i);
            const std::uint8_t hi =
                Chunk::getNibble(chunk.blockLightNib, base + i + 1);
            arr[i >> 1] = static_cast<std::uint8_t>(lo | (hi << 4));
            any = any || lo != 0 || hi != 0;
        }
        if (any) {
            markBit(blockMask, bit);
            blockArrays.push_back(std::move(arr));
        } else {
            markBit(emptyBlockMask, bit);
        }
    }
    writeMaskArray(out, skyMask);
    writeMaskArray(out, blockMask);
    writeMaskArray(out, emptySkyMask);
    writeMaskArray(out, emptyBlockMask);
    writeLightArrays(out, skyArrays);
    writeLightArrays(out, blockArrays);
}

inline void serializeUpdateLightBody(WriteBuffer& out, std::int32_t cx,
                                     std::int32_t cz, const Chunk& chunk,
                                     DimensionGeometry geometry = kOverworldGeometry) {
    // L1: UpdateLight 0x2B chunkX/Z are varint (not i32) per Prismarine packet_update_light — lock verified.
    out.varint(cx);
    out.varint(cz);
    serializeLightPayload(out, chunk, geometry);
}

// Serializes LevelChunkWithLight body (packet id excluded). biomeRegistryIndex: this world's biome id inside the synced registry order.
// Full LevelChunkWithLight body given an already-locked chunk reference.
inline void serializeLevelChunkBody(WriteBuffer& out, std::int32_t cx, std::int32_t cz,
                                    const Chunk& chunk, std::uint32_t biomeRegistryIndex,
                                    DimensionGeometry geometry = kOverworldGeometry,
                                    const nbt::Value* blockEntities = nullptr) {
    out.i32(cx);
    out.i32(cz);

    packHeightmapsNbt(out, &chunk, geometry);

    WriteBuffer blob;
    serializeSectionData(blob, &chunk, biomeRegistryIndex, geometry);
    out.varint(static_cast<std::int32_t>(blob.data.size()));
    out.raw(blob.data.data(), blob.data.size());

    writeChunkBlockEntities(out, cx, cz, blockEntities);

    serializeLightPayload(out, chunk, geometry);
}

inline void serializeLevelChunk(WriteBuffer& out, std::int32_t cx, std::int32_t cz,
                                const World& world, std::uint32_t biomeRegistryIndex,
                                const std::string& biomeKeyForDebug = {}) {
    world.generateChunkIfMissing(cx, cz);
    const auto geometry = world.verticalGeometry();
    world.withChunk(cx, cz, [&](const Chunk& chunk) {
        serializeLevelChunkBody(out, cx, cz, chunk, biomeRegistryIndex,
                                geometry);
    });
}

} // namespace cppfm
