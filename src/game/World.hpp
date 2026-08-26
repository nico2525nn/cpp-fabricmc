// World: superflat world storage, generation, block get/set.
// Chunk model: 24 sections of 16x16x16 block state ids (flat arrays; simple & fast).
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <functional>
#include <unordered_map>
#include "generated/BlockStates.hpp"
#include "TerrainGen.hpp"
#include "../worldgen/MultiNoise.hpp"
#include "../worldgen/Structures.hpp"

namespace cppfm {

constexpr int kSectionsPerChunk = 24;      // 384 / 16
constexpr int kMinY = -64;
constexpr int kMaxY = kMinY + 384;         // 320
constexpr int kSeaLevelFlat = -63;         // observed on reference flat worlds

inline constexpr std::int64_t chunkKey(std::int32_t cx, std::int32_t cz) {
    return (static_cast<std::int64_t>(static_cast<std::uint32_t>(cx)) << 32)
         | static_cast<std::uint32_t>(cz);
}

struct Chunk {
    // layout: [section][yInSection][z][x]
    std::array<std::uint16_t, kSectionsPerChunk * 4096> blocks{};
    // Per-cell biomes: 24 sections × 64 cells (4×4×4), values are the
    // synced biome-registry indices used directly on the wire.
    std::array<std::uint16_t, kSectionsPerChunk * 64> biomes{};
    // Block light, 4 bits per block (same index layout as `blocks`).
    std::array<std::uint8_t, (kSectionsPerChunk * 4096 + 1) / 2> blockLightNib{};
    // Cached sky light (built lazily by the LightEngine), 4 bits per block.
    std::shared_ptr<std::array<std::uint8_t,
                               (kSectionsPerChunk * 4096 + 1) / 2>> skyLight;
    std::uint64_t revision = 0;

    static constexpr std::size_t index(int section, int yIn, int z, int x) {
        return (static_cast<std::size_t>(section) * 4096)
             + (static_cast<std::size_t>(yIn) * 256)
             + (static_cast<std::size_t>(z) * 16)
             + static_cast<std::size_t>(x);
    }
    static constexpr std::size_t biomeIndex(int section, int cellY,
                                            int cellZ, int cellX) {
        return (static_cast<std::size_t>(section) * 64)
             + (static_cast<std::size_t>(cellY) * 16)
             + (static_cast<std::size_t>(cellZ) * 4)
             + static_cast<std::size_t>(cellX);
    }
    static std::uint8_t getNibble(const std::array<std::uint8_t,
                                  (kSectionsPerChunk * 4096 + 1) / 2>& a,
                                  std::size_t i) {
        return (i & 1) ? static_cast<std::uint8_t>(a[i >> 1] >> 4)
                       : static_cast<std::uint8_t>(a[i >> 1] & 0x0F);
    }
    static void setNibble(std::array<std::uint8_t,
                          (kSectionsPerChunk * 4096 + 1) / 2>& a,
                          std::size_t i, std::uint8_t v) {
        const std::uint8_t hi = a[i >> 1] & 0xF0;
        const std::uint8_t lo = a[i >> 1] & 0x0F;
        a[i >> 1] = (i & 1) ? static_cast<std::uint8_t>(lo | (v << 4))
                            : static_cast<std::uint8_t>(hi | (v & 0x0F));
    }
};

enum class LevelType { Flat, Normal, Nether, End };

class World {
public:
    World(std::string biomeKey, LevelType level, std::uint64_t seed)
        : biome_(std::move(biomeKey)), level_(level), terrain_(seed), srv_seed(seed) {
        initWorldgen();
    }

    LevelType levelType() const { return level_; }
    struct SpawnPoint { std::int32_t x=0, y=-60, z=0; };
    SpawnPoint spawnPt = {};
    SpawnPoint spawnPoint() const { return spawnPt; }
    void setSpawnPoint(const SpawnPoint& sp) { spawnPt = sp; }
    int seaLevel() const { return level_ == LevelType::Normal ? 63 : kSeaLevelFlat; }
    bool isFlat() const { return level_ == LevelType::Flat; }

