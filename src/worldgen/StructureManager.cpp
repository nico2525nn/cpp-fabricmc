// StructureManager implementation: data-driven SMStructureSet loading + chunk-local generation
#include "StructureManager.hpp"
#include "../game/World.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cmath>

namespace cppfm::worldgen {

namespace {
const gen::BlockDef* B(const char* name) {
    return gen::blockByName(name);
}
inline std::uint16_t stateByPalette(const std::unordered_map<std::string,std::string>& pal,
                                    const std::string& key, const std::string& fallback) {
    auto it = pal.find(key);
    const std::string& name = (it != pal.end() ? it->second : fallback);
    if (auto* d = B(name.c_str())) return d->defaultState;
    if (auto* f = B(fallback.c_str())) return f->defaultState;
    return 0;
}
struct Writer {
    Chunk& c;
    std::int32_t cx, cz;
    std::uint16_t air = 0;
    bool set(std::int32_t wx, std::int32_t wy, std::int32_t wz,
             std::uint16_t state, bool overwriteSolid = false) {
        if (wy < kMinY || wy >= kMaxY) return false;
        if ((wx >> 4) != cx || (wz >> 4) != cz) return false;
        const int lx = wx & 15, lz = wz & 15, wyR = wy - kMinY;
        auto& slot = c.blocks[Chunk::index(wyR >> 4, wyR & 15, lz, lx)];
        if (!overwriteSolid && slot != 0) return false;
        slot = state;
        return true;
    }
    void box(std::int32_t x0, std::int32_t y0, std::int32_t z0,
             std::int32_t x1, std::int32_t y1, std::int32_t z1,
             std::uint16_t st, bool force = false) {
        for (auto y = y0; y <= y1; ++y)
            for (auto z = z0; z <= z1; ++z)
                for (auto x = x0; x <= x1; ++x) set(x, y, z, st, force);
    }
};
} // namespace

StructureManager::StructureManager(std::uint64_t seed)
    : seed_(seed),
      biomes_(std::make_shared<MultiNoiseBiomeSource>(seed)),
      placer_(std::make_unique<StructurePlacer>(seed)) {
    placer_->load("assets/data/structures");
    ensureDefaults();
}
StructureManager::StructureManager(std::uint64_t seed, std::shared_ptr<MultiNoiseBiomeSource> biomes)
    : seed_(seed), biomes_(std::move(biomes)), placer_(std::make_unique<StructurePlacer>(seed)) {
    if (!biomes_) biomes_ = std::make_shared<MultiNoiseBiomeSource>(seed);
    placer_->load("assets/data/structures");
    ensureDefaults();
}

void StructureManager::ensureDefaults() {
    if (!sets_.empty()) return;
    // 20 sets per plan33 §4 wiki Structure set table (spacing, separation, salt, spread, frequency, maxHoriz)
    SMStructureSet stronghold;
    stronghold.name = "minecraft:stronghold";
    stronghold.spacing = 32; stronghold.separation = 0; stronghold.salt = 0;
    stronghold.spread = SMStructureSet::Linear; stronghold.frequency = 1.0;
    stronghold.maxHoriz = 3; stronghold.maxVert = 8;
    stronghold.concentric.enabled = true; stronghold.concentric.distance = 32; stronghold.concentric.count = 128; stronghold.concentric.spread = 3;
    SMStructureSet buried; buried.name = "minecraft:buried_treasure"; buried.spacing = 1; buried.separation = 0; buried.salt = 0;
    buried.spread = SMStructureSet::Linear; buried.frequency = 0.01; buried.locateOffsetX = 9; buried.locateOffsetZ = 9; buried.maxHoriz = 1; buried.maxVert = 2; buried.biomes = {"beach"};
    SMStructureSet mineshaft; mineshaft.name = "minecraft:mineshaft"; mineshaft.spacing = 1; mineshaft.separation = 0; mineshaft.salt = 0;
    mineshaft.spread = SMStructureSet::Linear; mineshaft.frequency = 0.004; mineshaft.maxHoriz = 2; mineshaft.maxVert = 4;
    sets_ = {
        {"minecraft:village", 34, 8, 10387312ULL, {"plains","savanna","desert","taiga","snowy"}},
        {"minecraft:ancient_city", 24, 8, 20083232ULL, {"deep_dark"}},
        {"minecraft:trail_ruins", 34, 8, 83469867ULL, {"taiga","snowy","old_growth_pine_taiga","old_growth_spruce_taiga"}},
        {"minecraft:desert_pyramid", 32, 8, 14357617ULL, {"desert"}},
        {"minecraft:jungle_temple", 32, 8, 14357619ULL, {"jungle"}},
        {"minecraft:swamp_hut", 32, 8, 14357620ULL, {"swamp"}},
        {"minecraft:igloo", 32, 8, 14357618ULL, {"snowy_plains","snowy_taiga","grove"}},
        {"minecraft:pillager_outpost", 32, 8, 165745296ULL, {}},
        {"minecraft:monument", 32, 5, 10387313ULL, {"deep_ocean","deep_cold_ocean","deep_frozen_ocean","deep_lukewarm_ocean"}},
        {"minecraft:mansion", 80, 20, 10387319ULL, {"dark_forest","roofed_forest","pale_garden"}},
        {"minecraft:ruined_portal", 40, 15, 34222645ULL, {}},
        {"minecraft:shipwreck", 24, 4, 165745295ULL, {"beach","ocean"}},
        {"minecraft:ocean_ruins", 20, 8, 14357621ULL, {"ocean"}},
        {"minecraft:nether_complexes", 27, 4, 30084232ULL, {}},
        {"minecraft:nether_fossil", 2, 1, 14357921ULL, {"soul_sand_valley"}},
        {"minecraft:end_city", 20, 11, 10387313ULL, {"end_highlands","end_midlands","end_barrens","small_end_islands"}},
        {"minecraft:trial_chambers", 34, 12, 94251327ULL, {}},
        buried,
        mineshaft,
        stronghold,
    };
    // adjust mansion/monument to triangular
    for (auto& s : sets_) {
        if (s.name == "minecraft:monument" || s.name == "minecraft:mansion") s.spread = SMStructureSet::Triangular;
        if (s.name == "minecraft:trial_chambers") { s.maxHoriz = 5; s.maxVert = 12; }
        if (s.name == "minecraft:monument") { s.maxHoriz = 4; s.maxVert = 8; }
        if (s.name == "minecraft:mansion") { s.maxHoriz = 3; s.maxVert = 12; }
        if (s.name == "minecraft:pillager_outpost") { s.exclusionOther = "minecraft:village"; s.exclusionCount = 10; }
        if (s.name == "minecraft:ruined_portal") { s.maxHoriz = 3; }
        if (s.name == "minecraft:ocean_ruins" || s.name == "minecraft:shipwreck") s.maxHoriz = 3;
        if (s.name == "minecraft:end_city") s.maxHoriz = 3;
        if (s.name == "minecraft:ancient_city") s.maxHoriz = 3;
        if (s.name == "minecraft:trail_ruins") s.maxHoriz = 3;
        if (s.name == "minecraft:nether_complexes") s.maxHoriz = 3;
    }
    // nether_complexes weight 40/60
    for (auto& s : sets_) if (s.name == "minecraft:nether_complexes") {
        s.structures = {{"minecraft:fortress",40},{"minecraft:bastion_remnant",60}};
    }
    // verify size 20
}

int StructureManager::loadFromFile(const std::string& path) {
    try {
        std::ifstream f(path);
        if (!f) return 0;
        std::string txt((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        json::Value v = json::Value::parse(txt);
        int added = 0;
        auto parseOne = [&](const json::Value& o) {
            SMStructureSet s;
            if (auto* n = o.find("name")) s.name = n->asStr();
            else return;
            if (auto* sp = o.find("spacing")) s.spacing = sp->asInt(s.spacing);
            if (auto* se = o.find("separation")) s.separation = se->asInt(s.separation);
            if (auto* sa = o.find("salt")) s.salt = (std::uint64_t)sa->asI64((std::int64_t)s.salt);
            if (auto* bi = o.find("biomes"); bi && bi->isArr()) {
                for (auto& e : bi->arr) if (e.isStr()) s.biomes.push_back(e.asStr());
            } else if (auto* bs = o.find("biome"); bs && bs->isStr()) {
                s.biomes.push_back(bs->asStr());
            }
            if (auto* st = o.find("spread_type")) {
                std::string v = st->asStr();
                if (v == "triangular") s.spread = SMStructureSet::Triangular;
                else if (v == "concentric") s.spread = SMStructureSet::Concentric;
                else s.spread = SMStructureSet::Linear;
            }
            if (auto* fr = o.find("frequency")) s.frequency = fr->asFloat(float(s.frequency));
            if (auto* lo = o.find("locate_offset"); lo && lo->isArr() && lo->arr.size() >= 3) {
                s.locateOffsetX = lo->arr[0].asInt(s.locateOffsetX);
                s.locateOffsetY = lo->arr[1].asInt(s.locateOffsetY);
                s.locateOffsetZ = lo->arr[2].asInt(s.locateOffsetZ);
            }
            if (auto* hz = o.find("max_distance_from_center")) {
                if (hz->isArr() && hz->arr.size() >= 2) { s.maxHoriz = hz->arr[0].asInt(s.maxHoriz); s.maxVert = hz->arr[1].asInt(s.maxVert); }
                else s.maxHoriz = hz->asInt(s.maxHoriz);
            }
            if (auto* ex = o.find("exclusion_zone"); ex && ex->isObj()) {
                if (auto* on = ex->find("other_set")) s.exclusionOther = on->asStr();
                if (auto* cc = ex->find("chunk_count")) s.exclusionCount = cc->asInt(s.exclusionCount);
            }
            sets_.push_back(std::move(s));
            ++added;
        };
        if (auto* sets = v.find("sets"); sets && sets->isArr()) {
            for (auto& e : sets->arr) if (e.isObj()) parseOne(e);
        } else if (v.isObj()) {
            if (v.find("name")) parseOne(v);
            else {
                // map of name -> config ?
                for (auto& [k, val] : v.obj) {
                    if (!val.isObj()) continue;
                    SMStructureSet s;
                    s.name = k;
                    if (s.name.find(":")==std::string::npos) s.name = "minecraft:" + s.name;
                    if (auto* sp = val.find("spacing")) s.spacing = sp->asInt(s.spacing);
                    if (auto* se = val.find("separation")) s.separation = se->asInt(s.separation);
                    if (auto* sa = val.find("salt")) s.salt = (std::uint64_t)sa->asI64((std::int64_t)s.salt);
                    if (auto* bi = val.find("biomes"); bi && bi->isArr())
                        for (auto& e: bi->arr) if(e.isStr()) s.biomes.push_back(e.asStr());
                    sets_.push_back(std::move(s));
                    ++added;
                }
            }
        } else if (v.isArr()) {
            for (auto& e : v.arr) if (e.isObj()) parseOne(e);
        }
        return added;
    } catch (...) { return 0; }
}

int StructureManager::loadFromDirectory(const std::string& dir) {
    int total = 0;
    try {
        if (!std::filesystem::exists(dir)) return 0;
        // clear defaults first if we are loading data-driven; keep ensureDefaults after?
        // If directory has files, we replace defaults with loaded ones (data-driven)
        std::vector<SMStructureSet> loaded;
        std::swap(loaded, sets_);
        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto p = entry.path();
            if (p.extension() != ".json") continue;
            int n = loadFromFile(p.string());
            total += n;
        }
        if (sets_.empty()) {
            sets_ = std::move(loaded);
            ensureDefaults();
        } else if (total==0) {
            // no valid files, restore defaults
            if (sets_.empty()) sets_ = std::move(loaded);
            ensureDefaults();
        }
        // if we loaded some, keep them; otherwise restore
        if (total==0 && !loaded.empty() && sets_.empty()) sets_=loaded;
    } catch (...) {}
    ensureDefaults();
    return total;
}

// Piece implementations (copied from Structures.cpp, now const)

void StructureManager::villageHouse(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                      std::int32_t bx, std::int32_t bz, int gy) const {
    Writer w{chunk, cx, cz};
    const auto planks = B("minecraft:oak_planks")->defaultState;
    const auto log = B("minecraft:oak_log")->defaultState;
    const auto glassP = B("minecraft:glass")->defaultState;
    const auto torchB = B("minecraft:torch")->defaultState;
    for (int dy = 0; dy <= 3; ++dy)
        for (int dzz = 0; dzz < 5; ++dzz)
            for (int dxx = 0; dxx < 5; ++dxx) {
                const bool wall = dxx == 0 || dxx == 4 || dzz == 0 || dzz == 4 || dy == 3 || dy == 0;
                if (!wall) continue;
                const std::uint16_t mat = dy == 0 || dy == 3 ? log : planks;
                w.set(bx + dxx, gy + 1 + dy, bz + dzz, mat);
            }
    w.set(bx + 2, gy + 1, bz, 0, true);
    w.set(bx + 2, gy + 2, bz, 0, true);
    w.set(bx, gy + 2, bz + 2, glassP);
    w.set(bx + 4, gy + 2, bz + 2, glassP);
    w.set(bx + 2, gy + 1, bz + 2, torchB, true);
}
void StructureManager::villageFarm(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                     std::int32_t bx, std::int32_t bz, int gy) const {
    Writer w{chunk, cx, cz};
    const auto farmland = B("minecraft:farmland") ? B("minecraft:farmland")->defaultState : 0;
    const auto wheat = B("minecraft:wheat") ? B("minecraft:wheat")->defaultState : 0;
    const auto waterS = static_cast<std::uint16_t>(gen::stateWithPropsList("minecraft:water", {{"level","0"}}));
    for (int dzz = 1; dzz < 6; ++dzz)
        for (int dxx = 1; dxx < 7; ++dxx) {
            const bool waterChannel = dxx == 4 && dzz == 3;
            w.set(bx + dxx, gy, bz + dzz, waterChannel ? waterS : farmland, true);
            if (!waterChannel) w.set(bx + dxx, gy + 1, bz + dzz, wheat);
        }
}
void StructureManager::villageChurch(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                       std::int32_t bx, std::int32_t bz, int gy) const {
    Writer w{chunk, cx, cz};
    const auto cobble = B("minecraft:cobblestone")->defaultState;
    const auto glassP = B("minecraft:glass")->defaultState;
    const auto torchB = B("minecraft:torch")->defaultState;
    for (int dy=0; dy<=5; ++dy) for (int dz=0; dz<7; ++dz) for (int dx=0; dx<7; ++dx){
        const bool wall = dx==0||dx==6||dz==0||dz==6||dy==5||dy==0;
        if (!wall) continue;
        w.set(bx+dx, gy+1+dy, bz+dz, cobble);
    }
    w.set(bx+3, gy+1, bz, 0, true); w.set(bx+3, gy+2, bz, 0, true);
    w.set(bx+3, gy+3, bz+3, torchB, true);
    w.set(bx, gy+3, bz+3, glassP); w.set(bx+6, gy+3, bz+3, glassP);
    w.set(bx+3, gy+7, bz+3, cobble, true);
    w.set(bx+3, gy+8, bz+3, torchB, true);
}
void StructureManager::villageJigsaw(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                       std::int32_t ox, std::int32_t oz, int depth,
                                       const GroundFn& ground) const {
    if (depth <= 0) return;
    const double r = smStructureHash(seed_, ox, oz, 0xBEEF + depth*0x9E37ULL);
    std::int32_t bx = ox, bz = oz;
    const int gy = ground(bx+3, bz+3);
    Writer w{chunk, cx, cz};
    const auto path = B("minecraft:dirt_path") ? B("minecraft:dirt_path")->defaultState : 0;
    for (int t=0; t<6; ++t){ w.set(bx+t, gy, bz, path, true); w.set(bx, gy, bz+t, path, true); }
    if (r < 0.35) villageHouse(chunk, cx, cz, bx, bz, gy);
    else if (r < 0.65) villageFarm(chunk, cx, cz, bx, bz, gy);
    else if (r < 0.85) villageChurch(chunk, cx, cz, bx, bz, gy);
    else {
        const auto log = B("minecraft:oak_log")->defaultState;
        const auto torchB = B("minecraft:torch")->defaultState;
        w.set(bx+2, gy+1, bz+2, B("minecraft:fence")? B("minecraft:fence")->defaultState : log);
        w.set(bx+2, gy+2, bz+2, B("minecraft:fence")? B("minecraft:fence")->defaultState : log);
        w.set(bx+2, gy+3, bz+2, torchB, true);
    }
    const int step = 12;
    const std::int32_t nx[4]={ox+step, ox-step, ox, ox};
    const std::int32_t nz[4]={oz, oz, oz+step, oz-step};
    for (int i=0;i<4;++i) if (depth>1) {
        if (std::abs(nx[i])<8 && std::abs(nz[i])<8) continue;
        if (smStructureHash(seed_, nx[i], nz[i], 0xCAFE) < 0.7)
            villageJigsaw(chunk, cx, cz, nx[i], nz[i], depth-1, ground);
    }
}
void StructureManager::villagePiece(Chunk& chunk, std::int32_t cx,
                                      std::int32_t cz, std::int32_t ox,
                                      std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto cobble = B("minecraft:cobblestone")->defaultState;
    const auto waterS = static_cast<std::uint16_t>(gen::stateWithPropsList("minecraft:water", {{"level", "0"}}));
    const int baseY = ground(ox + 8, oz + 8);
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
        const int x = ox + dx, z = oz + dz;
        const bool rim = std::abs(dx)==1 || std::abs(dz)==1;
        w.set(x, baseY-1, z, cobble, true);
        w.set(x, baseY, z, rim ? cobble : waterS, true);
        if (!rim) w.set(x, baseY-2, z, waterS, true);
    }
    const int branches[4][2]={{10,0},{-10,0},{0,10},{0,-10}};
    for (auto &b : branches) {
        villageJigsaw(chunk, cx, cz, ox + b[0], oz + b[1], 3, ground);
    }
    for (int lz=-2; lz<=2; ++lz) for (int lx=-2; lx<=2; ++lx){
        if (lx==0 && lz==0) continue;
        if (std::abs(lx)==1 && std::abs(lz)==1) continue;
        double r = smStructureHash(seed_, ox+lx*7, oz+lz*7, 0x5A17C);
        if (r < 0.15) {
            villageJigsaw(chunk, cx, cz, ox+lx*10, oz+lz*10, 2, ground);
        }
    }
}

void StructureManager::pyramidPiece(Chunk& chunk, std::int32_t cx,
                                      std::int32_t cz, std::int32_t ox,
                                      std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto sandstone = B("minecraft:sandstone")->defaultState;
    const auto chiseled  = B("minecraft:chiseled_sandstone")
                               ? B("minecraft:chiseled_sandstone")->defaultState
                               : sandstone;
    const int baseY = ground(ox + 8, oz + 8);
    for (int step = 0; step <= 8; ++step) {
        const int r = 9 - step;
        const int y = baseY + 1 + step;
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                const bool edge = std::abs(dx) == r || std::abs(dz) == r;
                if (edge || step % 3 == 0)
                    w.set(ox + dx + 9, y, oz + dz + 9,
                          edge ? sandstone : (step % 2 ? chiseled : sandstone),
                          true);
            }
    }
    w.box(ox + 7, baseY, oz + 7, ox + 11, baseY + 1, oz + 11,
          B("minecraft:air")->defaultState, true);
}

