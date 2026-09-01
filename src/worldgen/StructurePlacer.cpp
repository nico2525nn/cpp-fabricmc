// StructurePlacer implementation: JSON-driven with fallback.
#include "StructurePlacer.hpp"
#include "../generated/BlockStates.hpp"
#include <filesystem>
#include <fstream>
#include <cmath>

namespace cppfm::worldgen {

double StructurePlacer::hash01(std::uint64_t seed, std::int64_t gx, std::int64_t gz, std::uint64_t salt) {
    std::uint64_t h = seed ^ salt;
    h ^= static_cast<std::uint64_t>(gx) * 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<std::uint64_t>(gz) * 0xC2B2AE3D27D4EB4FULL;
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDULL; h ^= h >> 33;
    return (h >> 11) / double(1ULL << 53);
}

void StructurePlacer::ensureDefaults() {
    if (configured_.count("minecraft:stronghold")==0) {
        ConfiguredFeature cf;
        cf.name = "minecraft:stronghold";
        cf.type = "minecraft:stronghold";
        cf.pieces = {{"minecraft:stronghold/room_crossing",10},{"minecraft:stronghold/corridor",8}};
        configured_.emplace(cf.name, std::move(cf));
        PlacedFeature pf;
        pf.name = "minecraft:stronghold";
        pf.featureName = "minecraft:stronghold";
        pf.spacing = 32; pf.separation = 8; pf.salt = 14357617; pf.frequency = 1.0;
        placed_.emplace(pf.name, std::move(pf));
    }
    if (configured_.count("minecraft:mineshaft")==0) {
        ConfiguredFeature cf;
        cf.name = "minecraft:mineshaft";
        cf.type = "minecraft:mineshaft";
        cf.pieces = {{"minecraft:mineshaft/corridor",12}};
        configured_.emplace(cf.name, std::move(cf));
        PlacedFeature pf;
        pf.name = "minecraft:mineshaft";
        pf.featureName = "minecraft:mineshaft";
        pf.spacing = 32; pf.separation = 8; pf.salt = 123456; pf.frequency = 0.8;
        placed_.emplace(pf.name, std::move(pf));
    }
    if (configured_.count("minecraft:village")==0) {
        ConfiguredFeature cf;
        cf.name = "minecraft:village";
        cf.type = "minecraft:village";
        cf.pieces = {{"house",10},{"farm",6},{"church",4},{"lamp",3}};
        configured_.emplace(cf.name, std::move(cf));
        PlacedFeature pf;
        pf.name = "minecraft:village";
        pf.featureName = "minecraft:village";
        pf.spacing = 34; pf.separation = 8; pf.salt = 0x5A17C; pf.frequency = 1.0;
        placed_.emplace(pf.name, std::move(pf));
    }
}

int StructurePlacer::load(const std::string& baseDir) {
    int loaded = 0;
    try {
        if (!std::filesystem::exists(baseDir)) { ensureDefaults(); return 0; }
        for (auto& entry : std::filesystem::directory_iterator(baseDir)) {
            if (!entry.is_regular_file()) continue;
            auto p = entry.path();
            if (p.extension() != ".json") continue;
            std::ifstream f(p);
            if (!f) continue;
            std::string txt((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            json::Value v;
            try { v = json::Value::parse(txt); } catch (...) { continue; }
            ConfiguredFeature cf;
            if (auto* n = v.find("name")) cf.name = n->asStr();
            else cf.name = p.stem().string();
            if (cf.name.find(":")==std::string::npos) cf.name = "minecraft:" + cf.name;
            if (auto* t = v.find("type")) cf.type = t->asStr();
            else cf.type = cf.name;
            if (auto* c = v.find("config")) cf.config = *c;
            if (auto* pcs = v.find("pieces"); pcs && pcs->isArr()) {
                for (auto& e : pcs->arr) {
                    std::string pn;
                    int w = 1;
                    if (e.isObj()) {
                        if (auto* nn = e.find("name")) pn = nn->asStr();
                        if (auto* ww = e.find("weight")) w = ww->asInt(1);
                    } else if (e.isStr()) pn = e.asStr();
                    if (!pn.empty()) cf.pieces.emplace_back(pn, w);
                }
            }
            if (auto* pal = v.find("palette"); pal && pal->isObj()) {
                for (auto& [k,val] : pal->obj) if (val.isStr()) cf.palette[k]=val.asStr();
            }
            if (auto* vars = v.find("variants"); vars && vars->isObj()) {
                for (auto& [piece,val] : vars->obj) if (val.isObj()) {
                    for (auto& [k2,val2] : val.obj) if (val2.isStr()) cf.variants[piece][k2]=val2.asStr();
                }
            }
            if (auto* mobs = v.find("mobs"); mobs && mobs->isArr()) {
                for (auto& e : mobs->arr) if (e.isObj()) {
                    ConfiguredFeature::MobPlace mp{};
                    if (auto* pos = e.find("pos"); pos && pos->isArr() && pos->arr.size()>=3) {
                        mp.pos[0]=pos->arr[0].asInt(0); mp.pos[1]=pos->arr[1].asInt(0); mp.pos[2]=pos->arr[2].asInt(0);
                    }
                    if (auto* mob = e.find("mob")) mp.mob=mob->asStr();
                    else if (auto* mob2 = e.find("type")) mp.mob=mob2->asStr();
                    if (auto* c = e.find("count")) mp.count=c->asInt(1);
                    if (!mp.mob.empty()) cf.mobs.push_back(std::move(mp));
                }
            }
            if (auto* lt = v.find("loot"); lt && lt->isArr()) {
                for (auto& e : lt->arr) if (e.isObj()) {
                    std::string posStr, table;
                    if (auto* p = e.find("pos")) posStr=p->asStr();
                    if (auto* t = e.find("table")) table=t->asStr();
                    else if (auto* t2 = e.find("loot_table")) table=t2->asStr();
                    if (!posStr.empty() && !table.empty()) cf.lootByPos.emplace_back(posStr, table);
                }
            }
            // also support loot_tables map form
            if (auto* lt2 = v.find("loot_tables"); lt2 && lt2->isObj()) {
                for (auto& [k,val] : lt2->obj) if (val.isStr()) cf.lootByPos.emplace_back(k, val.asStr());
            }
            configured_.emplace(cf.name, std::move(cf));
            // placed
            PlacedFeature pf;
            pf.name = cf.name;
            pf.featureName = cf.name;
            if (auto* pl = v.find("placement"); pl && pl->isObj()) {
                if (auto* sp = pl->find("spacing")) pf.spacing = sp->asInt(pf.spacing);
                if (auto* se = pl->find("separation")) pf.separation = se->asInt(pf.separation);
                if (auto* sa = pl->find("salt")) pf.salt = (std::uint64_t)sa->asI64(pf.salt);
                if (auto* fr = pl->find("frequency")) pf.frequency = fr->number;
            }
            placed_.emplace(pf.name, std::move(pf));
            ++loaded;
        }
    } catch (...) {}
    ensureDefaults();
    return loaded;
}

const ConfiguredFeature* StructurePlacer::getConfigured(const std::string& name) const {
    auto it = configured_.find(name);
    return it==configured_.end() ? nullptr : &it->second;
}
const PlacedFeature* StructurePlacer::getPlaced(const std::string& name) const {
    auto it = placed_.find(name);
    return it==placed_.end() ? nullptr : &it->second;
}
bool StructurePlacer::shouldPlaceAt(const PlacedFeature& pf, std::int32_t cx, std::int32_t cz) const {
    // vanilla random spread check: chunk is origin if its grid cell's jitter matches
    const std::int64_t gx = std::floor(double(cx) / pf.spacing);
    const std::int64_t gz = std::floor(double(cz) / pf.spacing);
    double r1 = hash01(seed_, gx, gz, pf.salt);
    double r2 = hash01(seed_, gx, gz, pf.salt ^ 0x9E37ULL);
    std::int32_t scx = static_cast<std::int32_t>(gx * pf.spacing + r1 * (pf.spacing - pf.separation));
    std::int32_t scz = static_cast<std::int32_t>(gz * pf.spacing + r2 * (pf.spacing - pf.separation));
    return scx==cx && scz==cz && r1 < pf.frequency;
}
bool StructurePlacer::findOrigin(const PlacedFeature& pf, std::int32_t cx, std::int32_t cz,
                                 std::int32_t& outOriginCx, std::int32_t& outOriginCz) const {
    const std::int64_t gx = std::floor(double(cx) / pf.spacing);
    const std::int64_t gz = std::floor(double(cz) / pf.spacing);
    for (std::int64_t ox=-1; ox<=1; ++ox) for (std::int64_t oz=-1; oz<=1; ++oz) {
        const std::int64_t cellX = gx+ox, cellZ = gz+oz;
        double r1 = hash01(seed_, cellX, cellZ, pf.salt);
        double r2 = hash01(seed_, cellX, cellZ, pf.salt ^ 0x9E37ULL);
        std::int32_t scx = static_cast<std::int32_t>(cellX*pf.spacing + r1*(pf.spacing-pf.separation));
        std::int32_t scz = static_cast<std::int32_t>(cellZ*pf.spacing + r2*(pf.spacing-pf.separation));
        if (std::abs(scx-cx)<=3 && std::abs(scz-cz)<=3) {
            outOriginCx=scx; outOriginCz=scz; return true;
        }
    }
    return false;
}

std::uint16_t StructurePlacer::stateFor(const ConfiguredFeature& cf, const std::string& piece, const std::string& key, const std::string& fallback) const {
    auto itV = cf.variants.find(piece);
    if (itV != cf.variants.end()) {
        auto it2 = itV->second.find(key);
        if (it2 != itV->second.end()) {
            if (auto* d = gen::blockByName(it2->second.c_str())) return d->defaultState;
        }
    }
    auto it = cf.palette.find(key);
    const std::string& name = (it != cf.palette.end() ? it->second : fallback);
    if (auto* d = gen::blockByName(name.c_str())) return d->defaultState;
    if (auto* f = gen::blockByName(fallback.c_str())) return f->defaultState;
    return 0;
}

} // namespace cppfm::worldgen
