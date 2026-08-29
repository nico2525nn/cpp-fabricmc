// ChunkCodec: serializes chunks into the 1.21.4 LevelChunkWithLight wire format.
//
// Wire layout (verified byte-for-byte against captures of a reference server):
//   x: i32, z: i32
//   heightmaps: anonymous NBT compound { MOTION_BLOCKING: long[], WORLD_SURFACE: long[] }
//   dataLen: varint, data blob:
//       for each of 24 sections:
//         blockCount: i16
//         blocks: paletted container (min indirect bits = 4)
//         biomes: paletted container (min indirect bits = 1)
//   blockEntityCount: varint (=0 here)
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
#include "../core/NBT.hpp"
#include <algorithm>
#include <atomic>
#include <climits>
#include <unordered_map>
#include <unordered_set>
#include <string_view>

namespace cppfm {

inline int ceilLog2(std::uint32_t v) { // smallest b with (1<<b) >= v ; v>=1
    int b = 0;
    while ((1u << b) < v) ++b;
    return b;
}

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

// Getter maps linear index 0..entryCount-1 -> state id (blocks: global id; biomes: registry index).
// entryCount 4096 for blocks, 64 for biomes (D1 fix).
// D5: global bits from live registry size (fallback 27865→15). D6: deterministic palette (vector linear, no hash salt).
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

// Highest non-air y within chunk (chunk-relative 0..383); 0 if empty column.
inline int columnSurface(const Chunk& c, int lx, int lz) {
    for (int wy = kSectionsPerChunk * 16 - 1; wy >= 0; --wy)
        if (c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] != 0)
            return wy + 1;
    return 0;
}

// D2: MOTION_BLOCKING vs WORLD_SURFACE distinction.
// WORLD_SURFACE = highest non-air (columnSurface)
// MOTION_BLOCKING = highest block that blocks motion or contains fluid (vanilla Heightmap.Type)
inline bool isMotionBlocking(std::uint32_t stateId) {
    if (stateId == 0) return false;
    // void/cave air (13971,13972) are air variants
    if (stateId == 13971 || stateId == 13972) return false;
    const auto* def = gen::blockByState(stateId);
    if (!def) return false;
    std::string_view name = def->name;
    // fluids are motion blocking (water/lava/bubble_column) – any level state shares same name
    if (name == "minecraft:water" || name == "minecraft:lava" || name == "minecraft:bubble_column")
        return true;
    // waterlogged blocks contain fluid
    {
        auto props = gen::propsOf(stateId);
        for (auto& kv : props) if (kv.first == "waterlogged" && kv.second == "true") return true;
    }
    if (name == "minecraft:cobweb" || name == "minecraft:bamboo_sapling") return false;
    // snow: layers=1 is non-solid (MOTION false), layers 2..8 is solid
    if (name == "minecraft:snow") {
        auto props = gen::propsOf(stateId);
        for (auto& kv : props) if (kv.first == "layers") return kv.second != "1";
        return false;
    }
    // extensive non-solid foliage / no-collision list (blocksMotion false)
    // Leaves are intentionally NOT here – they ARE motion blocking (MOTION counts leaves)
    static const std::unordered_set<std::string_view> kNonMotion = {
        "minecraft:short_grass","minecraft:fern","minecraft:dead_bush","minecraft:seagrass","minecraft:tall_seagrass",
        "minecraft:dandelion","minecraft:poppy","minecraft:blue_orchid","minecraft:allium","minecraft:azure_bluet",
        "minecraft:red_tulip","minecraft:orange_tulip","minecraft:white_tulip","minecraft:pink_tulip","minecraft:oxeye_daisy",
        "minecraft:cornflower","minecraft:wither_rose","minecraft:lily_of_the_valley","minecraft:brown_mushroom","minecraft:red_mushroom",
        "minecraft:tall_grass","minecraft:large_fern","minecraft:sunflower","minecraft:lilac","minecraft:rose_bush","minecraft:peony",
        "minecraft:vine","minecraft:glow_lichen","minecraft:resin_clump","minecraft:sugar_cane","minecraft:kelp","minecraft:kelp_plant",
        "minecraft:bamboo","minecraft:powder_snow","minecraft:moss_carpet","minecraft:pale_moss_carpet","minecraft:open_eyeblossom","minecraft:closed_eyeblossom",
        "minecraft:cave_vines","minecraft:cave_vines_plant","minecraft:spore_blossom","minecraft:pink_petals","minecraft:hanging_roots","minecraft:big_dripleaf","minecraft:big_dripleaf_stem","minecraft:small_dripleaf",
        "minecraft:sweet_berry_bush","minecraft:nether_sprouts","minecraft:warped_roots","minecraft:crimson_roots","minecraft:weeping_vines","minecraft:weeping_vines_plant","minecraft:twisting_vines","minecraft:twisting_vines_plant","minecraft:crimson_fungus","minecraft:warped_fungus",
        "minecraft:torch","minecraft:wall_torch","minecraft:soul_torch","minecraft:soul_wall_torch","minecraft:redstone_wire","minecraft:repeater","minecraft:comparator",
        "minecraft:ladder","minecraft:rail","minecraft:powered_rail","minecraft:detector_rail","minecraft:activator_rail","minecraft:lever",
        "minecraft:stone_button","minecraft:oak_button","minecraft:spruce_button","minecraft:birch_button","minecraft:jungle_button","minecraft:acacia_button","minecraft:cherry_button","minecraft:dark_oak_button","minecraft:pale_oak_button","minecraft:mangrove_button","minecraft:bamboo_button",
        "minecraft:crimson_button","minecraft:warped_button","minecraft:polished_blackstone_button",
        "minecraft:tripwire","minecraft:tripwire_hook","minecraft:chain","minecraft:pointed_dripstone","minecraft:light","minecraft:barrier","minecraft:structure_void",
        "minecraft:oak_sapling","minecraft:spruce_sapling","minecraft:birch_sapling","minecraft:jungle_sapling","minecraft:acacia_sapling","minecraft:cherry_sapling","minecraft:dark_oak_sapling","minecraft:pale_oak_sapling","minecraft:mangrove_propagule",
        "minecraft:wheat","minecraft:carrots","minecraft:potatoes","minecraft:beetroots","minecraft:torchflower_crop","minecraft:pitcher_crop","minecraft:nether_wart","minecraft:cocoa","minecraft:chorus_plant","minecraft:chorus_flower",
        "minecraft:scaffolding","minecraft:azalea","minecraft:flowering_azalea"
    };
    if (kNonMotion.count(name)) return false;
    // default: solid / motion blocking (includes leaves, logs, stone, glass, etc.)
    return true;
}