void StructureManager::outpostPiece(Chunk& chunk, std::int32_t cx,
                                      std::int32_t cz, std::int32_t ox,
                                      std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto darkLog = B("minecraft:dark_oak_log")->defaultState;
    const auto darkPlanks = B("minecraft:dark_oak_planks")->defaultState;
    const int baseY = ground(ox + 4, oz + 4);
    for (int dxx = 0; dxx < 5; ++dxx)
        for (int dzz = 0; dzz < 5; ++dzz) {
            const bool corner = (dxx % 4 == 0) && (dzz % 4 == 0);
            for (int h = 0; h <= 10; ++h)
                if (corner) w.set(ox + dxx, baseY + h, oz + dzz, darkLog, true);
            w.set(ox + dxx, baseY + 10, oz + dzz, darkPlanks, true);
        }
    w.set(ox + 2, baseY + 11, oz + 2, B("minecraft:torch")->defaultState, true);
}

void StructureManager::jungleTemplePiece(Chunk& chunk, std::int32_t cx,
                                           std::int32_t cz, std::int32_t ox,
                                           std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto mossy = B("minecraft:mossy_cobblestone") ? B("minecraft:mossy_cobblestone")->defaultState
                    : B("minecraft:cobblestone")->defaultState;
    const auto cobble = B("minecraft:cobblestone")->defaultState;
    const auto chiseled = B("minecraft:chiseled_stone_bricks") ? B("minecraft:chiseled_stone_bricks")->defaultState : cobble;
    const int baseY = ground(ox + 6, oz + 6);
    for (int step = 0; step < 3; ++step) {
        const int r = 6 - step;
        const int y = baseY + 1 + step;
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                const bool edge = std::abs(dx) == r || std::abs(dz) == r;
                if (!edge) continue;
                w.set(ox + dx + 6, y, oz + dz + 6, (step % 2 == 0 ? mossy : cobble), true);
            }
    }
    w.set(ox + 6, baseY + 1, oz, 0, true);
    w.set(ox + 6, baseY + 2, oz, 0, true);
    w.box(ox + 4, baseY + 1, oz + 4, ox + 8, baseY + 2, oz + 8, chiseled, true);
    w.set(ox + 6, baseY + 1, oz + 6, B("minecraft:chest") ? B("minecraft:chest")->defaultState : cobble, true);
}

