// Worldgen v3: density-function shaped terrain, MultiNoise per-cell biomes,
// advanced triangle-distribution ores and structures (plan3.md ワールド生成).
#include "World.hpp"
#include "../worldgen/ChunkGenerator.hpp"
#include "../worldgen/StructureManager.hpp"
#include "../worldgen/DensityFunction.hpp"
#include <algorithm>
#include <shared_mutex>

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

World::~World() = default;

void World::initWorldgen() {
    biomeSource_ = std::make_shared<worldgen::MultiNoiseBiomeSource>(srv_seed);
    structures_ = std::make_unique<worldgen::StructureGenerator>(srv_seed);
    structureManager_ = std::make_unique<worldgen::StructureManager>(srv_seed, biomeSource_);
    // try data-driven load (plan7): JSON sets from assets/data/structure_sets or structures
    structureManager_->loadFromDirectory("assets/data/structure_sets");
    structureManager_->loadFromDirectory("assets/data/structures");
    // ChunkGenerator delegation (plan7): create level-specific generator
    switch (level_) {
        case LevelType::Flat:
            generator_ = std::make_unique<worldgen::FlatLevelSource>(this);
            break;
        case LevelType::Nether:
            generator_ = std::make_unique<worldgen::NetherLevelSource>(this);
            break;
        case LevelType::End:
            generator_ = std::make_unique<worldgen::EndLevelSource>(this);
            break;
        case LevelType::Normal:
        default:
            generator_ = std::make_unique<worldgen::NormalLevelSource>(this);
            break;
    }
}

void World::generateChunkIfMissing(std::int32_t cx, std::int32_t cz) const {
    {
        std::shared_lock lock(mutex_);
        if (chunks_.count(chunkKey(cx, cz))) return;
    }
    auto c = std::make_unique<Chunk>();
    const bool loaded = loader_ && loader_(cx, cz, *c);
    if (!loaded) {
        if (generator_) generator_->fillChunk(*c, cx, cz);
        else generateChunkFallback(*c, cx, cz);
    }
    std::unique_lock lock(mutex_);
    chunks_.try_emplace(chunkKey(cx, cz), std::move(c));
}

void World::fillTerrainV3(Chunk& c, std::int32_t cx, std::int32_t cz) const {
    if (dimensionId_ == -1) { fillNether(c, cx, cz); return; }
    if (dimensionId_ == 1)  { fillEnd(c, cx, cz);   return; }
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

    // ---------------------------------------------------------- structures (plan7: via StructureManager data-driven)
    auto groundFn = [&](std::int32_t wx, std::int32_t wz) -> std::int32_t {
        const double h = biomeSource_->heightEstimate(wx, wz);
        return static_cast<std::int32_t>(std::clamp(h, -56.0, 150.0));
    };
    if (structureManager_) structureManager_->generate(c, cx, cz, groundFn);
    else structures_->generateChunk(c, cx, cz, groundFn);
}


// ------------------------------------------------------- nether / end gen

