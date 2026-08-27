// BlockTickScheduler implementation (items 12-15).
#include "BlockTickScheduler.hpp"
#include "../game/GameServer.hpp"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace cppfm {

void BlockTickScheduler::schedule(std::int32_t x, std::int32_t y, std::int32_t z,
                                  std::int64_t dueTick) {
    const std::int64_t k = posKey3(x,y,z);
    if (pendingPos_.count(k)) return;
    pendingPos_.insert(k);
    queue_.push({x,y,z,dueTick});
}

void BlockTickScheduler::tick(std::int64_t now) {
    // random ticks: pick chunks (simulation-distance culled)
    if (rules_) {
        const int rts = rules_->getInt("randomTickSpeed", 3);
        if (rts > 0 && now % 5 == 0) {
            // Use World's allChunkKeys to pick random chunks (simulation-distance culled)
            auto keys = world_.allChunkKeys();
            if (!keys.empty()) {
                // filter by simulation distance if server present
                std::vector<std::int64_t> simKeys;
                simKeys.reserve(keys.size());
                for (auto k : keys) {
                    const std::int32_t cx = static_cast<std::int32_t>(k >> 32);
                    const std::int32_t cz = static_cast<std::int32_t>(k & 0xFFFFFFFFLL);
                    if (srv_ && !srv_->isChunkInSimulationDistance(cx, cz)) continue;
                    simKeys.push_back(k);
                }
                if (!simKeys.empty()) {
                    for (int i = 0; i < std::min<int>(rts, 8); ++i) {
                        const std::int64_t k = simKeys[rand() % simKeys.size()];
                        const std::int32_t cx = static_cast<std::int32_t>(k >> 32);
                        const std::int32_t cz = static_cast<std::int32_t>(k & 0xFFFFFFFFLL);
                        for (int r = 0; r < 16; ++r) {
                            const std::int32_t x = (cx << 4) + (rand() % 16);
                            const std::int32_t z = (cz << 4) + (rand() % 16);
                            const std::int32_t y = kMinY + (rand() % (kMaxY - kMinY));
                            const std::uint16_t st = world_.getBlock(x,y,z);
                            if (st == 0) continue;
                            const gen::BlockDef* d = gen::blockByState(st);
                            if (!d) continue;
                            auto* beh = behaviorFor(std::string(d->name));
                            if (beh) beh->tick(world_, x, y, z, st, now, srv_);
                        }
                    }
                }
            }
        }
    }
    // scheduled ticks (simulation-distance culled)
    while (!queue_.empty() && queue_.top().dueTick <= now) {
        ScheduledTick t = queue_.top(); queue_.pop();
        pendingPos_.erase(posKey3(t.x,t.y,t.z));
        if (srv_ && !srv_->isChunkInSimulationDistance(t.x >> 4, t.z >> 4)) continue;
        const std::uint16_t st = world_.getBlock(t.x,t.y,t.z);
        if (st == 0) continue;
        const gen::BlockDef* d = gen::blockByState(st);
        if (!d) continue;
        auto* beh = behaviorFor(std::string(d->name));
        if (beh) beh->tick(world_, t.x,t.y,t.z, st, now, srv_);
    }
}

// -------------------------------------------------------- Crop

void CropBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                        std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now; (void)srv;
    int age = 0, maxAge = 7;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="age") age = std::atoi(std::string(v).c_str());
    const gen::BlockDef* d = gen::blockByState(state);
    if (d) {
        if (std::string(d->name).find("beetroots") != std::string::npos) maxAge = 3;
        else if (std::string(d->name).find("potatoes") != std::string::npos ||
                 std::string(d->name).find("carrots") != std::string::npos) maxAge = 7;
    }
    if (age >= maxAge) return;
    // growth chance ~ 0.3 per random tick when farmland moist + light
    // simplified: always grow with 30% chance
    if ((rand() % 100) < 30) {
        const gen::BlockDef* dd = gen::blockByState(state);
        if (!dd) return;
        std::string ageStr = std::to_string(age+1);
        std::vector<std::pair<std::string_view,std::string_view>> props;
        for (auto& [k,v] : gen::propsOf(state))
            if (k != "age") props.emplace_back(k, v);
        props.emplace_back("age", ageStr);
        const std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*dd, props));
        w.setBlock(x,y,z, ns);
    }
}

