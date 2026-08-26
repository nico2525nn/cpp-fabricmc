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
#include <climits>
#include <unordered_map>

namespace cppfm {

inline int ceilLog2(std::uint32_t v) { // smallest b with (1<<b) >= v ; v>=1
    int b = 0;
    while ((1u << b) < v) ++b;
    return b;
}

struct PaletteSpec {
    int minBits;                    // blocks:4, biomes:1
    bool allowDirect;               // blocks:true, biomes:false
    std::uint32_t globalMaxId;      // for direct mode
};

// Packs 4096 entries of `bits` width into longs (no straddling) after a varint count.
template <typename Fn>
inline void writePackedEntries(WriteBuffer& out, int bits, Fn&& valueAt) {
    const int per = 64 / bits;
    const int nLongs = (4096 + per - 1) / per;
    out.varint(nLongs);
    std::uint64_t cur = 0;
    int filled = 0;
    for (int i = 0; i < 4096; ++i) {
        cur |= static_cast<std::uint64_t>(valueAt(i) & ((1ULL << bits) - 1)) << (filled * bits);
        if (++filled == per) { out.u64(cur); cur = 0; filled = 0; }
    }
    if (filled) out.u64(cur);
}

// Getter maps linear index 0..4095 -> state id (blocks: global id; biomes: registry index).
template <typename Getter>
inline void writePalettedContainer(WriteBuffer& out, Getter&& get, const PaletteSpec& spec) {
    std::vector<std::uint32_t> palette;
    palette.reserve(16);
    std::unordered_map<std::uint32_t, std::uint16_t> indexOf;
    for (int i = 0; i < 4096; ++i) {
        const std::uint32_t v = get(i);
        if (!indexOf.count(v)) {
            indexOf.emplace(v, static_cast<std::uint16_t>(palette.size()));
            palette.push_back(v);
        }
    }

    if (palette.size() <= 1) {                       // single valued
        out.u8(0);
        out.varint(palette.empty() ? 0 : static_cast<std::int32_t>(palette[0]));
        out.varint(0);                               // longCount ALWAYS present
        return;
    }

    int bits = std::max(spec.minBits, ceilLog2(static_cast<std::uint32_t>(palette.size())));
    if (spec.allowDirect && bits > 8) {              // direct/global ids
        const int gbits = ceilLog2(spec.globalMaxId + 1);
        out.u8(static_cast<std::uint8_t>(gbits));
        writePackedEntries(out, gbits, get);
        return;
    }
    out.u8(static_cast<std::uint8_t>(bits));         // indirect
    out.varint(static_cast<std::int32_t>(palette.size()));
    for (auto id : palette) out.varint(static_cast<std::int32_t>(id));
    writePackedEntries(out, bits, [&](int i) { return indexOf.at(get(i)); });
}

// Highest non-air y within chunk (chunk-relative 0..383); 0 if empty column.
inline int columnSurface(const Chunk& c, int lx, int lz) {
    for (int wy = kSectionsPerChunk * 16 - 1; wy >= 0; --wy)
        if (c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] != 0)
            return wy + 1;
    return 0;
}

inline void packHeightmap(std::vector<std::int64_t>& out, const Chunk& c) {
    constexpr int kBpe = 9;                    // ceil(log2(384+1))
    constexpr int kPer = 64 / kBpe;            // 7 entries per long
    out.assign((256 + kPer - 1) / kPer, 0);    // 37 longs
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            const int idx = z * 16 + x;
            out[idx / kPer] |= static_cast<std::int64_t>(columnSurface(c, x, z)) << (idx % kPer * kBpe);
        }
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
            PaletteSpec{4, true, gen::kMaxBlockStateId});

        const std::size_t bBase = static_cast<std::size_t>(s) * 64;
        bool uniform = true;
        const std::uint16_t first = chunk->biomes[bBase];
        for (std::size_t i = 1; i < 64; ++i)
            if (chunk->biomes[bBase + i] != first) { uniform = false; break; }
        if (uniform) {
            writePalettedContainer(blob,
                [&](int) -> std::uint32_t { return first; },
                PaletteSpec{1, false, 0});
        } else {
            writePalettedContainer(blob,
                [&](int i) -> std::uint32_t {
                    return chunk->biomes[bBase + static_cast<std::size_t>(i)];
                },
                PaletteSpec{1, false, 0});
        }
    }
}

inline void packHeightmapsNbt(WriteBuffer& out, const Chunk* chunk) {
    std::vector<std::int64_t> hm;
    packHeightmap(hm, *chunk);
    nbt::Writer w(out);
    w.rootCompound();
    w.namedLongArray("MOTION_BLOCKING", hm);
    w.namedLongArray("WORLD_SURFACE", hm);   // flat world: identical surfaces
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
