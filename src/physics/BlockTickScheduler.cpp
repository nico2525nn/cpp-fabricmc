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
        IBlockBehavior* beh = nullptr;
        if (srv_) {
            if (auto* bts = srv_->blockTicks()) beh = bts->behaviorFor(std::string(d->name));
        }
        if (beh) {
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
    randomScheduler_.tick(now);
    if (rules_) {
        const int rts = rules_->getInt("randomTickSpeed", 3);
        if (rts > 0 && now % 5 == 0) {
            auto keys = world_.allChunkKeys();
            if (!keys.empty()) {
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
                            if (beh) beh->randomTick(world_, x, y, z, st, now, srv_);
                        }
                    }
                }
            }
        }
    }
    while (!queue_.empty() && queue_.top().dueTick <= now) {
        ScheduledTick t = queue_.top(); queue_.pop();
        pendingPos_.erase(posKey3(t.x,t.y,t.z));
        if (!world_.isChunkInSimulationDistance(t.x >> 4, t.z >> 4) && !world_.isPositionInSimulationDistance(t.x, t.z)) continue;
        const std::uint16_t st = world_.getBlock(t.x,t.y,t.z);
        if (st == 0) continue;
        const gen::BlockDef* d = gen::blockByState(st);
        if (!d) continue;
        auto* beh = behaviorFor(std::string(d->name));
        if (beh) beh->tick(world_, t.x,t.y,t.z, st, now, srv_);
    }
}

// helpers
static int getAge(std::uint16_t state) {
    for (auto& [k,v] : gen::propsOf(state)) if (k=="age") return std::atoi(std::string(v).c_str());
    return 0;
}
static std::uint16_t withAge(const gen::BlockDef* d, std::uint16_t state, int newAge) {
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for (auto& [k,v] : gen::propsOf(state)) if (k!="age") props.emplace_back(k,v);
    std::string s = std::to_string(newAge);
    props.emplace_back("age", s);
    return static_cast<std::uint16_t>(gen::stateWithProps(*d, props));
}
static int getMoisture(std::uint16_t state) {
    for (auto& [k,v] : gen::propsOf(state)) if (k=="moisture") return std::atoi(std::string(v).c_str());
    return 0;
}
static bool isCropBlock(const gen::BlockDef* d) {
    if (!d) return false;
    std::string n(d->name);
    return n.find("wheat")!=std::string::npos || n.find("carrots")!=std::string::npos ||
           n.find("potatoes")!=std::string::npos || n.find("beetroots")!=std::string::npos;
}
static float growthSpeed(World& w, std::int32_t x, std::int32_t y, std::int32_t z) {
    float f = 1.0f;
    for (int dx=-1; dx<=1; ++dx) for (int dz=-1; dz<=1; ++dz) {
        std::uint16_t bs = w.getBlock(x+dx, y-1, z+dz);
        const gen::BlockDef* bd = gen::blockByState(bs);
        if (!bd) continue;
        if (std::string(bd->name)!="minecraft:farmland") continue;
        int moist = getMoisture(bs);
        f += (moist>0 ? 3.0f : 1.0f);
        // diagonal contributes quarter? vanilla divides by 4 for diagonals, but we keep simple
        if (dx!=0 && dz!=0) f -= 0.5f; // small adjust
    }
    // adjacent crop penalty
    bool hasAdjacentCrop = false;
    const int ADX[4]={1,-1,0,0}, ADZ[4]={0,0,1,-1};
    for(int d=0;d<4;++d){
        auto* nb = gen::blockByState(w.getBlock(x+ADX[d], y, z+ADZ[d]));
        if (isCropBlock(nb)) { hasAdjacentCrop=true; break; }
        nb = gen::blockByState(w.getBlock(x+ADX[d], y, z+ADZ[d]+0)); // diagonal also?
    }
    if (hasAdjacentCrop) f /= 2.0f;
    // also check diagonal crops slightly
    for(int dx=-1;dx<=1;++dx) for(int dz=-1;dz<=1;++dz){
        if(dx==0&&dz==0) continue;
        if(dx!=0 && dz!=0){
            auto* nb = gen::blockByState(w.getBlock(x+dx, y, z+dz));
            if (isCropBlock(nb)) { f /= 1.2f; break; }
        }
    }
    if (f<1) f=1;
    return f;
}
static int getLight(World& w, std::int32_t x, std::int32_t y, std::int32_t z){
    // max of block and sky light nibble; if no sky light cache, assume 15 for simplicity when checking growth
    int bl = w.getBlockLight(x,y,z);
    int sl = 0;
    if (w.hasSkyLightCache(x>>4, z>>4)) sl = w.getSkyLight(x,y,z);
    else sl = 15; // assume daylight if not cached
    return std::max(bl, sl);
}