inline int columnMotionBlocking(const Chunk& c, int lx, int lz) {
    for (int wy = kSectionsPerChunk * 16 - 1; wy >= 0; --wy) {
        const auto id = c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)];
        if (isMotionBlocking(id)) return wy + 1;
    }
    return 0;
}

template <typename Fn>
inline void packHeightmapGeneric(std::vector<std::int64_t>& out, Fn&& heightAt) {
    constexpr int kBpe = 9;
    out.assign((256 * kBpe + 63) / 64, 0);
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            const int idx = z * 16 + x;
            const int bit = idx * kBpe;
            const int lo = bit & 63;
            const int wi = bit >> 6;
            const std::int64_t v = static_cast<std::int64_t>(heightAt(x, z) & ((1 << kBpe) - 1));
            out[wi] |= v << lo;
            if (lo + kBpe > 64 && wi + 1 < (int)out.size())
                out[wi + 1] |= v >> (64 - lo);
        }
}

inline void packHeightmap(std::vector<std::int64_t>& out, const Chunk& c) {
    packHeightmapGeneric(out, [&](int x, int z){ return columnSurface(c, x, z); });
}

// Sky-light bytes for a transition section (lit at/above surface).
inline void sectionSkyLight(std::vector<std::uint8_t>& out, const Chunk& c, int section) {
    out.assign(2048, 0);
    const int baseWY = section * 16;
    for (int yi = 0; yi < 16; ++yi)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                const bool lit = (baseWY + yi) >= columnSurface(c, x, z);
                if (!lit) continue;
                const int linear = yi * 256 + z * 16 + x;
                if (linear & 1) out[linear >> 1] |= 0xF0;
                else            out[linear >> 1] |= 0x0F;
            }
}