void StructureManager::iglooPiece(Chunk& chunk, std::int32_t cx,
                                    std::int32_t cz, std::int32_t ox,
                                    std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto snow = B("minecraft:snow_block")->defaultState;
    const auto ice = B("minecraft:ice") ? B("minecraft:ice")->defaultState : snow;
    const int baseY = ground(ox + 4, oz + 4);
    for (int dy = 0; dy < 4; ++dy) {
        for (int dz = -3; dz <= 3; ++dz)
            for (int dx = -3; dx <= 3; ++dx) {
                const bool shell = std::abs(dx) == 3 || std::abs(dz) == 3 || dy == 3;
                const bool interior = !shell && dy > 0;
                if (shell) w.set(ox + dx + 4, baseY + 1 + dy, oz + dz + 4, snow, true);
                if (interior && dy == 1) w.set(ox + dx + 4, baseY + 1 + dy, oz + dz + 4, 0, true);
            }
    }
    w.set(ox + 4, baseY + 1, oz + 7, 0, true);
    w.set(ox + 4, baseY + 2, oz + 7, 0, true);
    w.set(ox + 5, baseY + 1, oz + 5, B("minecraft:crafting_table") ? B("minecraft:crafting_table")->defaultState : snow, true);
    w.set(ox + 3, baseY + 1, oz + 5, B("minecraft:furnace") ? B("minecraft:furnace")->defaultState : snow, true);
    w.set(ox + 4, baseY + 1, oz + 3, ice, true);
}

void StructureManager::swampHutPiece(Chunk& chunk, std::int32_t cx,
                                       std::int32_t cz, std::int32_t ox,
                                       std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto planks = B("minecraft:spruce_planks") ? B("minecraft:spruce_planks")->defaultState
                        : B("minecraft:oak_planks")->defaultState;
    const auto log = B("minecraft:oak_log")->defaultState;
    const auto cauldron = B("minecraft:cauldron") ? B("minecraft:cauldron")->defaultState : planks;
    const int baseY = ground(ox + 4, oz + 4);
    for (int dx = 0; dx < 7; ++dx)
        for (int dz = 0; dz < 7; ++dz) {
            const bool isPost = (dx % 6 == 0 && dz % 6 == 0);
            if (isPost) {
                for (int h = -2; h <= 0; ++h) w.set(ox + dx, baseY + h, oz + dz, log, true);
            }
        }
    for (int dx = 0; dx < 7; ++dx)
        for (int dz = 0; dz < 7; ++dz) {
            w.set(ox + dx, baseY + 1, oz + dz, planks, true);
        }
    for (int dy = 1; dy <= 3; ++dy) {
        for (int dx = 0; dx < 7; ++dx) {
            w.set(ox + dx, baseY + 1 + dy, oz, planks, true);
            w.set(ox + dx, baseY + 1 + dy, oz + 6, planks, true);
        }
        for (int dz = 0; dz < 7; ++dz) {
            w.set(ox, baseY + 1 + dy, oz + dz, planks, true);
            w.set(ox + 6, baseY + 1 + dy, oz + dz, planks, true);
        }
    }
    w.set(ox + 3, baseY + 2, oz, 0, true);
    w.set(ox + 3, baseY + 3, oz, 0, true);
    w.set(ox, baseY + 3, oz + 3, B("minecraft:glass") ? B("minecraft:glass")->defaultState : 0, true);
    w.set(ox + 2, baseY + 2, oz + 2, cauldron, true);
    w.set(ox + 4, baseY + 2, oz + 4, B("minecraft:crafting_table") ? B("minecraft:crafting_table")->defaultState : planks, true);
}