    // Feet Y for a fresh spawn at this column (after ensuring the chunk exists).
    int surfaceFeetY(std::int32_t wx, std::int32_t wz) {
        generateChunkIfMissing(wx >> 4, wz >> 4);
        int col = 4;
        withChunk(wx >> 4, wz >> 4, [&](const Chunk& c) {
            for (int ry = kSectionsPerChunk * 16 - 1; ry >= 0; --ry)
                if (c.blocks[Chunk::index(ry >> 4, ry & 15, wz & 15, wx & 15)] != 0) { col = ry + 1; break; }
        });
        return kMinY + col;
    }

    // NOTE: logically const (lazy generation); mutex is mutable
    // Loader hook: return true if it filled the chunk (e.g., from disk).
    void setLoader(std::function<bool(std::int32_t, std::int32_t, Chunk&)> l) { loader_ = std::move(l); }
    void setOnEdit(std::function<void(std::int32_t, std::int32_t)> cb) { onEdit_ = std::move(cb); }
    // Per-block change hook (x,y,z,old,new) used by the light/fluid engines.
    void setOnBlockChanged(std::function<void(std::int32_t, std::int32_t,
                                              std::int32_t, std::uint16_t,
                                              std::uint16_t)> cb) {
        onBlockChanged_ = std::move(cb);
    }

    // ---- block-light accessors (nibble storage inside Chunk) --------------
    std::uint8_t getBlockLight(std::int32_t x, std::int32_t y,
                               std::int32_t z) const {
        if (y < kMinY || y >= kMaxY) return 0;
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end()) return 0;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        const std::size_t i = Chunk::index(wy >> 4, wy & 15, lz, lx);
        return Chunk::getNibble(it->second->blockLightNib, i);
    }
    void setBlockLightRaw(std::int32_t x, std::int32_t y, std::int32_t z,
                          std::uint8_t v) {
        if (y < kMinY || y >= kMaxY) return;
        generateChunkIfMissing(x >> 4, z >> 4);
        std::unique_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end()) return;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        const std::size_t i = Chunk::index(wy >> 4, wy & 15, lz, lx);
        Chunk::setNibble(it->second->blockLightNib, i, v & 0x0F);
    }
    // Sky light cache accessors (nullptr when not yet computed).
    std::uint8_t getSkyLight(std::int32_t x, std::int32_t y,
                             std::int32_t z) const {
        if (y < kMinY || y >= kMaxY) return 0;
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end() || !it->second->skyLight) return 0;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        const std::size_t i = Chunk::index(wy >> 4, wy & 15, lz, lx);
        return Chunk::getNibble(*it->second->skyLight, i);
    }
    bool hasSkyLightCache(std::int32_t cx, std::int32_t cz) const {
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        return it != chunks_.end() && static_cast<bool>(it->second->skyLight);
    }
    void setSkyLightRaw(std::int32_t x, std::int32_t y, std::int32_t z,
                        std::uint8_t v) {
        if (y < kMinY || y >= kMaxY) return;
        std::unique_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end()) return;
        if (!it->second->skyLight)
            it->second->skyLight =
                std::make_shared<std::array<std::uint8_t,
                                            (kSectionsPerChunk * 4096 + 1) / 2>>();
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        const std::size_t i = Chunk::index(wy >> 4, wy & 15, lz, lx);
        Chunk::setNibble(*it->second->skyLight, i, v & 0x0F);
    }
    void ensureSkyStorage(std::int32_t cx, std::int32_t cz) {
        std::unique_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it == chunks_.end()) return;
        if (!it->second->skyLight)
            it->second->skyLight =
                std::make_shared<std::array<std::uint8_t,
                                            (kSectionsPerChunk * 4096 + 1) / 2>>();
    }

    // Block update: check if block above needs to fall (sand/gravel)
    void scheduleNeighborUpdates(std::int32_t x, std::int32_t y, std::int32_t z) {
        // Check block above for gravity
        const auto above = getBlock(x, y + 1, z);
        const auto& names = gen::blockNameToState();
        static const uint16_t sand  = (uint16_t)names.at("minecraft:sand");
        static const uint16_t gravel= (uint16_t)names.at("minecraft:gravel");
        if (above == sand || above == gravel) {
            // make it fall: remove from old pos, find ground, place
            setBlockInternal(x, y+1, z, 0);
            int fallY = y;
            while (fallY > kMinY && getBlock(x, fallY-1, z) == 0) --fallY;
            setBlockInternal(x, fallY, z, above);
            if (onEdit_) onEdit_(x >> 4, z >> 4);
        }
        // Torch/support blocks pop off if support removed
        static const uint16_t torch = (uint16_t)names.at("minecraft:torch");
        if (above == torch) {
            setBlockInternal(x, y+1, z, 0);
            if (onEdit_) onEdit_(x >> 4, z >> 4);
        }
    }

