// BlockTickScheduler implementation (items 12-15).
#include "BlockTickScheduler.hpp"
#include "../game/GameServer.hpp"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace cppfm {

// RandomTickScheduler (plan7): multiset queue sorted by dueTick
void RandomTickScheduler::scheduleRandomTick(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t delay) {
    // delay treated as absolute dueTick if no current time; schedule for delay ticks from 0
    queue_.insert({x, y, z, delay});
}
void RandomTickScheduler::scheduleRandomTick(std::int32_t x, std::int32_t y, std::int32_t z, std::int64_t delay, std::int64_t now) {
    queue_.insert({x, y, z, now + delay});
}
void RandomTickScheduler::tick(std::int64_t now) {
    while (!queue_.empty()) {
        auto it = queue_.begin();
        if (it->dueTick > now) break;
        RandomTickEntry e = *it;
        queue_.erase(it);
        const std::uint16_t st = world_.getBlock(e.x, e.y, e.z);
        if (st == 0) continue;
        const gen::BlockDef* d = gen::blockByState(st);
        if (!d) continue;
        // lookup behavior via BlockTickScheduler if available, else direct
        IBlockBehavior* beh = nullptr;
        if (srv_) {
            if (auto* bts = srv_->blockTicks()) beh = bts->behaviorFor(std::string(d->name));
        } else {
            // fallback: try to find via world? no op
        }
        if (beh) {
            // simulation distance cull
            if (srv_ && !srv_->isChunkInSimulationDistance(e.x >> 4, e.z >> 4)) continue;
            beh->randomTick(world_, e.x, e.y, e.z, st, now, srv_);
        }
    }
}

void BlockTickScheduler::schedule(std::int32_t x, std::int32_t y, std::int32_t z,
                                  std::int64_t dueTick) {
    const std::int64_t k = posKey3(x,y,z);
    if (pendingPos_.count(k)) return;
    pendingPos_.insert(k);
    queue_.push({x,y,z,dueTick});
}