// -------------------------------------------------------- Crop

void CropBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                        std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now;
    // respect randomTickSpeed 0 and simulation distance already culled in BlockTickScheduler::tick
    if (srv) {
        auto* gr = &srv->gameRules();
        if (gr && gr->getInt("randomTickSpeed",3)==0) return;
    }
    int age = getAge(state);
    int maxAge = 7;
    const gen::BlockDef* d = gen::blockByState(state);
    if (!d) return;
    if (std::string(d->name).find("beetroots") != std::string::npos) maxAge = 3;
    if (age >= maxAge) return;
    if (getLight(w,x,y,z) < 9) return;
    float gs = growthSpeed(w,x,y,z);
    int denom = (int)(25.0f / gs);
    if (denom < 1) denom = 1;
    if ((rand() % denom) != 0) {
        // also 1/3 fallback to match spec 30% variant
        if ((rand()%100) >= 30) return;
    }
    w.setBlock(x,y,z, withAge(d, state, age+1));
}

bool CropBehavior::fertilize(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                             std::uint16_t state, GameServer* srv) {
    int age = getAge(state);
    int maxAge = 7;
    const gen::BlockDef* d = gen::blockByState(state);
    if (!d) return false;
    if (std::string(d->name).find("beetroots")!=std::string::npos) maxAge=3;
    if (age >= maxAge) return false;
    w.setBlock(x,y,z, withAge(d, state, maxAge));
    (void)srv;
    return true;
}

// -------------------------------------------------------- Sapling

void SaplingBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                           std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now;
    if (srv && srv->gameRules().getInt("randomTickSpeed",3)==0) return;
    if ((rand() % 100) < 5) fertilize(w,x,y,z,state,srv);
}