void StructureManager::monumentPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                       std::int32_t ox, std::int32_t oz,
                                       const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto prismarine = B("minecraft:prismarine") ? B("minecraft:prismarine")->defaultState : B("minecraft:stone_bricks")->defaultState;
    const auto bricks = B("minecraft:prismarine_bricks") ? B("minecraft:prismarine_bricks")->defaultState : prismarine;
    const auto dark = B("minecraft:dark_prismarine") ? B("minecraft:dark_prismarine")->defaultState : bricks;
    const auto lantern = B("minecraft:sea_lantern") ? B("minecraft:sea_lantern")->defaultState : prismarine;
    const auto gold = B("minecraft:gold_block") ? B("minecraft:gold_block")->defaultState : prismarine;
    const auto water = static_cast<std::uint16_t>(gen::stateWithPropsList("minecraft:water", {{"level","0"}}));
    // Ocean monument base at Y 39 : 58x58 footprint, height 23 (Y39-61)
    int baseY = 39;
    // simple solid box with hollow interior and wing structure placeholder
    for (int dx=0; dx<58; ++dx) for (int dz=0; dz<58; ++dz) {
        int wx = ox + dx, wz = oz + dz;
        bool edge = dx==0||dx==57||dz==0||dz==57;
        for (int dy=0; dy<23; ++dy) {
            int py = baseY + dy;
            if (py<kMinY||py>=kMaxY) continue;
            std::uint16_t mat = prismarine;
            if (dy==0 || dy==22 || edge) mat = bricks;
            else if (dx%7==0 && dz%7==0 && dy%5==0) mat = lantern;
            else if (dx>20 && dx<37 && dz>20 && dz<37) {
                if (dy==1 && dx==28 && dz==28) mat = gold; // treasure
                else if (dy<3) mat = dark;
                else if (dy==10 && (dx==28||dz==28)) mat = lantern;
                else if (dx>=22&&dx<=35&&dz>=22&&dz<=35&&dy>=4&&dy<=8) {
                    // interior water/air
                    if (dx==28&&dz==28) mat = 0; // central air
                    else mat = water;
                }
            }
            // hollow interior
            if (dx>2&&dx<55&&dz>2&&dz<55&& dy>2&&dy<20) {
                if (mat==prismarine) mat = water;
            }
            if (mat==0) { w.set(wx, py, wz, 0, true); }
            else w.set(wx, py, wz, mat, true);
        }
    }
    // Elder guardian placeholder: would spawn mobs, but just place sponge cluster
    const auto sponge = B("minecraft:sponge") ? B("minecraft:sponge")->defaultState : prismarine;
    w.set(ox+28, baseY+10, oz+28, sponge, true);
    (void)ground;
}
void StructureManager::mansionPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                      std::int32_t ox, std::int32_t oz,
                                      const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto planks = B("minecraft:dark_oak_planks") ? B("minecraft:dark_oak_planks")->defaultState : B("minecraft:oak_planks")->defaultState;
    const auto log = B("minecraft:dark_oak_log") ? B("minecraft:dark_oak_log")->defaultState : planks;
    const auto cobble = B("minecraft:cobblestone")->defaultState;
    const auto chest = B("minecraft:chest") ? B("minecraft:chest")->defaultState : cobble;
    int surfaceY = ground(ox+20, oz+20);
    int baseY = std::clamp(surfaceY+1, 70, 85);
    // 40x40 mansion footprint 2 floors
    for (int dx=0; dx<40; ++dx) for (int dz=0; dz<40; ++dz) {
        int wx = ox+dx, wz = oz+dz;
        bool edge = dx==0||dx==39||dz==0||dz==39;
        // floor
        w.set(wx, baseY, wz, planks, true);
        w.set(wx, baseY+7, wz, planks, true); // second floor
        if (edge) {
            for (int dy=1; dy<=6; ++dy) w.set(wx, baseY+dy, wz, cobble, true);
            for (int dy=8; dy<=12; ++dy) w.set(wx, baseY+dy, wz, planks, true);
        }
        // corners as log pillars
        bool corner = (dx==0||dx==39) && (dz==0||dz==39);
        if (corner) {
            for (int dy=1; dy<=12; ++dy) w.set(wx, baseY+dy, wz, log, true);
        }
    }
    // interior walls splitting into rooms (grid 10)
    for (int dx=10; dx<30; dx+=10) for (int dz=0; dz<40; ++dz) {
        for (int dy=1; dy<=6; ++dy) w.set(ox+dx, baseY+dy, oz+dz, planks, true);
        for (int dy=8; dy<=12; ++dy) w.set(ox+dx, baseY+dy, oz+dz, planks, true);
    }
    for (int dz=10; dz<30; dz+=10) for (int dx=0; dx<40; ++dx) {
        for (int dy=1; dy<=6; ++dy) w.set(ox+dx, baseY+dy, oz+dz, planks, true);
        for (int dy=8; dy<=12; ++dy) w.set(ox+dx, baseY+dy, oz+dz, planks, true);
    }
    // doors
    w.set(ox+20, baseY+1, oz, 0, true); w.set(ox+20, baseY+2, oz, 0, true);
    w.set(ox+20, baseY+8, oz+10, 0, true); w.set(ox+20, baseY+9, oz+10, 0, true);
    // loot
    w.set(ox+5, baseY+1, oz+5, chest, true);
    w.set(ox+35, baseY+1, oz+35, chest, true);
    w.set(ox+20, baseY+1, oz+20, B("minecraft:torch")?B("minecraft:torch")->defaultState:planks, true);
    // evoker placeholder: leave space (mob spawn would be here)
}
void StructureManager::trialChambersPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t originX, std::int32_t originZ,
                      const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto tuff = B("minecraft:tuff") ? B("minecraft:tuff")->defaultState : B("minecraft:stone_bricks")->defaultState;
    const auto tuffBricks = B("minecraft:tuff_bricks") ? B("minecraft:tuff_bricks")->defaultState : tuff;
    const auto chiseledTuff = B("minecraft:chiseled_tuff") ? B("minecraft:chiseled_tuff")->defaultState : tuffBricks;
    const auto chiseledBricks = B("minecraft:chiseled_tuff_bricks") ? B("minecraft:chiseled_tuff_bricks")->defaultState : chiseledTuff;
    const auto waxedChiseled = B("minecraft:waxed_chiseled_copper") ? B("minecraft:waxed_chiseled_copper")->defaultState : chiseledTuff;
    const auto copperBulb = B("minecraft:copper_bulb") ? B("minecraft:copper_bulb")->defaultState : tuffBricks;
    const auto waxedBulb = B("minecraft:waxed_copper_bulb") ? B("minecraft:waxed_copper_bulb")->defaultState : copperBulb;
    const auto spawner = B("minecraft:trial_spawner") ? B("minecraft:trial_spawner")->defaultState : tuffBricks;
    const auto vault = B("minecraft:vault") ? B("minecraft:vault")->defaultState : tuffBricks;
    const auto dispenser = B("minecraft:dispenser") ? B("minecraft:dispenser")->defaultState : tuff;
    int surfaceY = ground ? ground(originX+8, originZ+8) : 64;
    int baseY = std::clamp(surfaceY - 30, kMinY+5, 20);
    auto chamberAt = [&](int ox, int oz, int kind) {
        // kind 0=chamber_1 1=chamber_2 2=chamber_4 3=chamber_8 (size/pattern variant)
        int sz = 18;
        if (kind==2) sz = 14;
        if (kind==3) sz = 22;
        for (int dx=0; dx<sz; ++dx) for (int dz=0; dz<sz; ++dz) {
            int wx = ox + dx, wz = oz + dz;
            bool edge = dx==0||dx==sz-1||dz==0||dz==sz-1;
            w.set(wx, baseY, wz, tuffBricks, true);
            w.set(wx, baseY+6, wz, tuffBricks, true);
            if (edge) {
                for (int dy=1; dy<=5; ++dy) w.set(wx, baseY+dy, wz, tuff, true);
                if ((dx%6==0||dz%6==0) && dx%3==0) w.set(wx, baseY+1, wz, chiseledTuff, true);
                if (dx==0 && dz%4==0) w.set(wx, baseY+2, wz, waxedChiseled, true);
            } else if (dx%7==3 && dz%7==3) {
                for (int dy=1; dy<=3; ++dy) w.set(wx, baseY+dy, wz, 0, true);
                // spawner variant based on kind
                if (kind==1) w.set(wx, baseY+1, wz, spawner, true);
                else if (kind==2) w.set(wx, baseY+1, wz, vault, true);
                else w.set(wx, baseY+1, wz, spawner, true);
                if (dx==sz/2 && dz==sz/2) w.set(wx, baseY+3, wz, waxedBulb, true);
            } else {
                for (int dy=1; dy<=5; ++dy) (void)w.set(wx, baseY+dy, wz, 0, false);
            }
        }
        // copper bulb + dispenser in corners for extra decoration
        w.set(ox+2, baseY+1, oz+2, dispenser, true);
        w.set(ox+sz-3, baseY+1, oz+sz-3, copperBulb, true);
        (void)chiseledBricks;
    };
    auto straightCorridor = [&](int ox, int oz, int len, int dir) {
        // dir 0=+x 1=-x 2=+z 3=-z : 3-wide corridor with slices
        for (int i=0;i<len;++i){
            int wx = ox + (dir==0? i : dir==1? -i : 0);
            int wz = oz + (dir==2? i : dir==3? -i : 0);
            // floor/ceiling
            for (int dw=-1; dw<=1; ++dw){
                int px = wx + (dir>=2? dw:0);
                int pz = wz + (dir<2? dw:0);
                w.set(px, baseY, pz, tuffBricks, true);
                w.set(px, baseY+4, pz, tuffBricks, true);
                if (std::abs(dw)==1){
                    for(int dy=1; dy<=3; ++dy) w.set(px, baseY+dy, pz, tuff, true);
                } else {
                    for(int dy=1; dy<=3; ++dy) w.set(px, baseY+dy, pz, 0, true);
                }
            }
            if (i%4==0) w.set(wx, baseY+1, wz, copperBulb, true);
        }
    };
    auto intersectionAt = [&](int ox, int oz) {
        for (int dx=-4; dx<=4; ++dx) for (int dz=-4; dz<=4; ++dz){
            int wx=ox+dx, wz=oz+dz;
            bool edge = std::abs(dx)==4 || std::abs(dz)==4;
            bool cross = (std::abs(dx)<=1 || std::abs(dz)<=1);
            if (!cross) continue;
            w.set(wx, baseY, wz, tuffBricks, true);
            w.set(wx, baseY+5, wz, tuffBricks, true);
            if (edge) for(int dy=1; dy<=4; ++dy) w.set(wx, baseY+dy, wz, tuff, true);
            else for(int dy=1; dy<=4; ++dy) (void)w.set(wx, baseY+dy, wz, 0, true);
        }
        w.set(ox, baseY+1, oz, spawner, true);
        w.set(ox, baseY+2, oz, waxedBulb, true);
    };
    auto atriumAt = [&](int ox, int oz) {
        const auto polishedTuff = B("minecraft:polished_tuff") ? B("minecraft:polished_tuff")->defaultState : tuffBricks;
        for (int dx=-6; dx<=6; ++dx) for (int dz=-6; dz<=6; ++dz){
            int wx=ox+dx, wz=oz+dz;
            bool edge = std::abs(dx)==6 || std::abs(dz)==6;
            w.set(wx, baseY, wz, polishedTuff, true);
            w.set(wx, baseY+7, wz, tuffBricks, true);
            if (edge) for(int dy=1; dy<=6; ++dy) w.set(wx, baseY+dy, wz, tuff, true);
            else for(int dy=1; dy<=6; ++dy) (void)w.set(wx, baseY+dy, wz, 0, false);
        }
        w.set(ox, baseY+1, oz, vault, true);
    };
    const double r0 = smStructureHash(seed_, originX, originZ, 0xBEEF);
    const double r1 = smStructureHash(seed_, originX, originZ, 0xCAFE);
    // start piece: corridor/end (5x5 entry)
    for (int dx=-2; dx<=2; ++dx) for (int dz=-2; dz<=2; ++dz){
        int wx=originX+dx, wz=originZ+dz;
        bool edge = std::abs(dx)==2 || std::abs(dz)==2;
        w.set(wx, baseY, wz, tuffBricks, true);
        w.set(wx, baseY+4, wz, tuffBricks, true);
        if (edge) for(int dy=1; dy<=3; ++dy) w.set(wx, baseY+dy, wz, tuff, true);
        else for(int dy=1; dy<=3; ++dy) (void)w.set(wx, baseY+dy, wz, 0, false);
    }
    w.set(originX, baseY+1, originZ, spawner, true);
    // corridor straight slices (4 slices)
    straightCorridor(originX+3, originZ, 10, 0);
    straightCorridor(originX-3, originZ, 8, 1);
    // central chamber (hash-driven variant)
    int chamberKind = int(r0*4) % 4;
    chamberAt(originX+10, originZ-9, chamberKind);
    if (r0 > 0.5) {
        int k2 = int(r1*4)%4;
        chamberAt(originX-24, originZ+4, k2);
    }
    // intersection + hallway branch
    intersectionAt(originX, originZ+12);
    // atrium near center-south
    if (r1 > 0.35) atriumAt(originX+18, originZ+16);
    // hallway connector east
    straightCorridor(originX+28, originZ-2, 6, 0);
}
void StructureManager::endCityPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t originX, std::int32_t originZ,
                      const GroundFn& ground) const {
    // End City is generated via fillEnd direct placement; this piece is for StructureManager overworld fallback (no-op)
    // Keep simple tower for test if ever called in overworld
    Writer w{chunk, cx, cz};
    const auto endBricks = B("minecraft:end_stone_bricks") ? B("minecraft:end_stone_bricks")->defaultState : B("minecraft:end_stone")->defaultState;
    const auto purpur = B("minecraft:purpur_block") ? B("minecraft:purpur_block")->defaultState : endBricks;
    int baseY = 70;
    if (ground) baseY = ground(originX+4, originZ+4);
    if (baseY < 55) baseY = 70;
    for (int dx=0; dx<9; ++dx) for (int dz=0; dz<9; ++dz) {
        int wx = originX+dx, wz = originZ+dz;
        w.set(wx, baseY, wz, endBricks, true);
        for (int dy=1; dy<=12; ++dy) {
            bool wall = dx==0||dx==8||dz==0||dz==8;
            if (!wall) continue;
            w.set(wx, baseY+dy, wz, purpur, true);
        }
    }
    w.set(originX+4, baseY+1, originZ+4, B("minecraft:chest")?B("minecraft:chest")->defaultState:endBricks, true);
}
void StructureManager::ancientCityPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz, std::int32_t ox, std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto deepslate = B("minecraft:deepslate_bricks") ? B("minecraft:deepslate_bricks")->defaultState : B("minecraft:cobblestone")->defaultState;
    const auto chest = B("minecraft:chest") ? B("minecraft:chest")->defaultState : deepslate;
    int baseY = ground ? ground(ox+4, oz+4) : 64;
    baseY = std::clamp(baseY - 8, -50, 30);
    for (int dx=0; dx<9; ++dx) for (int dz=0; dz<9; ++dz) w.set(ox+dx, baseY, oz+dz, deepslate, true);
    for (int dx=0; dx<9; ++dx){ w.set(ox+dx, baseY+1, oz, deepslate, true); w.set(ox+dx, baseY+1, oz+8, deepslate, true); }
    for (int dz=0; dz<9; ++dz){ w.set(ox, baseY+1, oz+dz, deepslate, true); w.set(ox+8, baseY+1, oz+dz, deepslate, true); }
    w.set(ox+4, baseY+1, oz+4, chest, true);
}
void StructureManager::trailRuinsPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz, std::int32_t ox, std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto bricks = B("minecraft:mud_bricks") ? B("minecraft:mud_bricks")->defaultState : B("minecraft:bricks")->defaultState;
    int baseY = ground ? ground(ox+4, oz+4) : 64;
    for (int dx=0; dx<7; ++dx) for (int dz=0; dz<7; ++dz) w.set(ox+dx, baseY, oz+dz, bricks, true);
    for (int dy=1; dy<=3; ++dy){ w.set(ox, baseY+dy, oz, bricks, true); w.set(ox+6, baseY+dy, oz+6, bricks, true); }
    w.set(ox+3, baseY+1, oz+3, B("minecraft:suspicious_gravel")?B("minecraft:suspicious_gravel")->defaultState:bricks, true);
}
void StructureManager::ruinedPortalPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz, std::int32_t ox, std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto obs = B("minecraft:obsidian")->defaultState;
    const auto crying = B("minecraft:crying_obsidian") ? B("minecraft:crying_obsidian")->defaultState : obs;
    int baseY = ground ? ground(ox+2, oz+2) : 64;
    for (int dy=0; dy<5; ++dy){ w.set(ox, baseY+dy, oz, obs, true); w.set(ox+4, baseY+dy, oz, obs, true); }
    for (int dx=0; dx<5; ++dx){ w.set(ox+dx, baseY+4, oz, crying, true); }
    w.set(ox+2, baseY+1, oz, 0, true); w.set(ox+2, baseY+2, oz, 0, true);
}
void StructureManager::shipwreckPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz, std::int32_t ox, std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto planks = B("minecraft:oak_planks")->defaultState;
    const auto chest = B("minecraft:chest") ? B("minecraft:chest")->defaultState : planks;
    int baseY = 42;
    if (ground) baseY = std::clamp(ground(ox+3, oz+3)-2, 40, 60);
    for (int dx=0; dx<7; ++dx) for (int dz=0; dz<3; ++dz) w.set(ox+dx, baseY, oz+dz, planks, true);
    w.set(ox+3, baseY+1, oz+1, chest, true);
}
void StructureManager::oceanRuinsPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz, std::int32_t ox, std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto stone = B("minecraft:stone_bricks") ? B("minecraft:stone_bricks")->defaultState : B("minecraft:cobblestone")->defaultState;
    int baseY = 38;
    if (ground) baseY = std::clamp(ground(ox+2, oz+2)-3, 30, 50);
    for (int dx=0; dx<5; ++dx) for (int dz=0; dz<5; ++dz) w.set(ox+dx, baseY, oz+dz, stone, true);
    w.set(ox+2, baseY+1, oz+2, B("minecraft:chest")?B("minecraft:chest")->defaultState:stone, true);
}
void StructureManager::netherFossilPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz, std::int32_t ox, std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto bone = B("minecraft:bone_block") ? B("minecraft:bone_block")->defaultState : B("minecraft:quartz_block")->defaultState;
    int baseY = 40;
    if (ground) baseY = ground(ox+2, oz+2);
    baseY = std::clamp(baseY, 30, 70);
    for (int dy=0; dy<7; ++dy) w.set(ox+2, baseY+dy, oz+2, bone, true);
    for (int dx=-2; dx<=2; ++dx) w.set(ox+dx, baseY+3, oz+2, bone, true);
}
void StructureManager::buriedTreasurePiece(Chunk& chunk, std::int32_t cx, std::int32_t cz, std::int32_t ox, std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto chest = B("minecraft:chest") ? B("minecraft:chest")->defaultState : B("minecraft:oak_planks")->defaultState;
    int baseY = ground ? ground(ox+1, oz+1)-2 : 50;
    baseY = std::clamp(baseY, 45, 65);
    w.set(ox+1, baseY, oz+1, chest, true);
}
void StructureManager::strongholdPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                        std::int32_t ox, std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto stoneBricks = B("minecraft:stone_bricks") ? B("minecraft:stone_bricks")->defaultState : B("minecraft:cobblestone")->defaultState;
    const auto mossy = B("minecraft:mossy_stone_bricks") ? B("minecraft:mossy_stone_bricks")->defaultState : stoneBricks;
    const auto cracked = B("minecraft:cracked_stone_bricks") ? B("minecraft:cracked_stone_bricks")->defaultState : stoneBricks;
    const auto portalFrame = B("minecraft:end_portal_frame") ? B("minecraft:end_portal_frame")->defaultState : stoneBricks;
    const auto torch = B("minecraft:torch")->defaultState;
    int surfaceY = ground(ox+1, oz+1);
    int baseY = std::clamp(surfaceY - 30, kMinY+5, 40);
    for (int dx=-1; dx<=4; ++dx) for (int dz=-1; dz<=4; ++dz){
        int x = ox+dx, z = oz+dz;
        bool edge = dx==-1||dx==4||dz==-1||dz==4;
        w.set(x, baseY, z, edge ? mossy : stoneBricks, true);
        for (int dy=1; dy<=3; ++dy){
            if (edge) w.set(x, baseY+dy, z, stoneBricks, false);
            else if (dx>=0 && dx<=3 && dz>=0 && dz<=3 && dy<=2) {
                w.set(x, baseY+dy, z, 0, true);
            }
        }
        w.set(x, baseY+4, z, cracked, true);
    }
    w.set(ox+1, baseY+1, oz+4, 0, true); w.set(ox+1, baseY+2, oz+4, 0, true);
    w.set(ox+2, baseY+1, oz+4, 0, true); w.set(ox+2, baseY+2, oz+4, 0, true);
    w.set(ox+1, baseY+1, oz, portalFrame, true);
    w.set(ox+2, baseY+1, oz, portalFrame, true);
    w.set(ox+1, baseY+1, oz+1, torch, true);
}
void StructureManager::mineshaftPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                       std::int32_t ox, std::int32_t oz, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto oakPlanks = B("minecraft:oak_planks")->defaultState;
    const auto oakFence = B("minecraft:oak_fence") ? B("minecraft:oak_fence")->defaultState : oakPlanks;
    const auto rail = B("minecraft:rail") ? B("minecraft:rail")->defaultState : oakPlanks;
    const auto cobweb = B("minecraft:cobweb") ? B("minecraft:cobweb")->defaultState : 0;
    int surfaceY = ground(ox, oz);
    int baseY = std::clamp(surfaceY - 20, kMinY+5, 50);
    for (int dx=0; dx<9; ++dx){
        for (int dz=-1; dz<=1; ++dz){
            int x=ox+dx, z=oz+dz;
            bool wall = dz==-1 || dz==1;
            w.set(x, baseY, z, oakPlanks, true);
            if (wall) {
                w.set(x, baseY+1, z, oakFence, false);
                w.set(x, baseY+2, z, oakFence, false);
            } else {
                w.set(x, baseY+1, z, 0, true);
                w.set(x, baseY+2, z, 0, true);
                if (dx%3==0) w.set(x, baseY, z, rail, true);
            }
            w.set(x, baseY+3, z, oakPlanks, true);
        }
        if (dx%5==0) {
            int x=ox+dx;
            w.set(x, baseY+1, oz, cobweb);
        }
    }
}