void BlockTickScheduler::tick(std::int64_t now) {
    // integrate RandomTickScheduler (plan7): process multiset queue of explicit random ticks
    randomScheduler_.tick(now);
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
                    if (!world_.isChunkInSimulationDistance(cx, cz)) continue;
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
        if (!world_.isPositionInSimulationDistance(t.x, t.z)) continue;
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

bool FireBehavior::isFlammable(const std::string& blockName) const {
    if (blockName.find("planks") != std::string::npos) return true;
    if (blockName.find("_log") != std::string::npos) return true;
    if (blockName.find("leaves") != std::string::npos) return true;
    if (blockName.find("wool") != std::string::npos) return true;
    if (blockName == "minecraft:hay_block") return true;
    return false;
}

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
    // spread to flammable neighbors using virtual isFlammable (plan9 #16)
    for (int dx=-1; dx<=1; ++dx) for (int dy=-1; dy<=1; ++dy) for (int dz=-1; dz<=1; ++dz){
        if (dx==0&&dy==0&&dz==0) continue;
        const std::uint16_t ns = w.getBlock(x+dx,y+dy,z+dz);
        if (ns==0 && (rand()%100) < getSpreadChance()) {
            bool adjFlam = false;
            for (int ddx=-1; ddx<=1 && !adjFlam; ++ddx) for (int ddy=-1; ddy<=1 && !adjFlam; ++ddy) for (int ddz=-1; ddz<=1 && !adjFlam; ++ddz){
                const std::uint16_t as = w.getBlock(x+dx+ddx, y+dy+ddy, z+dz+ddz);
                const gen::BlockDef* ad = gen::blockByState(as);
                if (ad && isFlammable(std::string(ad->name))) adjFlam = true;
            }
            if (adjFlam && w.getBlock(x+dx,y+dy,z+dz)==0) {
                const auto fire = gen::blockNameToState().find("minecraft:fire");
                if (fire != gen::blockNameToState().end()) {
                    // create fire with correct directional props for its new location
                    const gen::BlockDef* fd=gen::blockByState(fire->second);
                    if (fd && fd->propCount>=6) {
                        // compute direction bools for new fire position
                        auto isFlamAt=[&](int ax,int ay,int az)->bool{
                            const gen::BlockDef* ad=gen::blockByState(w.getBlock(ax,ay,az));
                            return ad && isFlammable(std::string(ad->name));
                        };
                        bool n=isFlamAt(x+dx, y+dy, z+dz-1);
                        bool s=isFlamAt(x+dx, y+dy, z+dz+1);
                        bool e=isFlamAt(x+dx+1, y+dy, z+dz);
                        bool west=isFlamAt(x+dx-1, y+dy, z+dz);
                        bool up=isFlamAt(x+dx, y+dy+1, z+dz);
                        std::vector<std::pair<std::string_view,std::string_view>> fp;
                        // keep age 0 for new fire
                        fp.emplace_back("age","0");
                        fp.emplace_back("east", e?"true":"false");
                        fp.emplace_back("north", n?"true":"false");
                        fp.emplace_back("south", s?"true":"false");
                        fp.emplace_back("up", up?"true":"false");
                        fp.emplace_back("west", west?"true":"false");
                        // try to create state with those props, fallback to default
                        std::uint16_t ns2=static_cast<std::uint16_t>(gen::stateWithProps(*fd, fp));
                        if (gen::blockByState(ns2) && std::string(gen::blockByState(ns2)->name)=="minecraft:fire") w.setBlock(x+dx,y+dy,z+dz, ns2);
                        else w.setBlock(x+dx,y+dy,z+dz, fire->second);
                    } else w.setBlock(x+dx,y+dy,z+dz, fire->second);
                }
            }
        }
    }
    // update directional props for existing fire to reflect surrounding flammable blocks (plan9 #16)
    {
        std::uint16_t cur=w.getBlock(x,y,z);
        const gen::BlockDef* cd=gen::blockByState(cur);
        if (cd && std::string(cd->name)=="minecraft:fire" && cd->propCount>=6) {
            auto isFlamAt=[&](int ax,int ay,int az)->bool{
                const gen::BlockDef* ad=gen::blockByState(w.getBlock(ax,ay,az));
                return ad && isFlammable(std::string(ad->name));
            };
            bool n=isFlamAt(x, y, z-1);
            bool s=isFlamAt(x, y, z+1);
            bool e=isFlamAt(x+1, y, z);
            bool west=isFlamAt(x-1, y, z);
            bool up=isFlamAt(x, y+1, z);
            // also consider if fire has age prop, keep it
            int curAge=0;
            for (auto& [k,v]: gen::propsOf(cur)) if (k=="age") curAge=std::atoi(std::string(v).c_str());
            std::vector<std::pair<std::string_view,std::string_view>> np;
            np.emplace_back("age", std::to_string(curAge));
            np.emplace_back("east", e?"true":"false");
            np.emplace_back("north", n?"true":"false");
            np.emplace_back("south", s?"true":"false");
            np.emplace_back("up", up?"true":"false");
            np.emplace_back("west", west?"true":"false");
            std::uint16_t ns2=static_cast<std::uint16_t>(gen::stateWithProps(*cd, np));
            if (ns2!=cur && gen::blockByState(ns2) && std::string(gen::blockByState(ns2)->name)=="minecraft:fire") w.setBlock(x,y,z, ns2);
        }
    }
    if (age >= 15 && (rand()%100)<30) w.setBlock(x,y,z, 0);
}

// -------------------------------------------------------- PortalAge (plan6 §3)

void PortalAgeBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                             std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now; (void)srv;
    int age = 0;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="age") age = std::atoi(std::string(v).c_str());
    const gen::BlockDef* d = gen::blockByState(state);
    if (!d) return;
    // Random decay: 10% chance to age, 5% chance to extinguish at max
    if (age < 15) {
        if ((rand() % 100) < 10) {
            std::vector<std::pair<std::string_view,std::string_view>> props;
            for (auto& [k,v] : gen::propsOf(state)) if (k!="age") props.emplace_back(k,v);
            std::string ns = std::to_string(age+1);
            props.emplace_back("age", ns);
            std::uint16_t nsState = static_cast<std::uint16_t>(gen::stateWithProps(*d, props));
            w.setBlock(x,y,z, nsState);
        }
    } else {
        // age 15: 2% chance to decay (become air) if not near obsidian frame
        if ((rand() % 100) < 2) {
            // check for nearby obsidian within 2 blocks
            bool nearFrame = false;
            for (int dx=-2; dx<=2 && !nearFrame; ++dx) for (int dy=-2; dy<=2 && !nearFrame; ++dy) for (int dz=-2; dz<=2 && !nearFrame; ++dz){
                if (dx==0&&dy==0&&dz==0) continue;
                auto s2 = w.getBlock(x+dx, y+dy, z+dz);
                auto* bd = gen::blockByState(s2);
                if (bd && std::string(bd->name)=="minecraft:obsidian") nearFrame = true;
            }
            if (!nearFrame) w.setBlock(x,y,z, 0);
        }
    }
}