private:
    void setBlockInternal(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
        if (y < kMinY || y >= kMaxY) return;
        generateChunkIfMissing(x >> 4, z >> 4);
        std::unique_lock lock(mutex_);
        auto& c = *chunks_.at(chunkKey(x >> 4, z >> 4));
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] = state;
        ++c.revision;
    }

public:

    // Double-checked, atomic lazy generation (safe under concurrent joins).
    void generateChunkIfMissing(std::int32_t cx, std::int32_t cz) const {
        {
            std::shared_lock lock(mutex_);
            if (chunks_.count(chunkKey(cx, cz))) return;
        }
        auto c = std::make_unique<Chunk>();
        const bool loaded = loader_ && loader_(cx, cz, *c);
        if (!loaded) {
            if (level_ == LevelType::Nether || level_ == LevelType::End) fillTerrainV3(*c, cx, cz);
            else if (level_ == LevelType::Normal) fillTerrainV3(*c, cx, cz);
            else if (false) fillTerrain(*c, cx, cz);
            else fillFlat(*c);
        }
        std::unique_lock lock(mutex_);
        chunks_.try_emplace(chunkKey(cx, cz), std::move(c));   // never replaces
    }
    // Runs fn(chunk) while holding the world read lock. Use for any access that
    // must not race with chunk replacement or edits.
    template <typename Fn>
    bool withChunk(std::int32_t cx, std::int32_t cz, Fn&& fn) const {
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it == chunks_.end()) return false;
        fn(*it->second);
        return true;
    }
    bool hasChunk(std::int32_t cx, std::int32_t cz) const {
        std::shared_lock lock(mutex_);
        return chunks_.count(chunkKey(cx, cz)) != 0;
    }

    std::uint16_t getBlock(std::int32_t x, std::int32_t y, std::int32_t z) const {
        if (y < kMinY || y >= kMaxY) return 0;
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end()) return 0;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        return it->second->blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)];
    }
    void setBlock(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
        if (y < kMinY || y >= kMaxY) return;
        generateChunkIfMissing(x >> 4, z >> 4);
        const std::uint16_t old =
            getBlock(x, y, z);                       // re-acquires shared lock
        {
            std::unique_lock lock(mutex_);
            auto& c = *chunks_.at(chunkKey(x >> 4, z >> 4));
            const int lx = x & 15, lz = z & 15, wy = y - kMinY;
            c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] = state;
            ++c.revision;
        }
        if (onBlockChanged_) onBlockChanged_(x, y, z, old, state);
        if (onEdit_) onEdit_(x >> 4, z >> 4);
    }
    const Chunk* tryGet(std::int32_t cx, std::int32_t cz) const {
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        return it == chunks_.end() ? nullptr : it->second.get();
    }
    // Sum of revisions of the chunk (cheap "did anything change" probe).
    std::uint64_t revisionAt(std::int32_t cx, std::int32_t cz) const {
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        return it == chunks_.end() ? 0 : it->second->revision;
    }

    const std::string& biomeKey() const { return biome_; }

    // Biome codec wiring: resolve biome key <-> synced registry index.
    // Must be installed before any chunk generation (GameServer::init).
    void setDimensionId(std::int8_t d) { dimensionId_ = d; }
    std::int8_t dimensionId() const { return dimensionId_; }
    void setBiomeCodec(std::function<std::int32_t(const std::string&)> toIndex,
                       std::int32_t defaultIndex) {
        biomeToIndex_ = std::move(toIndex);
        defaultBiomeIndex_ = defaultIndex;
    }
    std::int32_t biomeIndexOf(const std::string& key) const {
        return biomeToIndex_ ? biomeToIndex_(key) : 0;
    }