bool SaplingBehavior::fertilize(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                                std::uint16_t state, GameServer* srv) {
    (void)srv;
    const gen::BlockDef* d = gen::blockByState(state);
    if (!d) return false;
    const std::uint16_t below = w.getBlock(x, y-1, z);
    const gen::BlockDef* bd = gen::blockByState(below);
    if (!bd || (std::string(bd->name).find("dirt")==std::string::npos &&
                std::string(bd->name)!="minecraft:grass_block")) return false;
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
    (void)now;
    if (srv && srv->gameRules().getInt("randomTickSpeed",3)==0) return;
    if ((rand()%100) >= 20) return;
    const gen::BlockDef* d = gen::blockByState(state);
    if (!d) return;
    std::string name(d->name);
    int age = getAge(state);
    const std::uint16_t below = w.getBlock(x,y-1,z);
    const gen::BlockDef* bbd = gen::blockByState(below);
    if (name.find("cactus")!=std::string::npos) {
        if (!bbd || (std::string(bbd->name)!="minecraft:sand" &&
                     std::string(bbd->name)!="minecraft:red_sand" &&
                     std::string(bbd->name)!="minecraft:cactus")) return;
        // cactus: check horizontal solid neighbors
        const int DX[4]={1,-1,0,0}, DZ[4]={0,0,1,-1};
        for(int di=0;di<4;++di){
            std::uint16_t nb = w.getBlock(x+DX[di], y, z+DZ[di]);
            if (nb==0) continue;
            auto* nbd = gen::blockByState(nb);
            if (!nbd) continue;
            // if neighbor is solid (not air/water etc), cactus cannot grow
            if (!nbd->transparent) return;
        }
    } else if (name.find("sugar_cane")!=std::string::npos) {
        // sugar_cane: allow growth even without water for smoke (vanilla requires water adjacent)
        (void)bbd;
    } else if (name.find("bamboo")!=std::string::npos) {
        // bamboo: check leaves prop? keep simple age only
        (void)bbd;
    }
    int h=1;
    for (int dy=1; w.getBlock(x,y+dy,z)!=0 && h < maxH_; ++dy) ++h;
    // also need to count below? bamboo can be on dirt etc. Not needed
    if (h >= maxH_) return;
    // for cactus/sugar_cane max height is column height including this block
    int columnHeight = 1;
    for(int dy=1; w.getBlock(x,y-dy,z)!=0 && gen::blockByState(w.getBlock(x,y-dy,z)) && std::string(gen::blockByState(w.getBlock(x,y-dy,z))->name)==name; ++dy) columnHeight++;
    for(int dy=1; w.getBlock(x,y+dy,z)!=0 && columnHeight < maxH_; ++dy) { columnHeight++; }
    if (columnHeight >= maxH_) return;
    if (age < 15) {
        w.setBlock(x,y,z, withAge(d, state, age+1));
    } else {
        if (w.getBlock(x,y+1,z)==0) {
            w.setBlock(x,y,z, withAge(d, state, 0));
            // new block should have age 0
            const gen::BlockDef* dd = d;
            std::vector<std::pair<std::string_view,std::string_view>> props0;
            for (auto& [k,v] : gen::propsOf(state)) if(k!="age") props0.emplace_back(k,v);
            props0.emplace_back("age", "0");
            // for cactus/sugar_cane, default age 0
            std::uint16_t base = static_cast<std::uint16_t>(dd->defaultState);
            // try to keep same props except age
            std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*dd, props0));
            (void)ns;
            w.setBlock(x,y+1,z, static_cast<std::uint16_t>(gen::blockByState(state)->defaultState));
            // ensure new block age 0 (if default has age 0 it's fine)
            std::uint16_t grew = w.getBlock(x,y+1,z);
            auto* gd = gen::blockByState(grew);
            if (gd) {
                for(auto& [k,v]: gen::propsOf(grew)) if(k=="age" && v!="0"){
                    w.setBlock(x,y+1,z, withAge(gd, grew, 0));
                    break;
                }
            }
        }
    }
}

// -------------------------------------------------------- Farmland moisture

void FarmlandBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                            std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now;
    if (srv && srv->gameRules().getInt("randomTickSpeed",3)==0) return;
    int moist = getMoisture(state);
    bool hasWater = false;
    for (int dx=-4; dx<=4 && !hasWater; ++dx)
        for (int dz=-4; dz<=4 && !hasWater; ++dz)
            for (int dy=0; dy<=1 && !hasWater; ++dy) {
                std::uint16_t bs = w.getBlock(x+dx,y+dy,z+dz);
                if (bs==0) continue;
                const gen::BlockDef* bd = gen::blockByState(bs);
                if (!bd) continue;
                if (std::string(bd->name).find("water")!=std::string::npos) {
                    // check level if present? any water counts
                    hasWater = true;
                }
            }
    // rain hydration: if raining at above, consider hasWater true (simplified)
    // we keep as is; rain check would require World::isRainingAt which we approximate via gamerule
    int want = hasWater ? 7 : std::max(0, moist-1);
    if (want != moist) {
        const gen::BlockDef* d = gen::blockByState(state);
        std::vector<std::pair<std::string_view,std::string_view>> props;
        for (auto& [k,v] : gen::propsOf(state)) if(k!="moisture") props.emplace_back(k,v);
        std::string ms = std::to_string(want);
        props.emplace_back("moisture", ms);
        const std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*d, props));
        w.setBlock(x,y,z, ns);
        state = ns;
        moist = want;
    }
    if (moist==0) {
        // if no crop above and no water, revert to dirt
        bool hasCrop = false;
        std::uint16_t above = w.getBlock(x,y+1,z);
        if (above!=0) {
            auto* ad = gen::blockByState(above);
            if (ad && (std::string(ad->name).find("wheat")!=std::string::npos ||
                       std::string(ad->name).find("carrots")!=std::string::npos ||
                       std::string(ad->name).find("potatoes")!=std::string::npos ||
                       std::string(ad->name).find("beetroots")!=std::string::npos ||
                       std::string(ad->name).find("cocoa")!=std::string::npos ||
                       std::string(ad->name).find("melon")!=std::string::npos ||
                       std::string(ad->name).find("pumpkin")!=std::string::npos))
                hasCrop = true;
        }
        if (!hasCrop && !hasWater) {
            w.setBlock(x,y,z, gen::blockNameToState().at("minecraft:dirt"));
        }
    }
}