void World::fillNether(Chunk& c, std::int32_t cx, std::int32_t cz) const {
    // plan6 §1: multiple Noise Generators (surface, depth, float) for biome selection
    thread_local ImprovedNoise density(srv_seed ^ 0x6E657468ULL);
    thread_local ImprovedNoise surfaceNoise(srv_seed ^ 0x53555246ULL); // surface pattern
    thread_local ImprovedNoise depthNoise(srv_seed ^ 0x44455054ULL);   // depth/basal
    thread_local ImprovedNoise floatNoise(srv_seed ^ 0x464C4F41ULL);   // floating islands
    const auto& table = gen::blockNameToState();
    auto id2 = [&](const char* n) -> std::uint16_t {
        auto it = table.find(n);
        return it != table.end() ? static_cast<std::uint16_t>(it->second) : 0;
    };
    const std::uint16_t NETHERRACK = id2("minecraft:netherrack");
    const std::uint16_t BEDROCK = id2("minecraft:bedrock");
    const std::uint16_t LAVA = id2("minecraft:lava");
    const std::uint16_t SOUL = id2("minecraft:soul_sand");
    const std::uint16_t SOUL_SOIL = id2("minecraft:soul_soil") ? id2("minecraft:soul_soil") : SOUL;
    const std::uint16_t BASALT = id2("minecraft:basalt") ? id2("minecraft:basalt") : NETHERRACK;
    const std::uint16_t BLACKSTONE = id2("minecraft:blackstone") ? id2("minecraft:blackstone") : BASALT;
    const std::uint16_t CRIMSON_NYLIUM = id2("minecraft:crimson_nylium") ? id2("minecraft:crimson_nylium") : NETHERRACK;
    const std::uint16_t WARPED_NYLIUM = id2("minecraft:warped_nylium") ? id2("minecraft:warped_nylium") : NETHERRACK;
    const std::uint16_t GLOWSTONE = id2("minecraft:glowstone");
    const std::uint16_t QUARTZ_ORE = id2("minecraft:nether_quartz_ore") ? id2("minecraft:nether_quartz_ore") : NETHERRACK;
    c.biomes.fill(static_cast<std::uint16_t>(defaultBiomeIndex_));
    for (int lz = 0; lz < 16; ++lz)
        for (int lx = 0; lx < 16; ++lx) {
            const std::int32_t wx = cx * 16 + lx, wz = cz * 16 + lz;
            // biome selection via noise thresholds (plan6 §1)
            const double surf = surfaceNoise.octaves(wx*0.008, 0, wz*0.008, 3);
            const double dep = depthNoise.sample(wx*0.015, 0, wz*0.015);
            const double flt = floatNoise.sample(wx*0.02, 0, wz*0.02);
            // enum: 0=nether_wastes, 1=basalt_deltas, 2=warped, 3=crimson, 4=soul_sand_valley
            int biome = 0;
            if (surf > 0.55) biome = 1; // basalt deltas
            else if (surf < -0.55 && dep > 0.3) biome = 2; // warped forest
            else if (surf > 0.35 && flt > 0.4) biome = 3; // crimson forest
            else if (surf < -0.3 && dep < -0.2) biome = 4; // soul sand valley
            for (int y = kMinY; y < kMaxY; ++y) {
                std::uint16_t st = 0;
                if (y == kMinY || y == kMaxY - 1) st = BEDROCK;
                else if (y > kMaxY - 5 &&
                         TerrainGenerator::posHash(srv_seed, wx, y, wz) < .7)
                    st = BEDROCK;
                else {
                    const double d = density.octaves(wx * 0.012, y * 0.02, wz * 0.012, 3);
                    // basalt deltas have more solid at mid heights with basalt pillars
                    double thresh = 0.02;
                    if (biome==1) thresh = -0.05; // more terrain
                    if (d > thresh) {
                        // biome-specific top layer
                        if (biome==1) {
                            // basalt deltas: basalt/blackstone mix, occasional magma
                            if (y > 40 && y < 90) {
                                double b = surfaceNoise.sample(wx*0.04, y*0.03, wz*0.04);
                                if (b > 0.4) st = BASALT;
                                else if (b > 0.1) st = BLACKSTONE;
                                else st = NETHERRACK;
                            } else st = BASALT;
                        } else if (biome==2) {
                            // warped forest: warped nylium on top, netherrack below
                            if (y > 72 && y < 78) st = WARPED_NYLIUM;
                            else st = NETHERRACK;
                            if (density.sample(wx * 0.03, y * 0.05, wz * 0.03) > 0.55 && y < 38) st = SOUL_SOIL;
                        } else if (biome==3) {
                            if (y > 72 && y < 78) st = CRIMSON_NYLIUM;
                            else st = NETHERRACK;
                            if (density.sample(wx * 0.03, y * 0.05, wz * 0.03) > 0.6 && y < 40) st = SOUL;
                        } else if (biome==4) {
                            // soul sand valley: soul soil/sand преобладает
                            if (y < 45 && y > 32) {
                                double s = depthNoise.sample(wx*0.05, y*0.02, wz*0.05);
                                st = (s > 0.2) ? SOUL : SOUL_SOIL;
                            } else st = NETHERRACK;
                        } else {
                            st = NETHERRACK;
                            if (density.sample(wx * 0.03, y * 0.05, wz * 0.03) > 0.55 && y < 40)
                                st = SOUL;
                        }
                        // quartz ore veins (rare)
                        if (st==NETHERRACK || st==BASALT) {
                            double q = floatNoise.sample(wx*0.08, y*0.08, wz*0.08);
                            if (q > 0.82 && y > 10 && y < 110) st = QUARTZ_ORE;
                        }
                    } else if (y <= 31) {
                        st = LAVA;
                    }
                }
                const int wy = y - kMinY;
                c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] = st;
            }
            // ceiling glowstone speckles + basalt pillar caps
            for (int k2 = 0; k2 < 3; ++k2) {
                const int gy = kMaxY - 3 -
                    static_cast<int>(TerrainGenerator::posHash(srv_seed, wx, k2, wz) * 4);
                if (TerrainGenerator::posHash(srv_seed ^ 7, wx, gy, wz) < .12) {
                    const int wy = gy - kMinY;
                    c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] = GLOWSTONE;
                }
            }
            // basalt delta pillars (occasional tall basalt columns)
            if (biome==1 && TerrainGenerator::posHash(srv_seed ^ 0xBADA, wx, 1, wz) < 0.015) {
                int h = 10 + (int)(TerrainGenerator::posHash(srv_seed, wx, 3, wz)*14);
                for (int dy=0; dy<h; ++dy) {
                    int py = 32 + dy;
                    if (py < kMinY || py >= kMaxY) continue;
                    const int wy = py - kMinY;
                    if (c.blocks[Chunk::index(wy>>4, wy&15, lz, lx)]==0) continue;
                    // overwrite with basalt for pillar
                    c.blocks[Chunk::index(wy>>4, wy&15, lz, lx)] = BASALT;
                }
            }
        }
    // light engine seeds glowstone automatically via emission on first tick.
}