inline void writeMaskArray(WriteBuffer& out, const std::vector<std::int64_t>& mask) {
    out.varint(static_cast<std::int32_t>(mask.size()));
    for (auto w : mask) out.i64(w);
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
                                 std::uint32_t biomeRegistryIndex) {
    for (int s = 0; s < kSectionsPerChunk; ++s) {
        std::size_t base = static_cast<std::size_t>(s) * 4096;
        std::uint16_t nonAir = 0;
        for (int i = 0; i < 4096; ++i)
            if (chunk->blocks[base + i]) ++nonAir;
        blob.i16(static_cast<std::int16_t>(nonAir));

        writePalettedContainer(blob,
            [&](int i) -> std::uint32_t { return chunk->blocks[base + static_cast<std::size_t>(i)]; },
            PaletteSpec{4, true, gen::kMaxBlockStateId}, 4096);

        const std::size_t bBase = static_cast<std::size_t>(s) * 64;
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

inline void packHeightmapsNbt(WriteBuffer& out, const Chunk* chunk) {
    std::vector<std::int64_t> ws(36), mo(36);
    packHeightmapGeneric(ws, [&](int x,int z){ return columnSurface(*chunk, x, z); });
    packHeightmapGeneric(mo, [&](int x,int z){ return columnMotionBlocking(*chunk, x, z); });
    nbt::Writer w(out);
    w.rootCompound();
    w.namedLongArray("MOTION_BLOCKING", mo);
    w.namedLongArray("WORLD_SURFACE", ws);
    w.endCompound();
}

// Light payload shared by LevelChunkWithLight and UpdateLight packets.
// Uses engine-maintained arrays when available; falls back to the column
// heuristic for sky light and zeros for block light.
inline void serializeLightPayload(WriteBuffer& out, const Chunk& chunk) {
    std::vector<std::int64_t> skyMask((kSectionsPerChunk + 63) / 64, 0);
    std::vector<std::int64_t> blockMask((kSectionsPerChunk + 63) / 64, 0);
    std::vector<std::int64_t> emptySkyMask((kSectionsPerChunk + 63) / 64, 0);
    std::vector<std::int64_t> emptyBlockMask((kSectionsPerChunk + 63) / 64, 0);
    std::vector<std::vector<std::uint8_t>> skyArrays;
    std::vector<std::vector<std::uint8_t>> blockArrays;
    const bool haveSky = static_cast<bool>(chunk.skyLight);

    for (int s = 0; s < kSectionsPerChunk; ++s) {
        const std::size_t base = static_cast<std::size_t>(s) * 4096;
        bool anyBlock = false;
        if (haveSky || true) {}
        // scan arrays (or heuristic) per section
        int minH = INT_MAX, maxH = INT_MIN;
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                int h = columnSurface(chunk, x, z);
                minH = std::min(minH, h); maxH = std::max(maxH, h);
            }
        const int secBot = s * 16, secTop = s * 16 + 16;

        // ---- sky section classification
        if (!haveSky) {
            if (maxH <= secBot) emptySkyMask[s / 64] |= 1LL << (s % 64);
            else if (minH < secTop) {
                skyMask[s / 64] |= 1LL << (s % 64);
                std::vector<std::uint8_t> arr;
                sectionSkyLight(arr, chunk, s);
                skyArrays.push_back(std::move(arr));
            }
        } else {
            bool anyLit = false, allZero = true;
            std::vector<std::uint8_t> arr(2048, 0);
            for (int i = 0; i < 4096; i += 2) {
                const std::uint8_t lo = Chunk::getNibble(*chunk.skyLight, base + i);
                const std::uint8_t hi = Chunk::getNibble(*chunk.skyLight, base + i + 1);
                const std::uint8_t byte = static_cast<std::uint8_t>(lo | (hi << 4));
                arr[i >> 1] = byte;
                if (lo || hi) { anyLit = true; allZero = false; }
            }
            if (!anyLit || allZero) {
                emptySkyMask[s / 64] |= 1LL << (s % 64);
            } else {
                skyMask[s / 64] |= 1LL << (s % 64);
                skyArrays.push_back(std::move(arr));
            }
        }

        // ---- block light section
        {
            bool any = false;
            std::vector<std::uint8_t> arr(2048, 0);
            for (int i = 0; i < 4096; i += 2) {
                const std::uint8_t lo =
                    Chunk::getNibble(chunk.blockLightNib, base + i);
                const std::uint8_t hi =
                    Chunk::getNibble(chunk.blockLightNib, base + i + 1);
                arr[i >> 1] = static_cast<std::uint8_t>(lo | (hi << 4));
                if (lo || hi) any = true;
            }
            if (any) {
                blockMask[s / 64] |= 1LL << (s % 64);
                blockArrays.push_back(std::move(arr));
            } else {
                emptyBlockMask[s / 64] |= 1LL << (s % 64);
            }
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
                                     std::int32_t cz, const Chunk& chunk) {
    out.varint(cx);
    out.varint(cz);
    serializeLightPayload(out, chunk);
}

// Serializes LevelChunkWithLight body (packet id excluded).
// biomeRegistryIndex: this world's biome id inside the synced registry order.
// Full LevelChunkWithLight body given an already-locked chunk reference.
inline void serializeLevelChunkBody(WriteBuffer& out, std::int32_t cx, std::int32_t cz,
                                    const Chunk& chunk, std::uint32_t biomeRegistryIndex) {
    out.i32(cx);
    out.i32(cz);

    packHeightmapsNbt(out, &chunk);

    WriteBuffer blob;
    serializeSectionData(blob, &chunk, biomeRegistryIndex);
    out.varint(static_cast<std::int32_t>(blob.data.size()));
    out.raw(blob.data.data(), blob.data.size());

    out.varint(0);   // block entities

    serializeLightPayload(out, chunk);
}

inline void serializeLevelChunk(WriteBuffer& out, std::int32_t cx, std::int32_t cz,
                                const World& world, std::uint32_t biomeRegistryIndex,
                                const std::string& biomeKeyForDebug = {}) {
    (void)biomeKeyForDebug;
    world.generateChunkIfMissing(cx, cz);
    world.withChunk(cx, cz, [&](const Chunk& chunk) {
        serializeLevelChunkBody(out, cx, cz, chunk, biomeRegistryIndex);
    });
}

} // namespace cppfm