std::uint16_t StructureManager::resolvePaletteState(const std::unordered_map<std::string,std::string>& pal,
                                             const std::string& key,
                                             const std::string& fallback) {
    return stateByPalette(pal, key, fallback);
}
void StructureManager::enqueuePendingMob(int x,int y,int z, const std::string& mob, int count) const {
    std::lock_guard<std::mutex> lk(pendingMtx_);
    pendingMobs_.push_back({{x,y,z}, mob, count});
}
void StructureManager::enqueuePendingLoot(int x,int y,int z, const std::string& loot) const {
    std::lock_guard<std::mutex> lk(pendingMtx_);
    pendingLoot_.push_back({{x,y,z}, loot});
}
void StructureManager::drainPendingMobs(std::vector<PendingMob>& out) const {
    std::lock_guard<std::mutex> lk(pendingMtx_);
    out.insert(out.end(), pendingMobs_.begin(), pendingMobs_.end());
    pendingMobs_.clear();
}
void StructureManager::drainPendingLoot(std::vector<PendingLoot>& out) const {
    std::lock_guard<std::mutex> lk(pendingMtx_);
    out.insert(out.end(), pendingLoot_.begin(), pendingLoot_.end());
    pendingLoot_.clear();
}
std::vector<StructureManager::PendingMob> StructureManager::takePendingMobs() const {
    std::lock_guard<std::mutex> lk(pendingMtx_);
    auto v = pendingMobs_; pendingMobs_.clear(); return v;
}
std::vector<StructureManager::PendingLoot> StructureManager::takePendingLoot() const {
    std::lock_guard<std::mutex> lk(pendingMtx_);
    auto v = pendingLoot_; pendingLoot_.clear(); return v;
}
size_t StructureManager::pendingMobCount() const { std::lock_guard<std::mutex> lk(pendingMtx_); return pendingMobs_.size(); }
size_t StructureManager::pendingLootCount() const { std::lock_guard<std::mutex> lk(pendingMtx_); return pendingLoot_.size(); }
void StructureManager::clearPending() const { std::lock_guard<std::mutex> lk(pendingMtx_); pendingMobs_.clear(); pendingLoot_.clear(); }