bool CropBehavior::fertilize(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                             std::uint16_t state, GameServer* srv) {
    int age = 0, maxAge = 7;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="age") age = std::atoi(std::string(v).c_str());
    const gen::BlockDef* d = gen::blockByState(state);
    if (d && std::string(d->name).find("beetroots")!=std::string::npos) maxAge=3;
    if (age >= maxAge) return false;
    const gen::BlockDef* dd = gen::blockByState(state);
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for (auto& [k,v] : gen::propsOf(state)) if (k!="age") props.emplace_back(k,v);
    std::string ageStr = std::to_string(maxAge);
    props.emplace_back("age", ageStr);
    const std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*dd, props));
    w.setBlock(x,y,z, ns);
    (void)srv;
    return true;
}

// -------------------------------------------------------- Sapling

void SaplingBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                           std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now; (void)srv;
    // 5% chance per random tick to grow
    if ((rand() % 100) < 5) fertilize(w,x,y,z,state,srv);
}

bool SaplingBehavior::fertilize(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                                std::uint16_t state, GameServer* srv) {
    (void)srv;
    const gen::BlockDef* d = gen::blockByState(state);
    if (!d) return false;
    // check soil
    const std::uint16_t below = w.getBlock(x, y-1, z);
    const gen::BlockDef* bd = gen::blockByState(below);
    if (!bd || (std::string(bd->name).find("dirt")==std::string::npos &&
                std::string(bd->name)!="minecraft:grass_block")) return false;
    // grow oak tree (reuse WorldGen tree logic simplified)
    const auto logId = gen::blockNameToState().at("minecraft:oak_log");
    const auto leavesId = gen::blockNameToState().at("minecraft:oak_leaves");
    w.setBlock(x,y,z, 0);
    const int trunkH = 4 + (rand()%3);
    for (int t=0; t<trunkH; ++t) w.setBlock(x, y+t, z, logId);
    for (int dy=trunkH-2; dy<=trunkH+1; ++dy){
        int rad = dy>=trunkH ? 1 : 2;
        for(int dz=-rad; dz<=rad; ++dz) for(int dx=-rad; dx<=rad; ++dx){
            if(dx==0&&dz==0&&dy<trunkH) continue;
            if (w.getBlock(x+dx, y+dy, z+dz)==0) w.setBlock(x+dx, y+dy, z+dz, leavesId);
        }
    }
    return true;
}

// -------------------------------------------------------- Stem (bamboo/sugar_cane/cactus)

void StemBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                        std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now; (void)srv;
    if ((rand()%100) >= 20) return;
    const gen::BlockDef* d = gen::blockByState(state);
    if (!d) return;
    std::string name(d->name);
    int age = 0;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="age") age = std::atoi(std::string(v).c_str());
    // check support
    const std::uint16_t below = w.getBlock(x,y-1,z);
    if (name.find("cactus")!=std::string::npos) {
        if (gen::blockByState(below) && std::string(gen::blockByState(below)->name)!="minecraft:sand" &&
            std::string(gen::blockByState(below)->name)!="minecraft:cactus") return;
    } else if (name.find("sugar_cane")!=std::string::npos) {
        (void)below;
    }
    // count height
    int h=1;
    for (int dy=1; w.getBlock(x,y+dy,z)!=0 && h < maxH_; ++dy) ++h;
    if (h >= maxH_) return;
    if (age < 15) {
        std::vector<std::pair<std::string_view,std::string_view>> props;
        for (auto& [k,v] : gen::propsOf(state)) if(k!="age") props.emplace_back(k,v);
        std::string ns = std::to_string(age+1);
        props.emplace_back("age", ns);
        const std::uint16_t nst = static_cast<std::uint16_t>(gen::stateWithProps(*d, props));
        w.setBlock(x,y,z, nst);
    } else {
        if (w.getBlock(x,y+1,z)==0) {
            std::vector<std::pair<std::string_view,std::string_view>> props0;
            for (auto& [k,v] : gen::propsOf(state)) if(k!="age") props0.emplace_back(k,v);
            props0.emplace_back("age", "0");
            const std::uint16_t nst0 = static_cast<std::uint16_t>(gen::stateWithProps(*d, props0));
            w.setBlock(x,y,z, nst0);
            w.setBlock(x,y+1,z, static_cast<std::uint16_t>(gen::blockByState(state)->defaultState));
        }
    }
}