void World::fillEnd(Chunk& c, std::int32_t cx, std::int32_t cz) const {
    const auto& table = gen::blockNameToState();
    auto id2 = [&](const char* n) -> std::uint16_t {
        auto it = table.find(n);
        return it != table.end() ? static_cast<std::uint16_t>(it->second) : 0;
    };
    const std::uint16_t END_STONE = id2("minecraft:end_stone");
    const std::uint16_t BEDROCK = id2("minecraft:bedrock");
    const std::uint16_t OBSIDIAN = id2("minecraft:obsidian") ? id2("minecraft:obsidian") : BEDROCK;
    const std::uint16_t CHORUS = id2("minecraft:chorus_plant") ? id2("minecraft:chorus_plant") : END_STONE;
    const std::uint16_t PORTAL = id2("minecraft:end_portal")
                                     ? id2("minecraft:end_portal")
                                     : BEDROCK;
    // multi-noise for outer islands (plan6 §1)
    thread_local ImprovedNoise islandNoise(srv_seed ^ 0x454E4410ULL);
    c.biomes.fill(static_cast<std::uint16_t>(defaultBiomeIndex_));
    // central island ~ radius 60 at y=64 surface
    for (int lz = 0; lz < 16; ++lz)
        for (int lx = 0; lx < 16; ++lx) {
            const std::int32_t wx = cx * 16 + lx, wz = cz * 16 + lz;
            const double r = std::sqrt(double(wx) * wx + double(wz) * wz);
            if (r < 58 + TerrainGenerator::posHash(srv_seed, wx, 1, wz) * 10) {
                const int depth = 20 + static_cast<int>(
                    TerrainGenerator::posHash(srv_seed, wx, 2, wz) * 14);
                for (int y = 64 - depth; y <= 64; ++y) {
                    const int wy = y - kMinY;
                    c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] = END_STONE;
                }
            } else if (r > 200) {
                // outer islands: noise-modulated blobs beyond 200 blocks
                double n = islandNoise.octaves(wx*0.01, 0, wz*0.01, 3);
                double hash = TerrainGenerator::posHash(srv_seed ^ 0xE11D, wx, 7, wz);
                // island threshold: central falloff + noise
                double islandProb = 0.04 + n * 0.02;
                if (hash < islandProb) {
                    int islandH = 64 + (int)(n * 8);
                    int depth = 8 + (int)(hash * 12);
                    // occasionally tall outer island
                    if (n > 0.6) depth += 8;
                    for (int y = islandH - depth; y <= islandH; ++y) {
                        if (y < kMinY || y >= kMaxY) continue;
                        const int wy = y - kMinY;
                        // island shape: circular falloff within chunk pixel
                        // use small radius jitter
                        c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] = END_STONE;
                    }
                    // chorus-like pillars on outer islands (4-7 high)
                    if (hash < 0.008 && islandNoise.sample(wx*0.05, 0, wz*0.05) > 0.3) {
                        int pillarH = 4 + (int)(TerrainGenerator::posHash(srv_seed, wx, 9, wz)*5);
                        for (int dy = 1; dy <= pillarH; ++dy) {
                            int py = islandH + dy;
                            if (py < kMinY || py >= kMaxY) continue;
                            const int wy = py - kMinY;
                            // choose chorus or obsidian for pillar top
                            std::uint16_t mat = (dy==pillarH) ? CHORUS : END_STONE;
                            if (dy > pillarH-2 && hash < 0.003) mat = OBSIDIAN;
                            c.blocks[Chunk::index(wy>>4, wy&15, lz, lx)] = mat;
                        }
                    }
                }
            }
        }
    // exit portal pedestal at origin
    if (cx == 0 && cz == 0) {
        for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz) {
                if (std::abs(dx) == 2 && std::abs(dz) == 2) continue;
                const int wy = 65 - kMinY;
                c.blocks[Chunk::index(wy >> 4, wy & 15, dz & 15, dx & 15)] =
                    BEDROCK;
                if (dx == 0 && dz == 0) {
                    c.blocks[Chunk::index((66 - kMinY) >> 4, (66 - kMinY) & 15,
                                          0, 0)] = BEDROCK;
                }
            }
        (void)PORTAL;
    }
    // obsidian pillars for end gateways (like vanilla outer end pillars)
    if (std::abs(cx) < 3 && std::abs(cz) < 3 && (cx!=0 || cz!=0)) {
        // occasional obsidian spikes near origin ring
        for (int lz=0; lz<16; ++lz) for (int lx=0; lx<16; ++lx) {
            const std::int32_t wx = cx*16+lx, wz = cz*16+lz;
            double r = std::sqrt(double(wx)*wx + double(wz)*wz);
            if (r > 70 && r < 90 && TerrainGenerator::posHash(srv_seed ^ 0x5050, wx, wz, 3) < 0.005) {
                for (int y=64; y<80; ++y) {
                    const int wy = y - kMinY;
                    c.blocks[Chunk::index(wy>>4, wy&15, lz, lx)] = OBSIDIAN;
                }
            }
        }
    }
}

} // namespace cppfm