private:
    void fillTerrainV3(Chunk& c, std::int32_t cx, std::int32_t cz) const;
    void fillNether(Chunk& c, std::int32_t cx, std::int32_t cz) const;
    void fillEnd(Chunk& c, std::int32_t cx, std::int32_t cz) const;

public:
    void fillTerrain(Chunk& c, std::int32_t cx, std::int32_t cz) const {
        const auto& table = gen::blockNameToState();
        const std::uint16_t stone   = (uint16_t)table.at("minecraft:stone");
        const std::uint16_t dirt    = (uint16_t)table.at("minecraft:dirt");
        const std::uint16_t grass   = (uint16_t)table.at("minecraft:grass_block");
        const std::uint16_t sand    = (uint16_t)table.at("minecraft:sand");
        const std::uint16_t water   = (uint16_t)table.at("minecraft:water");
        const std::uint16_t bedrock = (uint16_t)table.at("minecraft:bedrock");
        const std::uint16_t coalOre = (uint16_t)table.at("minecraft:coal_ore");
        const std::uint16_t ironOre = (uint16_t)table.at("minecraft:iron_ore");
        const std::uint16_t log     = (uint16_t)table.at("minecraft:oak_log");
        const std::uint16_t leaves  = (uint16_t)table.at("minecraft:oak_leaves");
        constexpr int kSea = 63;

        auto setIfIn = [&](std::int32_t wx, int wy, std::int32_t wz,
                           std::uint16_t st, bool overwriteSolid=false) {
            const std::int32_t ccx = wx >> 4, ccz = wz >> 4;
            if (ccx != cx || ccz != cz) return;
            if (wy < kMinY || wy >= kMaxY) return;
            const int wyR = wy - kMinY;
            auto& slot = c.blocks[Chunk::index(wyR >> 4, wyR & 15, wz & 15, wx & 15)];
            if (!overwriteSolid && slot != 0 && slot != water) return;
            if (overwriteSolid && slot == 0) return;
            slot = st;
        };

        for (int lz = 0; lz < 16; ++lz)
        for (int lx = 0; lx < 16; ++lx) {
            const std::int32_t wx = cx * 16 + lx, wz = cz * 16 + lz;
            const auto col = terrain_.column(wx, wz);
            const int surf = col.surfaceY;                       // first air (world y)
            const bool beach = col.ocean || surf <= kSea + 2;
            for (int y = kMinY; y <= kMaxY && y < surf; ++y) {
                std::uint16_t st;
                if (y == kMinY) st = bedrock;
                else if (y >= surf - 1) st = beach ? sand : grass;
                else if (y >= surf - 4) st = beach ? sand : dirt;
                else st = stone;
                if (!col.ocean && y >= -58 && y < surf - 8) {    // spaghetti caves
                    const double n1 = terrain_.caveA_.sample(wx*0.02, y*0.03, wz*0.02);
                    const double n2 = terrain_.caveB_.sample(wx*0.023, y*0.033, wz*0.023);
                    if (n1*n1 + n2*n2 < 0.0025) st = 0;          // carve air
                }
                if (st == stone) {                               // ores
                    const double o = terrain_.oreA_.sample(wx*0.09, y*0.09, wz*0.09);
                    if (o > 0.78 && y < 128) st = coalOre;
                    else if (o < -0.80 && y < 62) st = ironOre;
                }
                const int wy = y - kMinY;
                c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] = st;
            }
            for (int y = surf; y < kSea; ++y) {                  // oceans/lakes
                const int wy = y - kMinY;
                c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] = water;
            }
        }

        // trees: up to a few per chunk, fully inside chunk margin
        int planted = 0;
        for (int lz = 3; lz < 13 && planted < 3; ++lz)
        for (int lx = 3; lx < 13 && planted < 3; ++lx) {
            const std::int32_t wx = cx * 16 + lx, wz = cz * 16 + lz;
            if (!terrain_.treeCandidate(srv_seed, wx, wz)) continue;
            const auto col = terrain_.column(wx, wz);
            if (col.ocean || col.surfaceY <= kSea + 1) continue;
            const int gyR = col.surfaceY - 1 - kMinY;
            if (c.blocks[Chunk::index(gyR >> 4, gyR & 15, wz & 15, wx & 15)] != grass) continue;
            const int trunkH = 4 + static_cast<int>(terrain_.posHash(srv_seed,wx,555,wz)*3);
            for (int t = 0; t < trunkH; ++t) setIfIn(wx, col.surfaceY + t, wz, log, true);
            for (int dy = trunkH - 2; dy <= trunkH + 1; ++dy) {
                const int rad = (dy >= trunkH) ? 1 : 2;
                for (int dzl = -rad; dzl <= rad; ++dzl)
                for (int dxl = -rad; dxl <= rad; ++dxl) {
                    if (dxl==0 && dzl==0 && dy<trunkH) continue;
                    setIfIn(wx+dxl, col.surfaceY+dy, wz+dzl, leaves, false);
                }
            }
            ++planted;
        }
    }

    void fillFlat(Chunk& c) const {
        const auto& map = gen::blockNameToState();
        const std::uint16_t bedrock = static_cast<std::uint16_t>(map.at("minecraft:bedrock"));
        const std::uint16_t dirt    = static_cast<std::uint16_t>(map.at("minecraft:dirt"));
        const std::uint16_t grass   = static_cast<std::uint16_t>(map.at("minecraft:grass_block"));
        for (int y = kMinY; y <= -61; ++y) {
            const std::uint16_t st = (y == -64) ? bedrock : (y == -61 ? grass : dirt);
            const int sec = ((y - kMinY) >> 4), yi = (y - kMinY) & 15;
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x)
                    c.blocks[Chunk::index(sec, yi, z, x)] = st;
        }
    }

    mutable std::shared_mutex mutex_;
    mutable std::unordered_map<std::int64_t, std::unique_ptr<Chunk>> chunks_;
    std::string biome_;
    LevelType level_;
    TerrainGenerator terrain_;
    std::uint64_t srv_seed;
    std::function<std::int32_t(const std::string&)> biomeToIndex_;
    std::int32_t defaultBiomeIndex_ = 40;
    std::int8_t dimensionId_ = 0;
    std::unique_ptr<worldgen::MultiNoiseBiomeSource> biomeSource_;
    std::unique_ptr<worldgen::StructureGenerator> structures_;
    std::vector<std::pair<const char*, float>> oreTableV3_;   // name, rarity
    void initWorldgen();
    std::function<bool(std::int32_t, std::int32_t, Chunk&)> loader_;
    std::function<void(std::int32_t, std::int32_t)> onEdit_;
    std::function<void(std::int32_t, std::int32_t, std::int32_t,
                       std::uint16_t, std::uint16_t)> onBlockChanged_;
};

} // namespace cppfm
