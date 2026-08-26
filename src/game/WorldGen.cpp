// Worldgen v3: density-function shaped terrain, MultiNoise per-cell biomes,
// advanced triangle-distribution ores and structures (plan3.md ワールド生成).
#include "World.hpp"
#include "../worldgen/DensityFunction.hpp"
#include <algorithm>

namespace cppfm {

namespace {

// Per-ore placement rules (clean-room approximations of vanilla 1.21 ore
// distribution: triangle distributions over Y with per-chunk vein counts).
struct OreRule {
    const char* name;
    int minY, maxY, peakY;      // triangle support & apex
    float veinsPerChunk;        // average
    int veinSize;               // blocks per vein (approx)
};

const std::vector<OreRule>& oreRules() {
    static const std::vector<OreRule> rules = {
        {"minecraft:coal_ore",        0,  192,   96, 8.f, 12},
        {"minecraft:iron_ore",       -24, 256,   72, 6.f,  8},
        {"minecraft:copper_ore",     -16, 112,   48, 4.f, 10},
        {"minecraft:gold_ore",       -64,  32,  -16, 2.f,  8},
        {"minecraft:redstone_ore",   -64,  15,  -59, 4.f,  8},
        {"minecraft:diamond_ore",   -144,  16,  -59, 2.f,  6},
        {"minecraft:lapis_ore",      -32,  32,    0, 1.5f, 6},
        {"minecraft:emerald_ore",    -16, 320,   96, 0.3f, 1},
    };
    return rules;
}

double triWeight(int y, int lo, int hi, int peak) {
    if (y < lo || y > hi) return 0;
    // piecewise-linear triangle normalised to [0,1]
    if (y <= peak) {
        const double span = std::max(1, peak - lo);
        return (y - lo) / span;
    }
    const double span = std::max(1, hi - peak);
    return (hi - y) / span;
}

std::uint64_t rng64(std::uint64_t& s) {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return s;
}

} // namespace

void World::initWorldgen() {
    biomeSource_ = std::make_unique<worldgen::MultiNoiseBiomeSource>(srv_seed);
    structures_ = std::make_unique<worldgen::StructureGenerator>(srv_seed);
}

void World::fillTerrainV3(Chunk& c, std::int32_t cx, std::int32_t cz) const {
    const auto& table = gen::blockNameToState();
    auto id = [&](const char* n) -> std::uint16_t {
        auto it = table.find(n);
        return it != table.end() ? static_cast<std::uint16_t>(it->second) : 0;
    };
    const std::uint16_t STONE = id("minecraft:stone");
    const std::uint16_t DIRT = id("minecraft:dirt");
    const std::uint16_t GRASS = id("minecraft:grass_block");
    const std::uint16_t SAND = id("minecraft:sand");
    const std::uint16_t WATER = id("minecraft:water");
    const std::uint16_t BEDROCK = id("minecraft:bedrock");
    const std::uint16_t GRAVEL = id("minecraft:gravel");
    constexpr int kSea = TerrainGenerator::kSeaLevelNormal;

    // ------------------------------------------------------ biome assignment
    c.biomes.fill(static_cast<std::uint16_t>(defaultBiomeIndex_));
    for (int s = 0; s < kSectionsPerChunk; ++s)
        
    for (int sec = 0; sec < kSectionsPerChunk; ++sec)
        for (int cy = 0; cy < 4; ++cy)
            for (int cz2 = 0; cz2 < 4; ++cz2)
                for (int cx2 = 0; cx2 < 4; ++cx2) {
                    const std::int32_t wx = cx * 16 + cx2 * 4 + 2;
                    const std::int32_t wz = cz * 16 + cz2 * 4 + 2;
                    const int wy = kMinY + sec * 16 + cy * 4 + 2;
                    const std::string& key =
                        biomeSource_->sample(wx, wy, wz);
                    const std::int32_t idx = biomeIndexOf(key);
                    c.biomes[Chunk::biomeIndex(sec, cy, cz2, cx2)] =
                        static_cast<std::uint16_t>(idx < 0 ? defaultBiomeIndex_
                                                           : idx);
                }

    // ------------------------------------------------------- surface shaping
    struct ColInfo { int surf; bool ocean; std::string biome; };
    ColInfo cols[18][18];                                  // margin for trees
    for (int lz = -1; lz < 17; ++lz)
        for (int lx = -1; lx < 17; ++lx) {
            const std::int32_t wx = cx * 16 + lx, wz = cz * 16 + lz;
            const double h = biomeSource_->heightEstimate(wx, wz);
            const int surface = static_cast<int>(std::clamp(h, -56.0, 150.0)) + 1;
            const bool ocean = surface < kSea - 2;
            const std::string& bio = biomeSource_->sample(wx + 2, 63, wz + 2);
            cols[lz + 1][lx + 1] = {surface, ocean, bio};
        }

    auto setIfIn = [&](std::int32_t wx, int wy, std::int32_t wz,
                       std::uint16_t st, bool overwriteSolid = false) {
        const std::int32_t ccx = wx >> 4, ccz = wz >> 4;
        if (ccx != cx || ccz != cz) return;
        if (wy < kMinY || wy >= kMaxY) return;
        const int wyR = wy - kMinY;
        auto& slot = c.blocks[Chunk::index(wyR >> 4, wyR & 15, wz & 15, wx & 15)];
        if (!overwriteSolid && slot != 0 && slot != WATER) return;
        if (overwriteSolid && slot == 0) return;
        slot = st;
    };

    const ImprovedNoise* caveA = nullptr;
    thread_local ImprovedNoise caveANoise(srv_seed ^ 0xA24BAED4963EE407ULL);
    caveA = &caveANoise;
    thread_local ImprovedNoise caveBNoise(srv_seed ^ 0x9FB21C651E98DF25ULL);

    for (int lz = 0; lz < 16; ++lz)
        for (int lx = 0; lx < 16; ++lx) {
            const std::int32_t wx = cx * 16 + lx, wz = cz * 16 + lz;
            const auto& col = cols[lz + 1][lx + 1];
            const int surf = col.surf;
            const bool desertish = col.biome.find("desert") != std::string::npos ||
                                   col.biome.find("beach") != std::string::npos ||
                                   col.biome.find("badlands") != std::string::npos;
            const bool snowy = col.biome.find("snowy") != std::string::npos ||
                               col.biome.find("frozen") != std::string::npos;
            const bool beach = col.ocean || surf <= kSea + 2;
            for (int y = kMinY; y < surf; ++y) {
                std::uint16_t st;
                if (y == kMinY) st = BEDROCK;
                else if (y == kMinY + 1 + (TerrainGenerator::posHash(srv_seed, wx, 9,
                                                    wz) < 0.5 ? 0 : 1))
                    st = BEDROCK;                            // ragged floor
                else if (y >= surf - 1)
                    st = beach || desertish ? SAND
                         : snowy ? id("minecraft:snow_block")
                                 : GRASS;
                else if (y >= surf - 4)
                    st = beach || desertish ? SAND
                         : snowy ? DIRT : DIRT;
                else st = STONE;
                if (st == STONE || st == GRASS || st == DIRT) {
                    // spaghetti caves carve through stone only below surface-7
                    if (st == STONE && !col.ocean && y >= -58 && y < surf - 7) {
                        const double n1 = caveANoise.sample(wx * 0.02, y * 0.03,
                                                            wz * 0.02);
                        const double n2 = caveBNoise.sample(wx * 0.023, y * 0.033,
                                                            wz * 0.023);
                        if (n1 * n1 + n2 * n2 < 0.0025) st = 0;
                    }
                }
                const int wy = y - kMinY;
                c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] = st;
            }
            for (int y = surf; y < kSea; ++y) {              // oceans/rivers
                const int wy = y - kMinY;
                c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] = WATER;
            }
        }

    // ------------------------------------------------------------- ores v3
    std::uint64_t oreRng = srv_seed
        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) * 0x2545F4914F6CDD1DULL)
        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cz)) << 32);
    const auto isStoneLike = [&](std::uint16_t s) {
        return s == STONE;
    };
    for (const auto& rule : oreRules()) {
        const std::uint16_t oreState = id(rule.name);
        if (!oreState) continue;
        const int deepslateStartY = 0;                   // visual variant below
        const std::uint16_t deepVariant = [&]() -> std::uint16_t {
            std::string dn = rule.name;
            dn.insert(dn.rfind("_ore"), "_deepslate");
            auto it = table.find(dn.c_str());
            return it != table.end()
                       ? static_cast<std::uint16_t>(it->second) : oreState;
        }();
        const int attempts = static_cast<int>(rule.veinsPerChunk)
                           + ((rng64(oreRng) % 100) / 100.f <
                              (rule.veinsPerChunk -
                               static_cast<int>(rule.veinsPerChunk)) ? 1 : 0);
        for (int a = 0; a < attempts; ++a) {
            const int baseX = static_cast<int>(rng64(oreRng) % 16);
            const int baseZ = static_cast<int>(rng64(oreRng) % 16);
            // pick Y by sampling the triangle CDF (rejection-free): choose
            // proportional to weight at integer steps.
            int py = rule.peakY;
            {
                double totalW = 0;
                for (int y = rule.minY; y <= rule.maxY; y += 4)
                    totalW += triWeight(y, rule.minY, rule.maxY, rule.peakY);
                double r = (rng64(oreRng) >> 11) / double(1ULL << 53) * totalW;
                for (int y = rule.minY; y <= rule.maxY; y += 4) {
                    r -= triWeight(y, rule.minY, rule.maxY, rule.peakY);
                    if (r <= 0) { py = y; break; }
                }
            }
            // small ellipsoid blob around (baseX,py,baseZ)
            const int rx = 1 + static_cast<int>(rng64(oreRng) % 3);
            const int ry = 1 + static_cast<int>(rng64(oreRng) % 2);
            for (int dy = -ry; dy <= ry; ++dy)
                for (int dz = -rx; dz <= rx; ++dz)
                    for (int dx = -rx; dx <= rx; ++dx) {
                        if (dx * dx / double(rx * rx + 1) +
                            dz * dz / double(rx * rx + 1) +
                            dy * dy / double(ry * ry + 1) > 1.0) continue;
                        const int wy = py + dy - kMinY;
                        if (wy < 0 || wy >= kSectionsPerChunk * 16) continue;
                        // allow blobs to cross chunk borders by clamping here
                        const int bx = baseX + dx, bz2 = baseZ + dz;
                        if (bx < 0 || bx > 15 || bz2 < 0 || bz2 > 15) continue;
                        auto& slot =
                            c.blocks[Chunk::index(wy >> 4, wy & 15, bz2, bx)];
                        if (!isStoneLike(slot)) continue;
                        slot = (kMinY + wy) < deepslateStartY + 8 ? deepVariant
                                                                  : oreState;
                    }
        }
    }

    // --------------------------------------------------------------- trees
    {
        int planted = 0;
        for (int lz = 2; lz < 14 && planted < 3; ++lz)
            for (int lx = 2; lx < 14 && planted < 3; ++lx) {
                const std::int32_t wx = cx * 16 + lx, wz = cz * 16 + lz;
                const auto& col = cols[lz + 1][lx + 1];
                const bool forest = col.biome.find("forest") != std::string::npos
                                 || col.biome.find("taiga") != std::string::npos
                                 || col.biome == "minecraft:jungle";
                const double chance = forest ? 0.03 : 0.006;
                if (TerrainGenerator::posHash(srv_seed ^ 0x7EE5, wx, 777, wz)
                    >= chance) continue;
                if (col.ocean || col.surf <= kSea + 1) continue;
                const int gyR = col.surf - 1 - kMinY;
                if (c.blocks[Chunk::index(gyR >> 4, gyR & 15, wz & 15, wx & 15)]
                    != GRASS) continue;
                const int trunkH = 4 + static_cast<int>(
                    TerrainGenerator::posHash(srv_seed, wx, 555, wz) * 3);
                for (int t = 0; t < trunkH; ++t)
                    setIfIn(wx, col.surf + t, wz, id("minecraft:oak_log"), true);
                for (int dy = trunkH - 2; dy <= trunkH + 1; ++dy) {
                    const int rad = dy >= trunkH ? 1 : 2;
                    for (int dzl = -rad; dzl <= rad; ++dzl)
                        for (int dxl = -rad; dxl <= rad; ++dxl) {
                            if (dxl == 0 && dzl == 0 && dy < trunkH) continue;
                            setIfIn(wx + dxl, col.surf + dy, wz + dzl,
                                    id("minecraft:oak_leaves"));
                        }
                }
                ++planted;
            }
    }

    // ---------------------------------------------------------- structures
    structures_->generateChunk(c, cx, cz, [&](std::int32_t wx, std::int32_t wz)
                                   -> std::int32_t {
        // ground from the same height field used above
        const double h = biomeSource_->heightEstimate(wx, wz);
        return static_cast<std::int32_t>(std::clamp(h, -56.0, 150.0)) ;
    });
}

} // namespace cppfm