bool SoulFireBehavior::isFlammable(const std::string& blockName) const {
    // soul_fire only on soul_sand / soul_soil adjacency
    if (blockName == "minecraft:soul_sand" || blockName == "minecraft:soul_soil") return true;
    return false;
}
void SoulFireBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                            std::uint16_t state, std::int64_t now, GameServer* srv) {
    // soul_fire does not spread like normal fire, only stays on soul sand
    if (srv) {
        auto* gr = &srv->gameRules();
        if (gr && !gr->getBool("doFireTick")) return;
    }
    const std::uint16_t below = w.getBlock(x, y-1, z);
    const gen::BlockDef* bd = gen::blockByState(below);
    if (!bd || (std::string(bd->name) != "minecraft:soul_sand" && std::string(bd->name) != "minecraft:soul_soil")) {
        w.setBlock(x,y,z, 0);
        return;
    }
    FireBehavior::tick(w, x, y, z, state, now, srv);
}

bool CampfireBehavior::isFlammable(const std::string& blockName) const {
    // campfire only with lit state; flammable check similar to fire but only when lit
    if (blockName.find("planks") != std::string::npos) return true;
    if (blockName.find("log") != std::string::npos) return true;
    return false;
}
void CampfireBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                            std::uint16_t state, std::int64_t now, GameServer* srv) {
    bool lit = false;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="lit" && v=="true") lit = true;
    if (!lit) return;
    // campfire signal_fire if hay bale below, waterlogged check
    bool waterlogged=false;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="waterlogged" && v=="true") waterlogged=true;
    if (waterlogged) return;
    FireBehavior::tick(w, x, y, z, state, now, srv);
    // spread chance lower than fire, also emit smoke particle via fire behavior already
}

// -------------------------------------------------------- Cocoa (plan9 #13)
void CocoaBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                         std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now; (void)srv;
    int age=0;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="age") age=std::atoi(std::string(v).c_str());
    if (age>=2) return;
    if ((rand()%100) >= 20) return; // 20% per random tick
    const gen::BlockDef* d=gen::blockByState(state);
    if (!d) return;
    // check support: jungle log facing direction
    std::string facing="north";
    for (auto& [k,v] : gen::propsOf(state)) if (k=="facing") facing=std::string(v);
    int dx=0,dz=0;
    if (facing=="north") dz=1; else if (facing=="south") dz=-1; else if (facing=="west") dx=1; else if (facing=="east") dx=-1;
    std::uint16_t support=w.getBlock(x+dx,y,z+dz);
    const gen::BlockDef* sd=gen::blockByState(support);
    if (!sd || std::string(sd->name).find("jungle_log")==std::string::npos) return;
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for (auto& [k,v] : gen::propsOf(state)) if (k!="age") props.emplace_back(k,v);
    std::string ns=std::to_string(age+1);
    props.emplace_back("age", ns);
    w.setBlock(x,y,z, static_cast<std::uint16_t>(gen::stateWithProps(*d, props)));
}
bool CocoaBehavior::fertilize(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                              std::uint16_t state, GameServer* srv) {
    int age=0; for (auto& [k,v] : gen::propsOf(state)) if (k=="age") age=std::atoi(std::string(v).c_str());
    if (age>=2) return false;
    const gen::BlockDef* d=gen::blockByState(state);
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for (auto& [k,v] : gen::propsOf(state)) if (k!="age") props.emplace_back(k,v);
    props.emplace_back("age", "2");
    w.setBlock(x,y,z, static_cast<std::uint16_t>(gen::stateWithProps(*d, props)));
    (void)srv; return true;
}