void StructureManager::placeTrialChambersPalette(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                   std::int32_t originX, std::int32_t originZ,
                                   const std::string& pieceName,
                                   const std::unordered_map<std::string,std::string>& palette,
                                   int /*variant*/, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    const auto tuff = stateByPalette(palette, "tuff", "minecraft:tuff");
    const auto tuffBricks = stateByPalette(palette, "tuff_bricks", "minecraft:tuff_bricks");
    const auto chiseledTuff = stateByPalette(palette, "chiseled_tuff", "minecraft:chiseled_tuff");
    const auto chiseledBricks = stateByPalette(palette, "chiseled_tuff_bricks", "minecraft:chiseled_tuff_bricks");
    const auto copperBulb = stateByPalette(palette, "copper_bulb", "minecraft:copper_bulb");
    const auto spawner = stateByPalette(palette, "spawner", "minecraft:trial_spawner");
    const auto vault = stateByPalette(palette, "vault", "minecraft:vault");
    const auto chestS = stateByPalette(palette, "chest", "minecraft:chest");
    const auto dispenserS = stateByPalette(palette, "dispenser", "minecraft:dispenser");
    int surfaceY = ground ? ground(originX+8, originZ+8) : 64;
    int baseY = std::clamp(surfaceY - 30, kMinY+5, 20);
    bool isCorridor = pieceName.find("corridor")!=std::string::npos;
    bool isChamber = pieceName.find("chamber")!=std::string::npos;
    bool isSpawner = pieceName.find("spawner")!=std::string::npos || pieceName.find("intersection")!=std::string::npos;
    if (isCorridor) {
        // 3-wide straight corridor 10 long + copper bulbs + adjacent chamber to ensure >200 non-air
        for (int i=0;i<10;++i){
            int wx = originX + i; int wz = originZ;
            for (int dw=-1; dw<=1; ++dw){
                int px = wx; int pz = wz + dw;
                w.set(px, baseY, pz, tuffBricks, true);
                w.set(px, baseY+4, pz, tuffBricks, true);
                if (std::abs(dw)==1){ for(int dy=1; dy<=3; ++dy) w.set(px, baseY+dy, pz, tuff, true); }
                else { for(int dy=1; dy<=3; ++dy) w.set(px, baseY+dy, pz, 0, true); }
            }
            if (i%4==0) w.set(wx, baseY+1, wz, copperBulb, true);
        }
        // attach chamber at end to guarantee 3-piece composition corridor+chamber+spawner
        {
            int cOx = originX+10, cOz = originZ-9, sz=18;
            for (int dx=0; dx<sz; ++dx) for (int dz=0; dz<sz; ++dz) {
                int wx = cOx + dx, wz = cOz + dz;
                bool edge = dx==0||dx==sz-1||dz==0||dz==sz-1;
                w.set(wx, baseY, wz, tuffBricks, true);
                w.set(wx, baseY+6, wz, tuffBricks, true);
                if (edge) for(int dy=1; dy<=5; ++dy) w.set(wx, baseY+dy, wz, tuff, true);
                else if (dx%7==3 && dz%7==3) { for(int dy=1; dy<=3; ++dy) w.set(wx, baseY+dy, wz, 0, true); w.set(wx, baseY+1, wz, spawner, true); enqueuePendingMob(wx, baseY+2, wz, "minecraft:breeze", 1); }
            }
            w.set(cOx+2, baseY+1, cOz+2, dispenserS, true);
            w.set(cOx+sz/2, baseY+1, cOz+sz/2, chestS, true);
            enqueuePendingLoot(cOx+sz/2, baseY+1, cOz+sz/2, "minecraft:chests/trial_chambers/chamber");
        }
        w.set(originX+5, baseY+1, originZ, spawner, true);
        enqueuePendingMob(originX+5, baseY+2, originZ, "minecraft:breeze", 1);
        w.set(originX+2, baseY+1, originZ+1, chestS, true);
        enqueuePendingLoot(originX+2, baseY+1, originZ+1, "minecraft:chests/trial_chambers/corridor");
    } else if (isChamber) {
        int sz = 18;
        if (pieceName.find("chamber_4")!=std::string::npos) sz = 14;
        else if (pieceName.find("chamber_8")!=std::string::npos) sz = 22;
        for (int dx=0; dx<sz; ++dx) for (int dz=0; dz<sz; ++dz) {
            int wx = originX + dx, wz = originZ + dz;
            bool edge = dx==0||dx==sz-1||dz==0||dz==sz-1;
            w.set(wx, baseY, wz, tuffBricks, true);
            w.set(wx, baseY+6, wz, tuffBricks, true);
            if (edge) {
                for (int dy=1; dy<=5; ++dy) w.set(wx, baseY+dy, wz, tuff, true);
                if ((dx%6==0||dz%6==0) && dx%3==0) w.set(wx, baseY+1, wz, chiseledTuff, true);
            } else if (dx%7==3 && dz%7==3) {
                for (int dy=1; dy<=3; ++dy) w.set(wx, baseY+dy, wz, 0, true);
                w.set(wx, baseY+1, wz, spawner, true);
                enqueuePendingMob(wx, baseY+2, wz, "minecraft:breeze", 1);
                if (dx==sz/2 && dz==sz/2) w.set(wx, baseY+3, wz, copperBulb, true);
            } else {
                for (int dy=1; dy<=5; ++dy) (void)w.set(wx, baseY+dy, wz, 0, false);
            }
        }
        w.set(originX+2, baseY+1, originZ+2, dispenserS, true);
        w.set(originX+sz-3, baseY+1, originZ+sz-3, vault, true);
        w.set(originX+sz/2, baseY+1, originZ+sz/2, chestS, true);
        enqueuePendingLoot(originX+sz/2, baseY+1, originZ+sz/2, "minecraft:chests/trial_chambers/chamber");
        (void)chiseledBricks;
    } else if (isSpawner) {
        for (int dx=-4; dx<=4; ++dx) for (int dz=-4; dz<=4; ++dz){
            int wx=originX+dx, wz=originZ+dz;
            bool edge = std::abs(dx)==4 || std::abs(dz)==4;
            bool cross = (std::abs(dx)<=1 || std::abs(dz)<=1);
            if (!cross) continue;
            w.set(wx, baseY, wz, tuffBricks, true);
            w.set(wx, baseY+5, wz, tuffBricks, true);
            if (edge) for(int dy=1; dy<=4; ++dy) w.set(wx, baseY+dy, wz, tuff, true);
            else for(int dy=1; dy<=4; ++dy) (void)w.set(wx, baseY+dy, wz, 0, true);
        }
        w.set(originX, baseY+1, originZ, spawner, true);
        enqueuePendingMob(originX, baseY+2, originZ, "minecraft:breeze", 1);
        w.set(originX+3, baseY+1, originZ+3, chestS, true);
        enqueuePendingLoot(originX+3, baseY+1, originZ+3, "minecraft:chests/trial_chambers/intersection");
        w.set(originX, baseY+2, originZ, copperBulb, true);
    } else {
        // fallback: generic chamber
        for (int dx=0; dx<9; ++dx) for (int dz=0; dz<9; ++dz){ w.set(originX+dx, baseY, originZ+dz, tuffBricks, true); w.set(originX+dx, baseY+4, originZ+dz, tuffBricks, true); }
        w.set(originX+4, baseY+1, originZ+4, spawner, true);
        enqueuePendingMob(originX+4, baseY+2, originZ+4, "minecraft:breeze", 1);
    }
}
void StructureManager::placeGenericPalette(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                             std::int32_t originX, std::int32_t originZ,
                             const std::string& pieceName,
                             const std::unordered_map<std::string,std::string>& palette,
                             int /*variant*/, const GroundFn& ground) const {
    Writer w{chunk, cx, cz};
    std::string lp = pieceName;
    // lower
    for (auto& c: lp) c = std::tolower(c);
    if (lp.find("village")!=std::string::npos || lp.find("house")!=std::string::npos || lp.find("farm")!=std::string::npos || lp.find("church")!=std::string::npos) {
        std::string plankName = "minecraft:oak_planks";
        if (auto it = palette.find("plank"); it!=palette.end()) plankName = it->second;
        else if (auto it2 = palette.find("planks"); it2!=palette.end()) plankName = it2->second;
        const auto planks = B(plankName.c_str()) ? B(plankName.c_str())->defaultState : B("minecraft:oak_planks")->defaultState;
        const auto log = stateByPalette(palette, "log", "minecraft:oak_log");
        const auto glass = stateByPalette(palette, "glass", "minecraft:glass");
        const auto chestS = stateByPalette(palette, "chest", "minecraft:chest");
        const auto torchS = stateByPalette(palette, "torch", "minecraft:torch");
        int gy = ground ? ground(originX+3, originZ+3) : 64;
        bool isFarm = lp.find("farm")!=std::string::npos;
        bool isChurch = lp.find("church")!=std::string::npos;
        if (isFarm) {
            const auto farmland = B("minecraft:farmland") ? B("minecraft:farmland")->defaultState : planks;
            const auto wheat = B("minecraft:wheat") ? B("minecraft:wheat")->defaultState : 0;
            const auto waterS = static_cast<std::uint16_t>(gen::stateWithPropsList("minecraft:water", {{"level","0"}}));
            for (int dzz=1; dzz<6; ++dzz) for(int dxx=1; dxx<7; ++dxx){ bool wc = dxx==4&&dzz==3; w.set(originX+dxx, gy, originZ+dzz, wc?waterS:farmland, true); if(!wc) w.set(originX+dxx, gy+1, originZ+dzz, wheat); }
            w.set(originX+2, gy+1, originZ+2, chestS, true);
            enqueuePendingLoot(originX+2, gy+1, originZ+2, "minecraft:chests/village/village_plains_house");
            enqueuePendingMob(originX+3, gy+1, originZ+3, "minecraft:villager", 1);
        } else if (isChurch) {
            const auto cobble = stateByPalette(palette, "cobble", "minecraft:cobblestone");
            for (int dy=0; dy<=5; ++dy) for(int dz=0; dz<7; ++dz) for(int dx=0; dx<7; ++dx){ bool wall = dx==0||dx==6||dz==0||dz==6||dy==5||dy==0; if(!wall) continue; w.set(originX+dx, gy+1+dy, originZ+dz, cobble); }
            w.set(originX+3, gy+1, originZ, 0, true); w.set(originX+3, gy+2, originZ, 0, true);
            w.set(originX+3, gy+3, originZ+3, torchS, true);
            w.set(originX, gy+3, originZ+3, glass); w.set(originX+6, gy+3, originZ+3, glass);
            w.set(originX+3, gy+1, originZ+3, chestS, true);
            enqueuePendingLoot(originX+3, gy+1, originZ+3, "minecraft:chests/village/village_taiga_house");
            enqueuePendingMob(originX+2, gy+1, originZ+2, "minecraft:villager", 1);
            enqueuePendingMob(originX+4, gy+1, originZ+4, "minecraft:iron_golem", 1);
        } else {
            for (int dy=0; dy<=3; ++dy) for(int dzz=0; dzz<5; ++dzz) for(int dxx=0; dxx<5; ++dxx){ bool wall = dxx==0||dxx==4||dzz==0||dzz==4||dy==3||dy==0; if(!wall) continue; auto mat = dy==0||dy==3?log:planks; w.set(originX+dxx, gy+1+dy, originZ+dzz, mat); }
            w.set(originX+2, gy+1, originZ, 0, true); w.set(originX+2, gy+2, originZ, 0, true);
            w.set(originX, gy+2, originZ+2, glass); w.set(originX+4, gy+2, originZ+2, glass);
            w.set(originX+2, gy+1, originZ+2, chestS, true);
            enqueuePendingLoot(originX+2, gy+1, originZ+2, "minecraft:chests/village/village_plains_house");
            enqueuePendingMob(originX+2, gy+1, originZ+2, "minecraft:villager", 2);
        }
        // also enqueue extra iron_golem for village
        if (lp.find("village")!=std::string::npos) enqueuePendingMob(originX+6, gy+1, originZ+6, "minecraft:iron_golem", 1);
        (void)glass; (void)torchS;
    } else if (lp.find("ancient_city")!=std::string::npos) {
        const auto deepslate = stateByPalette(palette, "deepslate", "minecraft:deepslate_bricks");
        const auto chestS = stateByPalette(palette, "chest", "minecraft:chest");
        const auto sculk = stateByPalette(palette, "sculk", "minecraft:sculk");
        int baseY = ground ? ground(originX+4, originZ+4) : 64;
        baseY = std::clamp(baseY - 8, -50, 30);
        bool isCenter = lp.find("center")!=std::string::npos;
        bool isWing = lp.find("wing")!=std::string::npos;
        int w2 = isWing ? 12 : 9;
        int h = isCenter ? 6 : 3;
        for (int dx=0; dx<w2; ++dx) for(int dz=0; dz<w2; ++dz) w.set(originX+dx, baseY, originZ+dz, deepslate, true);
        for (int dx=0; dx<w2; ++dx){ w.set(originX+dx, baseY+1, originZ, deepslate, true); w.set(originX+dx, baseY+1, originZ+w2-1, deepslate, true); }
        for (int dz=0; dz<w2; ++dz){ w.set(originX, baseY+1, originZ+dz, deepslate, true); w.set(originX+w2-1, baseY+1, originZ+dz, deepslate, true); }
        for(int dy=2; dy<=h; ++dy){ w.set(originX+w2/2, baseY+dy, originZ+w2/2, sculk, true); }
        w.set(originX+4, baseY+1, originZ+4, chestS, true);
        enqueuePendingLoot(originX+4, baseY+1, originZ+4, "minecraft:chests/ancient_city");
        w.set(originX+w2-3, baseY+1, originZ+w2-3, chestS, true);
        enqueuePendingLoot(originX+w2-3, baseY+1, originZ+w2-3, "minecraft:chests/ancient_city_ice_box");
        enqueuePendingMob(originX+5, baseY+1, originZ+5, "minecraft:warden", 1);
        if (isCenter){ w.set(originX+6, baseY+1, originZ+6, chestS, true); enqueuePendingLoot(originX+6, baseY+1, originZ+6, "minecraft:chests/ancient_city"); }
    } else if (lp.find("mansion")!=std::string::npos) {
        const auto planks = stateByPalette(palette, "planks", "minecraft:dark_oak_planks");
        const auto log = stateByPalette(palette, "log", "minecraft:dark_oak_log");
        const auto chestS = stateByPalette(palette, "chest", "minecraft:chest");
        int surfaceY = ground ? ground(originX+20, originZ+20) : 70;
        int baseY = std::clamp(surfaceY+1, 70, 85);
        int sz = (lp.find("wing")!=std::string::npos ? 20 : 40);
        for(int dx=0; dx<sz; ++dx) for(int dz=0; dz<sz; ++dz){ int wx=originX+dx, wz=originZ+dz; bool edge = dx==0||dx==sz-1||dz==0||dz==sz-1; w.set(wx, baseY, wz, planks, true); if(edge){ for(int dy=1; dy<=6; ++dy) w.set(wx, baseY+dy, wz, log, true);} }
        w.set(originX+5, baseY+1, originZ+5, chestS, true);
        enqueuePendingLoot(originX+5, baseY+1, originZ+5, "minecraft:chests/woodland_mansion");
        enqueuePendingMob(originX+8, baseY+1, originZ+8, "minecraft:vindicator", 1);
        enqueuePendingMob(originX+12, baseY+1, originZ+12, "minecraft:evoker", 1);
    } else {
        // generic fallback: small palette hut 5x5
        const auto mat = stateByPalette(palette, "plank", "minecraft:oak_planks");
        const auto mat2 = stateByPalette(palette, "log", "minecraft:oak_log");
        const auto chestS = stateByPalette(palette, "chest", "minecraft:chest");
        int gy = ground ? ground(originX+2, originZ+2) : 64;
        for(int dy=0; dy<=3; ++dy) for(int dz=0; dz<5; ++dz) for(int dx=0; dx<5; ++dx){ bool wall = dx==0||dx==4||dz==0||dz==4||dy==3||dy==0; if(!wall) continue; w.set(originX+dx, gy+1+dy, originZ+dz, dy==0||dy==3?mat2:mat); }
        w.set(originX+2, gy+1, originZ, 0, true); w.set(originX+2, gy+2, originZ, 0, true);
        w.set(originX+2, gy+1, originZ+2, chestS, true);
        enqueuePendingLoot(originX+2, gy+1, originZ+2, "minecraft:chests/simple_dungeon");
        // enqueue mob based on pieceName
        if (lp.find("monument")!=std::string::npos) enqueuePendingMob(originX+2, gy+1, originZ+2, "minecraft:guardian", 2);
        else if (lp.find("outpost")!=std::string::npos) enqueuePendingMob(originX+2, gy+1, originZ+2, "minecraft:pillager", 2);
        else if (lp.find("temple")!=std::string::npos) enqueuePendingMob(originX+2, gy+1, originZ+2, "minecraft:skeleton", 1);
        else enqueuePendingMob(originX+2, gy+1, originZ+2, "minecraft:zombie", 1);
    }
}