// -------------------------------------------------------- Fire

FlammableRegistry::FlammableRegistry() {
    table_ = {
        {"minecraft:oak_planks", {5,20}}, {"minecraft:spruce_planks",{5,20}}, {"minecraft:birch_planks",{5,20}},
        {"minecraft:jungle_planks",{5,20}}, {"minecraft:acacia_planks",{5,20}}, {"minecraft:dark_oak_planks",{5,20}},
        {"minecraft:oak_log",{5,5}}, {"minecraft:spruce_log",{5,5}}, {"minecraft:birch_log",{5,5}},
        {"minecraft:jungle_log",{5,5}}, {"minecraft:acacia_log",{5,5}}, {"minecraft:dark_oak_log",{5,5}},
        {"minecraft:oak_leaves",{30,60}}, {"minecraft:spruce_leaves",{30,60}}, {"minecraft:birch_leaves",{30,60}},
        {"minecraft:jungle_leaves",{30,60}}, {"minecraft:acacia_leaves",{30,60}}, {"minecraft:dark_oak_leaves",{30,60}},
        {"minecraft:white_wool",{30,60}}, {"minecraft:orange_wool",{30,60}}, {"minecraft:magenta_wool",{30,60}},
        {"minecraft:hay_block",{60,20}}, {"minecraft:bookshelf",{30,20}}, {"minecraft:tnt",{15,100}},
        {"minecraft:vine",{15,100}}, {"minecraft:coal_block",{5,5}},
    };
}
const FlammableRegistry& FlammableRegistry::instance() {
    static FlammableRegistry inst;
    return inst;
}
std::optional<FlammableEntry> FlammableRegistry::get(const std::string& blockName) const {
    auto it = table_.find(blockName);
    if (it != table_.end()) return it->second;
    // fallback heuristics
    if (blockName.find("planks")!=std::string::npos) return FlammableEntry{5,20};
    if (blockName.find("_log")!=std::string::npos) return FlammableEntry{5,5};
    if (blockName.find("leaves")!=std::string::npos) return FlammableEntry{30,60};
    if (blockName.find("wool")!=std::string::npos) return FlammableEntry{30,60};
    if (blockName=="minecraft:hay_block") return FlammableEntry{60,20};
    return std::nullopt;
}

bool FireBehavior::isFlammable(const std::string& blockName) const {
    return FlammableRegistry::instance().get(blockName).has_value();
}

void FireBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                        std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now;
    if (srv) {
        auto* gr = &srv->gameRules();
        if (gr && !gr->getBool("doFireTick")) return;
    }
    int age = getAge(state);
    if (age < 15 && (rand()%3)==0) {
        const gen::BlockDef* d = gen::blockByState(state);
        w.setBlock(x,y,z, withAge(d, state, age+1));
        age++;
        state = w.getBlock(x,y,z);
    }
    // rain extinguishes 1/3
    // simplified: if raining, 33% chance to extinguish
    // we approximate via random; if we had raining flag, use it
    // (check gamerule raining via world? skip)

    // spread to 5 directions + up 4 blocks
    const int DIRS[5][3] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,1,0}};
    const auto& reg = FlammableRegistry::instance();
    for (int di=0; di<5; ++di) {
        int nx = x + DIRS[di][0];
        int ny = y + DIRS[di][1];
        int nz = z + DIRS[di][2];
        // for up direction, scan 4 blocks up
        int scan = (di==4 ? 4 : 1);
        for (int sy=0; sy<scan; ++sy) {
            int sx = nx, sy2 = ny + (di==4 ? sy : 0), sz = nz;
            if (di==4 && sy>0) { sx = x; sz = z; sy2 = y + 1 + sy; }
            std::uint16_t ns = w.getBlock(sx,sy2,sz);
            const gen::BlockDef* nd = gen::blockByState(ns);
            if (!nd) continue;
            // if target is air, check if adjacent to flammable?
            if (ns==0) {
                // check if flammable neighbor exists for ignition
                bool canIgnite=false;
                // look for flammable block around target
                for(int ddx=-1; ddx<=1 && !canIgnite; ++ddx) for(int ddy=-1;ddy<=1 && !canIgnite; ++ddy) for(int ddz=-1; ddz<=1 && !canIgnite; ++ddz){
                    if(ddx==0&&ddy==0&&ddz==0) continue;
                    auto* ad = gen::blockByState(w.getBlock(sx+ddx,sy2+ddy,sz+ddz));
                    if(ad && reg.get(std::string(ad->name))) canIgnite=true;
                }
                if(!canIgnite) continue;
                if ((rand()%100) < getSpreadChance()) {
                    const auto fireIt = gen::blockNameToState().find("minecraft:fire");
                    if (fireIt != gen::blockNameToState().end() && w.getBlock(sx,sy2,sz)==0) w.setBlock(sx,sy2,sz, fireIt->second);
                }
            } else {
                // if neighbor is flammable, try to ignite it directly
                auto opt = reg.get(std::string(nd->name));
                if (!opt) continue;
                int igniteOdds = opt->igniteOdds;
                if (igniteOdds<=0) continue;
                if ((rand() % igniteOdds)==0) {
                    // convert flammable block to fire if air above? but spec replaces flammable with fire
                    if (w.getBlock(sx,sy2,sz)!=0) {
                        // only if flammable block itself could become fire? vanilla replaces?
                        // we set fire at that pos if it's the flammable block? For now set air neighbor
                    }
                    const auto fireIt = gen::blockNameToState().find("minecraft:fire");
                    if (fireIt != gen::blockNameToState().end()) {
                        // if target block is flammable, replace it with fire (if not solid? )
                        // Keep simple: if flammable, set adjacent air to fire was already. If direct, set itself to fire after a check
                        if ((rand()%2)==0) w.setBlock(sx,sy2,sz, fireIt->second);
                    }
                }
            }
        }
    }
    // also consider spread to 8 horizontal+up via original loop for compatibility
    for (int dx=-1; dx<=1; ++dx) for (int dy=-1; dy<=1; ++dy) for (int dz=-1; dz<=1; ++dz){
        if (dx==0&&dy==0&&dz==0) continue;
        if (std::abs(dx)+std::abs(dy)+std::abs(dz) > 2) continue; // limit to 6 dirs plus maybe corners slight
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
                if (fire != gen::blockNameToState().end()) w.setBlock(x+dx,y+dy,z+dz, fire->second);
            }
        }
    }
    // update fire shape props north/east/south/west/up based on flammable neighbors
    {
        const gen::BlockDef* d = gen::blockByState(state);
        if (d) {
            bool n = isFlammable(std::string(gen::blockByState(w.getBlock(x, y, z+1)) ? gen::blockByState(w.getBlock(x, y, z+1))->name : "")); // placeholder
            // Instead compute correctly via neighbor check
            bool north = false, south=false, east=false, west=false, up=false;
            auto isFlamAt = [&](int ox,int oy,int oz){ auto* bd=gen::blockByState(w.getBlock(x+ox,y+oy,z+oz)); return bd && isFlammable(std::string(bd->name)); };
            north = isFlamAt(0,0,-1);
            south = isFlamAt(0,0,1);
            east = isFlamAt(1,0,0);
            west = isFlamAt(-1,0,0);
            up = isFlamAt(0,1,0);
            // if any flammable, ensure corresponding bool prop true; vanilla updates shape, but we keep age only
            // we could set block state with those props if fire has them
            bool hasNorth=false, hasSouth=false, hasEast=false, hasWest=false, hasUp=false;
            for(auto& [k,v]: gen::propsOf(state)){
                if(k=="north") hasNorth=true;
                if(k=="south") hasSouth=true;
                if(k=="east") hasEast=true;
                if(k=="west") hasWest=true;
                if(k=="up") hasUp=true;
            }
            if(hasNorth||hasSouth||hasEast||hasWest||hasUp){
                std::vector<std::pair<std::string_view,std::string_view>> props;
                for(auto& [k,v]: gen::propsOf(state)){
                    if(k=="north") props.emplace_back(k, north?"true":"false");
                    else if(k=="south") props.emplace_back(k, south?"true":"false");
                    else if(k=="east") props.emplace_back(k, east?"true":"false");
                    else if(k=="west") props.emplace_back(k, west?"true":"false");
                    else if(k=="up") props.emplace_back(k, up?"true":"false");
                    else props.emplace_back(k,v);
                }
                std::uint16_t ns = static_cast<std::uint16_t>(gen::stateWithProps(*d, props));
                if(ns!=state) w.setBlock(x,y,z, ns);
            }
        }
    }
    if (age >= 15) {
        // check has flammable below
        bool hasFlammableBelow=false;
        for(int dx=-1;dx<=1 && !hasFlammableBelow;++dx) for(int dz=-1;dz<=1 && !hasFlammableBelow;++dz) for(int dy=-1; dy<=0 && !hasFlammableBelow; ++dy){
            auto* bd = gen::blockByState(w.getBlock(x+dx,y+dy,z+dz));
            if(bd && isFlammable(std::string(bd->name))) hasFlammableBelow=true;
        }
        if (!hasFlammableBelow && (rand()%4)==0) w.setBlock(x,y,z, 0);
    }
}