// -------------------------------------------------------- Sweet Berry
void SweetBerryBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                              std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now; (void)srv;
    int age=0; for (auto& [k,v] : gen::propsOf(state)) if (k=="age") age=std::atoi(std::string(v).c_str());
    if (age>=3) return;
    if ((rand()%100) >= 20) return;
    const gen::BlockDef* d=gen::blockByState(state);
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for (auto& [k,v] : gen::propsOf(state)) if (k!="age") props.emplace_back(k,v);
    props.emplace_back("age", std::to_string(age+1));
    w.setBlock(x,y,z, static_cast<std::uint16_t>(gen::stateWithProps(*d, props)));
}
bool SweetBerryBehavior::fertilize(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                                   std::uint16_t state, GameServer* srv) {
    int age=0; for (auto& [k,v] : gen::propsOf(state)) if (k=="age") age=std::atoi(std::string(v).c_str());
    if (age>=3) return false;
    const gen::BlockDef* d=gen::blockByState(state);
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for (auto& [k,v] : gen::propsOf(state)) if (k!="age") props.emplace_back(k,v);
    props.emplace_back("age", "3");
    w.setBlock(x,y,z, static_cast<std::uint16_t>(gen::stateWithProps(*d, props)));
    (void)srv; return true;
}

// -------------------------------------------------------- Nether Wart
void NetherWartBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                              std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now; (void)srv;
    int age=0; for (auto& [k,v] : gen::propsOf(state)) if (k=="age") age=std::atoi(std::string(v).c_str());
    if (age>=3) return;
    if ((rand()%100) >= 10) return; // slower 10%
    const std::uint16_t below=w.getBlock(x,y-1,z);
    const gen::BlockDef* bd=gen::blockByState(below);
    if (!bd || std::string(bd->name)!="minecraft:soul_sand") return;
    const gen::BlockDef* d=gen::blockByState(state);
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for (auto& [k,v] : gen::propsOf(state)) if (k!="age") props.emplace_back(k,v);
    props.emplace_back("age", std::to_string(age+1));
    w.setBlock(x,y,z, static_cast<std::uint16_t>(gen::stateWithProps(*d, props)));
}
bool NetherWartBehavior::fertilize(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                                   std::uint16_t state, GameServer* srv) {
    (void)w; (void)x; (void)y; (void)z; (void)state; (void)srv; return false; // bonemeal does not work on nether wart
}

// -------------------------------------------------------- Chorus Flower (plan9 #13)
void ChorusFlowerBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                                std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now; (void)srv;
    int age=0; for (auto& [k,v] : gen::propsOf(state)) if (k=="age") age=std::atoi(std::string(v).c_str());
    if (age>=5) { w.setBlock(x,y,z, 0); // death -> chorus plant
        auto plantIt=gen::blockNameToState().find("minecraft:chorus_plant");
        if (plantIt!=gen::blockNameToState().end()) w.setBlock(x,y,z, plantIt->second);
        return;
    }
    if ((rand()%100) >= 5) return;
    // try grow upward if air above and end_stone or chorus_plant below support chain
    if (w.getBlock(x,y+1,z)!=0) return;
    // check if can survive: if air around and support below is chorus_plant or end_stone
    const std::uint16_t below=w.getBlock(x,y-1,z);
    const gen::BlockDef* bd=gen::blockByState(below);
    bool support=false;
    if (bd) {
        std::string bn(bd->name);
        if (bn=="minecraft:end_stone"||bn=="minecraft:chorus_plant"||bn.find("chorus")!=std::string::npos) support=true;
    }
    if (!support) return;
    const gen::BlockDef* d=gen::blockByState(state);
    // grow upward, increment age
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for (auto& [k,v] : gen::propsOf(state)) if (k!="age") props.emplace_back(k,v);
    props.emplace_back("age", std::to_string(age+1));
    w.setBlock(x,y+1,z, static_cast<std::uint16_t>(gen::stateWithProps(*d, props)));
    // original becomes chorus_plant
    auto plantIt=gen::blockNameToState().find("minecraft:chorus_plant");
    if (plantIt!=gen::blockNameToState().end()) w.setBlock(x,y,z, plantIt->second);
}

} // namespace cppfm