// -------------------------------------------------------- Farmland moisture

void FarmlandBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                            std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now; (void)srv;
    int moist = 0;
    for (auto& [k,v] : gen::propsOf(state)) if(k=="moisture") moist = std::atoi(std::string(v).c_str());
    bool hasWater = false;
    for (int dx=-4; dx<=4 && !hasWater; ++dx)
        for (int dz=-4; dz<=4 && !hasWater; ++dz)
            for (int dy=0; dy<=1 && !hasWater; ++dy)
                if (w.getBlock(x+dx,y+dy,z+dz) != 0) {
                    const gen::BlockDef* bd = gen::blockByState(w.getBlock(x+dx,y+dy,z+dz));
                    if (bd && std::string(bd->name).find("water")!=std::string::npos) hasWater = true;
                }
    int want = hasWater ? 7 : std::max(0, moist-1);
    if (want == moist) return;
    const gen::BlockDef* d = gen::blockByState(state);
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for (auto& [k,v] : gen::propsOf(state)) if(k!="moisture") props.emplace_back(k,v);
    std::string ms = std::to_string(want);
    props.emplace_back("moisture", ms);
    const std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*d, props));
    w.setBlock(x,y,z, ns);
    if (want==0) {
        // if no作物 above, revert to dirt
        if (w.getBlock(x,y+1,z)==0) {
            w.setBlock(x,y,z, gen::blockNameToState().at("minecraft:dirt"));
        }
    }
}

// -------------------------------------------------------- Fire

void FireBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                        std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now;
    if (srv) {
        auto* gr = &srv->gameRules();
        if (gr && !gr->getBool("doFireTick")) return;
    }
    int age = 0;
    for (auto& [k,v] : gen::propsOf(state)) if(k=="age") age = std::atoi(std::string(v).c_str());
    if (age < 15 && (rand()%100) < 20) {
        const gen::BlockDef* d = gen::blockByState(state);
        std::vector<std::pair<std::string_view,std::string_view>> props;
        for (auto& [k,v] : gen::propsOf(state)) if(k!="age") props.emplace_back(k,v);
        std::string ns2 = std::to_string(age+1);
        props.emplace_back("age", ns2);
        w.setBlock(x,y,z, static_cast<std::uint16_t>(gen::stateWithProps(*d, props)));
    }
    // spread to flammable neighbors
    static const char* flammable[] = {"minecraft:oak_planks","minecraft:oak_log","minecraft:oak_leaves","minecraft:wool","minecraft:hay_block"};
    for (int dx=-1; dx<=1; ++dx) for (int dy=-1; dy<=1; ++dy) for (int dz=-1; dz<=1; ++dz){
        if (dx==0&&dy==0&&dz==0) continue;
        const std::uint16_t ns = w.getBlock(x+dx,y+dy,z+dz);
        if (ns==0 && (rand()%100)<10) {
            // check if neighbor is flammable block adjacent
            for (auto* fn : flammable){
                const gen::BlockDef* fb = gen::blockByState(ns);
                if (fb && std::string(fb->name)==fn) { /* already fire */ break; }
            }
            // simple: if adjacent to flammable, ignite air
            bool adjFlam = false;
            for (int ddx=-1; ddx<=1 && !adjFlam; ++ddx) for (int ddy=-1; ddy<=1 && !adjFlam; ++ddy) for (int ddz=-1; ddz<=1 && !adjFlam; ++ddz){
                const std::uint16_t as = w.getBlock(x+dx+ddx, y+dy+ddy, z+dz+ddz);
                const gen::BlockDef* ad = gen::blockByState(as);
                if (ad) {
                    std::string an(ad->name);
                    if (an.find("planks")!=std::string::npos || an.find("log")!=std::string::npos || an.find("leaves")!=std::string::npos) adjFlam = true;
                }
            }
            if (adjFlam && w.getBlock(x+dx,y+dy,z+dz)==0) {
                const auto fire = gen::blockNameToState().find("minecraft:fire");
                if (fire != gen::blockNameToState().end()) w.setBlock(x+dx,y+dy,z+dz, fire->second);
            }
        }
    }
    if (age >= 15 && (rand()%100)<30) w.setBlock(x,y,z, 0);
}

} // namespace cppfm