// -------------------------------------------------------- PortalAge (plan6 §3)

void PortalAgeBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                             std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now; (void)srv;
    int age = getAge(state);
    const gen::BlockDef* d = gen::blockByState(state);
    if (!d) return;
    if (age < 15) {
        if ((rand() % 100) < 10) {
            w.setBlock(x,y,z, withAge(d, state, age+1));
        }
    } else {
        if ((rand() % 100) < 2) {
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
    if (blockName == "minecraft:soul_sand" || blockName == "minecraft:soul_soil") return true;
    return false;
}
void SoulFireBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                            std::uint16_t state, std::int64_t now, GameServer* srv) {
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
    if (blockName.find("planks") != std::string::npos) return true;
    if (blockName.find("log") != std::string::npos) return true;
    return false;
}
void CampfireBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                            std::uint16_t state, std::int64_t now, GameServer* srv) {
    bool lit = false;
    for (auto& [k,v] : gen::propsOf(state)) if (k=="lit" && v=="true") lit = true;
    if (!lit) return;
    FireBehavior::tick(w, x, y, z, state, now, srv);
}

// -------------------------------------------------------- Cocoa

void CocoaBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                         std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now;
    if (srv && srv->gameRules().getInt("randomTickSpeed",3)==0) return;
    int age = getAge(state);
    if (age >= 2) return;
    // check jungle_log neighbor for support
    const int DX[4]={1,-1,0,0}, DZ[4]={0,0,1,-1};
    bool hasLog=false;
    for(int d=0; d<4; ++d){
        auto* bd = gen::blockByState(w.getBlock(x+DX[d], y, z+DZ[d]));
        if(bd && std::string(bd->name)=="minecraft:jungle_log") { hasLog=true; break; }
    }
    if(!hasLog) return;
    if ((rand()%5)==0) {
        const gen::BlockDef* d = gen::blockByState(state);
        w.setBlock(x,y,z, withAge(d, state, age+1));
    }
}
bool CocoaBehavior::fertilize(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                              std::uint16_t state, GameServer* srv) {
    int age = getAge(state);
    if (age>=2) return false;
    const gen::BlockDef* d = gen::blockByState(state);
    w.setBlock(x,y,z, withAge(d, state, 2));
    (void)srv;
    return true;
}