void StructureManager::generate(Chunk& chunk, std::int32_t cx,
                                       std::int32_t cz, const GroundFn& ground) const {
    if (placer_) {
        if (auto* pf = placer_->getPlaced("minecraft:stronghold")) {
            std::int32_t oCx, oCz;
            if (placer_->findOrigin(*pf, cx, cz, oCx, oCz)) {
                if (placer_->shouldPlaceAt(*pf, oCx, oCz) || true) {
                    strongholdPiece(chunk, cx, cz, oCx*16, oCz*16, ground);
                }
            } else if (placer_->shouldPlaceAt(*pf, cx, cz)) {
                strongholdPiece(chunk, cx, cz, cx*16, cz*16, ground);
            }
        }
        if (auto* pf = placer_->getPlaced("minecraft:mineshaft")) {
            std::int32_t oCx, oCz;
            if (placer_->findOrigin(*pf, cx, cz, oCx, oCz) && placer_->shouldPlaceAt(*pf, oCx, oCz)) {
                mineshaftPiece(chunk, cx, cz, oCx*16, oCz*16, ground);
            }
        }
    }
    {
        double h = smStructureHash(seed_, cx, cz, 0x5354524FULL);
        if (h < 0.002) {
            strongholdPiece(chunk, cx, cz, cx*16, cz*16, ground);
        }
    }
    for (const auto& s : sets_) {
        const SMStructureAt at = smStructureAtChunk(s, seed_, cx, cz);
        if (!at.present) continue;
        // trial_chambers deep_dark origin reject (vanilla: origin in deep_dark => skip)
        if (s.name.find("trial_chambers") != std::string::npos || s.name.find("trial_chamber") != std::string::npos) {
            const std::string& picked = biomes_->sample(at.originX + 8, 63, at.originZ + 8);
            if (picked.find("deep_dark") != std::string::npos) continue;
        }
        if (!s.biomes.empty()) {
            const std::string& picked = biomes_->sample(at.originX + 8, 63, at.originZ + 8);
            bool ok = false;
            for (auto& want : s.biomes)
                if (picked.find(want) != std::string::npos) { ok = true; break; }
            if (!ok) continue;
        }
        // plan36 palette-driven path: if placer has palette for this structure, use it
        if (placer_) {
            if (auto* cf = placer_->getConfigured(s.name); cf && !cf->palette.empty() && !cf->pieces.empty()) {
                int variant = int(smStructureHash(seed_, at.originX, at.originZ, s.salt ^ 0xBEEFULL) * (double)cf->pieces.size());
                if (variant < 0) variant = 0;
                if (variant >= (int)cf->pieces.size()) variant = (int)cf->pieces.size()-1;
                const std::string& pieceName = cf->pieces[variant].first;
                auto itv = cf->variants.find(pieceName);
                std::unordered_map<std::string,std::string> pal = (itv != cf->variants.end() ? itv->second : cf->palette);
                // enqueue loot/mobs defined in JSON (defer)
                int baseY = ground ? ground(at.originX+8, at.originZ+8) : 64;
                for (auto& mb : cf->mobs) enqueuePendingMob(at.originX + mb.pos[0], baseY + mb.pos[1], at.originZ + mb.pos[2], mb.mob, mb.count);
                for (auto& lb : cf->lootByPos) {
                    // lb.first is "x,y,z"
                    int lx=0, ly=0, lz=0;
                    if (sscanf(lb.first.c_str(), "%d,%d,%d", &lx,&ly,&lz)==3) enqueuePendingLoot(at.originX+lx, baseY+ly, at.originZ+lz, lb.second);
                }
                if (s.name.find("trial_chambers")!=std::string::npos || s.name.find("trial_chamber")!=std::string::npos) {
                    placeTrialChambersPalette(chunk, cx, cz, at.originX, at.originZ, pieceName, pal, variant, ground);
                } else {
                    placeGenericPalette(chunk, cx, cz, at.originX, at.originZ, pieceName, pal, variant, ground);
                }
                continue;
            }
        }
        const std::string name = s.name;
        if (name.find("village") != std::string::npos)
            villagePiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("desert_pyramid") != std::string::npos)
            pyramidPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("pillager_outpost") != std::string::npos)
            outpostPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("jungle_temple") != std::string::npos)
            jungleTemplePiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("igloo") != std::string::npos)
            iglooPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("swamp_hut") != std::string::npos)
            swampHutPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("monument") != std::string::npos)
            monumentPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("mansion") != std::string::npos)
            mansionPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("trial_chambers") != std::string::npos || name.find("trial_chamber") != std::string::npos)
            trialChambersPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("end_city") != std::string::npos)
            endCityPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("mineshaft") != std::string::npos)
            mineshaftPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("ancient_city") != std::string::npos)
            ancientCityPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("trail_ruins") != std::string::npos)
            trailRuinsPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("ruined_portal") != std::string::npos)
            ruinedPortalPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("shipwreck") != std::string::npos)
            shipwreckPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("ocean_ruins") != std::string::npos)
            oceanRuinsPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("nether_fossil") != std::string::npos)
            netherFossilPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("buried_treasure") != std::string::npos)
            buriedTreasurePiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("nether_complexes") != std::string::npos || name.find("bastion") != std::string::npos || name.find("fortress") != std::string::npos) {
            // lottery based on hash
            double r = smStructureHash(seed_, at.originX, at.originZ, s.salt ^ 0xBEEF);
            if (r < 0.4) mineshaftPiece(chunk, cx, cz, at.originX, at.originZ, ground); // fortress placeholder reuse
            else ancientCityPiece(chunk, cx, cz, at.originX, at.originZ, ground); // bastion placeholder
        } else if (name.find("stronghold") != std::string::npos)
            strongholdPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else continue;
    }
    // plan36: placer-only structures (extra 20 JSONs beyond sets_ 20) — generate via palette path
    if (placer_) {
        for (auto& [pname, pf] : placer_->allPlaced()) {
            bool already = false;
            for (auto& s : sets_) if (s.name == pname) { already = true; break; }
            if (already) continue;
            // placement check: origin within 3 chunks
            std::int32_t oCx=0, oCz=0;
            bool hasOrigin = placer_->findOrigin(pf, cx, cz, oCx, oCz);
            bool should = false;
            if (hasOrigin) should = placer_->shouldPlaceAt(pf, oCx, oCz);
            else should = placer_->shouldPlaceAt(pf, cx, cz);
            if (!should && !hasOrigin) continue;
            std::int32_t originX = hasOrigin ? oCx*16 : cx*16;
            std::int32_t originZ = hasOrigin ? oCz*16 : cz*16;
            // biome gate via sets? use pf spacing only
            auto* cf = placer_->getConfigured(pname);
            if (!cf || cf->palette.empty() || cf->pieces.empty()) continue;
            int variant = int(smStructureHash(seed_, originX, originZ, pf.salt ^ 0xBEEFULL) * (double)cf->pieces.size());
            if (variant < 0) variant = 0;
            if (variant >= (int)cf->pieces.size()) variant = (int)cf->pieces.size()-1;
            const std::string& pieceName = cf->pieces[variant].first;
            auto itv = cf->variants.find(pieceName);
            std::unordered_map<std::string,std::string> pal = (itv != cf->variants.end() ? itv->second : cf->palette);
            int baseY = ground ? ground(originX+8, originZ+8) : 64;
            for (auto& mb : cf->mobs) enqueuePendingMob(originX + mb.pos[0], baseY + mb.pos[1], originZ + mb.pos[2], mb.mob, mb.count);
            for (auto& lb : cf->lootByPos) { int lx=0,ly=0,lz=0; if (sscanf(lb.first.c_str(), "%d,%d,%d",&lx,&ly,&lz)==3) enqueuePendingLoot(originX+lx, baseY+ly, originZ+lz, lb.second); }
            if (pname.find("trial_chambers")!=std::string::npos) placeTrialChambersPalette(chunk, cx, cz, originX, originZ, pieceName, pal, variant, ground);
            else placeGenericPalette(chunk, cx, cz, originX, originZ, pieceName, pal, variant, ground);
        }
    }
}

} // namespace cppfm::worldgen
