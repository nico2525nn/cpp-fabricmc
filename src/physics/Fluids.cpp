// Fluids implementation.
#include "Fluids.hpp"
#include <algorithm>

namespace cppfm {

namespace {
constexpr int kWaterInterval = 5;
constexpr int kLavaInterval = 30;
constexpr int kMaxRunLevel = 7;
} // namespace

int FluidSim::kindAt(std::uint16_t state, int& levelOut) const {
    levelOut = -1;
    const gen::BlockDef* b = gen::blockByState(state);
    if (!b) return -1;
    // waterlogged blocks act as water source for fluid interaction
    if (WaterloggableHelper::getWaterlogged(state)) {
        levelOut = 0;
        return 0;
    }
    if (b->name == "minecraft:water" || b->name == "minecraft:flowing_water") {
        for (auto& [k, v] : gen::propsOf(state))
            if (k == "level") { levelOut = std::atoi(std::string(v).c_str()); break; }
        if (levelOut==-1) levelOut=0;
        return 0;
    }
    if (b->name == "minecraft:lava" || b->name == "minecraft:flowing_lava") {
        for (auto& [k, v] : gen::propsOf(state))
            if (k == "level") { levelOut = std::atoi(std::string(v).c_str()); break; }
        if (levelOut==-1) levelOut=0;
        return 1;
    }
    // seagrass/kelp are water-plants but also water source? treat as not fluid for solidify
    return -1;
}

std::uint16_t FluidSim::fluidState(Kind k, int level) const {
    const char* name = k == Kind::Water ? "minecraft:water" : "minecraft:lava";
    return static_cast<std::uint16_t>(gen::stateWithPropsList(
        name, {{"level", std::to_string(std::clamp(level, 0, 15))}}));
}

FluidState FluidSim::getFluidState(World& w, std::int32_t x, std::int32_t y, std::int32_t z) {
    std::uint16_t st = w.getBlock(x,y,z);
    if (st==0) return {FluidId::Empty, 0, false};
    if (WaterloggableHelper::getWaterlogged(st)) return {FluidId::Water, 0, false};
    const gen::BlockDef* b = gen::blockByState(st);
    if (!b) return {FluidId::Empty,0,false};
    if (b->name=="minecraft:water" || b->name=="minecraft:flowing_water") {
        int lvl=0;
        for(auto& [k,v]: gen::propsOf(st)) if(k=="level") lvl=std::atoi(std::string(v).c_str());
        bool falling = lvl==8;
        return {FluidId::Water, lvl, falling};
    }
    if (b->name=="minecraft:lava" || b->name=="minecraft:flowing_lava") {
        int lvl=0;
        for(auto& [k,v]: gen::propsOf(st)) if(k=="level") lvl=std::atoi(std::string(v).c_str());
        bool falling = lvl==8;
        return {FluidId::Lava, lvl, falling};
    }
    // kelp/seagrass imply water above but not fluid at this pos
    return {FluidId::Empty,0,false};
}
void FluidSim::checkInteraction(World& w, std::int32_t x, std::int32_t y, std::int32_t z, FluidState a, FluidState b) {
    (void)w; (void)x; (void)y; (void)z; (void)a; (void)b;
}

void FluidSim::touch(std::int32_t x, std::int32_t y, std::int32_t z) {
    if (!world_.isChunkInSimulationDistance(x >> 4, z >> 4) && !world_.isPositionInSimulationDistance(x, z)) return;
    schedule(x, y, z, 0);
}

void FluidSim::tick(std::int64_t now) {
    while (!queue_.empty() && queue_.top().dueTick <= now) {
        const FluidTick t = queue_.top();
        queue_.pop();
        if (!world_.isChunkInSimulationDistance(t.x >> 4, t.z >> 4) && !world_.isPositionInSimulationDistance(t.x, t.z)) continue;
        apply(t.x, t.y, t.z, now);
    }
}

void FluidSim::apply(std::int32_t x, std::int32_t y, std::int32_t z,
                     std::int64_t now) {
    if (!world_.isChunkInSimulationDistance(x >> 4, z >> 4) && !world_.isPositionInSimulationDistance(x, z)) return;
    const std::uint64_t worldRevAtEntry = world_.revisionAt(x >> 4, z >> 4);
    const std::uint16_t st = world_.getBlock(x, y, z);
    int level = -1;
    const int kindInt = kindAt(st, level);
    if (kindInt < 0) return;
    const Kind kind = kindInt == 0 ? Kind::Water : Kind::Lava;
    const bool isSource = level == 0;
    const int interval = kind == Kind::Water ? kWaterInterval : kLavaInterval;

    auto solidifyCheck = [&](std::int32_t nx, std::int32_t ny, std::int32_t nz) -> bool {
        const std::uint16_t ns = world_.getBlock(nx, ny, nz);
        int nl = -1;
        const int nk = kindAt(ns, nl);
        if (nk < 0) return false;
        const bool lavaHere = kind == Kind::Lava;
        const bool meetsOther = (lavaHere && nk == 0) || (!lavaHere && nk == 1);
        if (!meetsOther) return false;
        const std::uint16_t cobble = static_cast<std::uint16_t>(gen::blockNameToState().at("minecraft:cobblestone"));
        const std::uint16_t obsidian = static_cast<std::uint16_t>(gen::blockNameToState().at("minecraft:obsidian"));
        const std::uint16_t stone = static_cast<std::uint16_t>(gen::blockNameToState().at("minecraft:stone"));
        // 3 rules: per plan12 §8
        if (lavaHere) {
            // lava source touched by water side/top -> obsidian
            if (isSource && nk==0) {
                world_.setBlock(nx, ny, nz, obsidian);
            } else if (nk==0 && ny == y+1) {
                // lava flowing down onto water -> stone
                world_.setBlock(nx, ny, nz, stone);
            } else if (nk==0 && ny==y-1) {
                // water below lava falling -> stone as well
                world_.setBlock(nx, ny, nz, stone);
            } else {
                // lava flowing + water side -> cobble
                world_.setBlock(nx, ny, nz, isSource ? obsidian : cobble);
                // if this lava is flowing and water is source side, original water becomes obsidian? we handle below
                if (!isSource && nk==0) {
                    world_.setBlock(x, y, z, cobble);
                    return true;
                }
                // For water perspective, lava source -> obsidian
            }
        } else {
            // water + lava source -> obsidian, else cobble/stone
            if (nl == 0) {
                world_.setBlock(nx, ny, nz, obsidian);
            } else {
                // flowing lava + water
                if (ny == y - 1 || ny == y+1) {
                    world_.setBlock(nx, ny, nz, stone);
                } else {
                    world_.setBlock(nx, ny, nz, cobble);
                }
            }
            // also handle waterlogged solidify: if waterlogged at this pos, the water part should become stone/cobble? but waterlogged block remains with waterlogged false?
            // For simplicity, if waterlogged, set stone/cobble and clear waterlogged is not possible via simple block change; keep as stone
        }
        return true;
    };

    const std::uint16_t belowState = world_.getBlock(x, y - 1, z);
    int belowLevel = -1;
    const int belowKind = kindAt(belowState, belowLevel);
    if (belowKind >= 0 && belowKind != kindInt) {
        solidifyCheck(x, y - 1, z);
        schedule(x, y - 1, z, now + interval);
    }
    const bool belowAirOrSame = belowState == 0 || (belowKind == kindInt);
    if (belowAirOrSame && y - 1 >= kMinY) {
        if (belowState == 0 || (belowKind == kindInt && belowLevel != 8 && belowLevel != 0)) {
            const std::uint16_t falling = fluidState(kind, 8);
            if (world_.getBlock(x, y - 1, z) != falling)
                world_.setBlock(x, y - 1, z, falling);
            schedule(x, y - 1, z, now + interval);
        }
    }

    if (!isSource && level != 8) {
        static constexpr int DX[4] = {1,-1,0,0};
        static constexpr int DZ[4] = {0,0,1,-1};
        int best = 99;
        bool fedByFall = false;
        for (int d = 0; d < 4; ++d) {
            const std::uint16_t ns = world_.getBlock(x + DX[d], y, z + DZ[d]);
            int nl = -1;
            const int nk = kindAt(ns, nl);
            if (nk != kindInt) continue;
            if (nl == 8) fedByFall = true;
            if (nl == 0) best = -1;
            else best = std::min(best, nl);
        }
        int lavaStep = world_.dimensionId()==-1 ? 1 : 2;
        int want;
        if (best == -1) want = 1;
        else if (best >= 99) want = -2;
        else want = best + (kind == Kind::Water ? 1 : lavaStep);

        if (want == -2 || want > kMaxRunLevel) {
            if (world_.getBlock(x, y, z) != 0)
                world_.setBlock(x, y, z, 0);
            static constexpr int DX2[4] = {1,-1,0,0};
            static constexpr int DZ2[4] = {0,0,1,-1};
            for (int d = 0; d < 4; ++d) schedule(x + DX2[d], y, z + DZ2[d], now + interval);
            schedule(x, y + 1, z, now + interval);
            return;
        }
        if (want != level) {
            world_.setBlock(x, y, z, fluidState(kind, want));
            level = want;
        }
        (void)fedByFall;
    }

    if (isSource || level < kMaxRunLevel || level == 8) {
        int lavaStep2 = world_.dimensionId()==-1 ? 1 : 2;
        const int nextLevel = level == 8 ? 1 : level + (kind == Kind::Water ? 1 : lavaStep2);
        if (nextLevel <= kMaxRunLevel) {
            static constexpr int DX3[4] = {1,-1,0,0};
            static constexpr int DZ3[4] = {0,0,1,-1};
            for (int d = 0; d < 4; ++d) {
                const std::int32_t nx = x + DX3[d], nz = z + DZ3[d];
                if (y < kMinY || y >= kMaxY) continue;
                const std::uint16_t ns = world_.getBlock(nx, y, nz);
                if (ns != 0) {
                    int nl = -1;
                    int nk = kindAt(ns, nl);
                    if (nk >= 0 && nk != kindInt) {
                        solidifyCheck(nx, y, nz);
                        continue;
                    }
                    if (nk == kindInt && nl <= nextLevel) continue;
                    if (nk < 0 && ns!=0) {
                        // solid block blocks flow
                        const gen::BlockDef* bd = gen::blockByState(ns);
                        if (bd && !bd->transparent) continue;
                    }
                    if (nk != kindInt && nk>=0) continue;
                    if (nk < 0 && ns!=0) continue;
                }
                // waterlogged check: flowing into waterlogged air? waterlogged is considered water, but we treat as solidify earlier
                if (WaterloggableHelper::isWaterloggable(ns) && WaterloggableHelper::getWaterlogged(ns)) {
                    solidifyCheck(nx, y, nz);
                    continue;
                }
                if (world_.getBlock(nx, y, nz)==0) {
                    world_.setBlock(nx, y, nz, fluidState(kind, nextLevel));
                    schedule(nx, y, nz, now + interval);
                }
            }
        }
    }
    {
        static constexpr int DXF[6] = {1,-1,0,0,0,0};
        static constexpr int DYF[6] = {0,0,1,-1,0,0};
        static constexpr int DZF[6] = {0,0,0,0,1,-1};
        for (int d = 0; d < 6; ++d) solidifyCheck(x + DXF[d], y + DYF[d], z + DZF[d]);
        static constexpr int DXH[4] = {1,-1,0,0};
        static constexpr int DZH[4] = {0,0,1,-1};
        for (int d = 0; d < 4; ++d) {
            solidifyCheck(x + DXH[d], y, z + DZH[d]);
            solidifyCheck(x + DXH[d], y - 1, z + DZH[d]);
        }
    }
    if (worldRevAtEntry != world_.revisionAt(x >> 4, z >> 4))
        schedule(x, y, z, now + interval * 2);
}

// WaterloggableHelper

bool WaterloggableHelper::isWaterloggable(const std::string& blockName) {
    // stairs/slab/fence/wall/trapdoor etc have waterlogged
    if (blockName.find("stairs")!=std::string::npos) return true;
    if (blockName.find("_slab")!=std::string::npos) return true;
    if (blockName.find("fence")!=std::string::npos) return true;
    if (blockName.find("wall")!=std::string::npos) return true;
    if (blockName.find("trapdoor")!=std::string::npos) return true;
    if (blockName.find("ladder")!=std::string::npos) return true;
    if (blockName.find("sign")!=std::string::npos) return true;
    if (blockName.find("chest")!=std::string::npos) return false;
    if (blockName=="minecraft:glass" || blockName=="minecraft:barrier") return false;
    // kelp/seagrass are not waterloggable but are water plants
    if (blockName.find("kelp")!=std::string::npos) return false;
    if (blockName.find("seagrass")!=std::string::npos) return false;
    return false;
}
bool WaterloggableHelper::isWaterloggable(std::uint16_t state) {
    const gen::BlockDef* d = gen::blockByState(state);
    if (!d) return false;
    if (std::string(d->name).find("_slab")!=std::string::npos) {
        // double slab not waterloggable
        for(auto& [k,v]: gen::propsOf(state)) if(k=="type" && v=="double") return false;
    }
    return isWaterloggable(std::string(d->name));
}
bool WaterloggableHelper::getWaterlogged(std::uint16_t state) {
    for(auto& [k,v]: gen::propsOf(state)) if(k=="waterlogged") return v=="true";
    return false;
}
std::uint16_t WaterloggableHelper::withWaterlogged(std::uint16_t state, bool v) {
    const gen::BlockDef* d = gen::blockByState(state);
    if (!d) return state;
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for(auto& [k,vv]: gen::propsOf(state)) if(k!="waterlogged") props.emplace_back(k,vv);
    props.emplace_back("waterlogged", v?"true":"false");
    return static_cast<std::uint16_t>(gen::stateWithProps(*d, props));
}

} // namespace cppfm