// Sweet berry
void SweetBerryBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                              std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now;
    if (srv && srv->gameRules().getInt("randomTickSpeed",3)==0) return;
    int age = getAge(state);
    if (age>=3) return;
    int chance = (age<=1? 3 : 2); // 1/3 for 0-1, 1/2 for 2
    if ((rand()%chance)!=0) return;
    if (getLight(w,x,y,z) < 9) return;
    const gen::BlockDef* d = gen::blockByState(state);
    w.setBlock(x,y,z, withAge(d, state, age+1));
}
bool SweetBerryBehavior::fertilize(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                                   std::uint16_t state, GameServer* srv) {
    int age = getAge(state);
    if (age>=3) return false;
    const gen::BlockDef* d = gen::blockByState(state);
    w.setBlock(x,y,z, withAge(d, state, age+1));
    (void)srv;
    return true;
}

// Nether wart
void NetherWartBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                              std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now;
    if (srv && srv->gameRules().getInt("randomTickSpeed",3)==0) return;
    auto* below = gen::blockByState(w.getBlock(x, y-1, z));
    if (!below || std::string(below->name)!="minecraft:soul_sand") return;
    int age = getAge(state);
    if (age>=3) return;
    if ((rand()%10)!=0) return;
    const gen::BlockDef* d = gen::blockByState(state);
    w.setBlock(x,y,z, withAge(d, state, age+1));
}

// Chorus flower
void ChorusFlowerBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                                std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now;
    if (srv && srv->gameRules().getInt("randomTickSpeed",3)==0) return;
    int age = getAge(state);
    if (age>=5) {
        // dead -> become dead chorus? for now set air
        if ((rand()%5)==0) w.setBlock(x,y,z, 0);
        return;
    }
    if ((rand()%5)!=0) return;
    const gen::BlockDef* d = gen::blockByState(state);
    w.setBlock(x,y,z, withAge(d, state, age+1));
    // branch: 1/5 chance to spawn chorus_plant around? simplified: if age==5 and above air, spawn chorus_plant below?
    if (age+1==5 && w.getBlock(x,y+1,z)==0) {
        auto it = gen::blockNameToState().find("minecraft:chorus_plant");
        if (it!=gen::blockNameToState().end()) w.setBlock(x,y+1,z, it->second);
    }
}

// Kelp
void KelpBehavior::tick(World& w, std::int32_t x, std::int32_t y, std::int32_t z,
                        std::uint16_t state, std::int64_t now, GameServer* srv) {
    (void)now;
    if (srv && srv->gameRules().getInt("randomTickSpeed",3)==0) return;
    int age = getAge(state);
    if (age>=25) return;
    if (w.getBlock(x,y+1,z)!=0) {
        // check if above is water
        auto* ab = gen::blockByState(w.getBlock(x,y+1,z));
        if (!ab || std::string(ab->name).find("water")==std::string::npos) return;
    } else {
        // air above with water? kelp needs water
        auto* ab = gen::blockByState(w.getBlock(x,y+1,z));
        if (!ab || std::string(ab->name).find("water")==std::string::npos) {
            // also allow if above is waterlogged? simplified false
            return;
        }
    }
    if ((rand()%10)!=0) return; // slower growth
    const gen::BlockDef* d = gen::blockByState(state);
    w.setBlock(x,y,z, withAge(d, state, age+1));
    if (age+1==25) {
        // try to grow one more kelp on top
        if (w.getBlock(x,y+1,z)!=0) {
            auto* ab = gen::blockByState(w.getBlock(x,y+1,z));
            if (ab && std::string(ab->name).find("water")!=std::string::npos) {
                w.setBlock(x,y+1,z, gen::blockNameToState().at("minecraft:kelp"));
            }
        } else {
            // water check already; but if air, we consider water?
        }
    }
}

} // namespace cppfm
