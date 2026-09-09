// Fluids implementation.
#include "Fluids.hpp"
#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace cppfm {

namespace {
constexpr int kWaterInterval = 5;
constexpr int kLavaInterval = 30;
constexpr int kMaxRunLevel = 7;

bool hasProperty(const gen::BlockDef& block, std::string_view name) {
    for (int i = 0; i < block.propCount; ++i) {
        const auto& property = gen::kPropDefs[gen::kBlockPropsRun[block.propsOff + i]];
        if (property.name == name) return true;
    }
    return false;
}

std::uint16_t namedState(std::string_view name) {
    const auto it = gen::blockNameToState().find(std::string(name));
    return it == gen::blockNameToState().end()
        ? 0
        : static_cast<std::uint16_t>(it->second);
}

bool isNamed(const gen::BlockDef* block, std::string_view name) {
    return block && block->name == name;
}

bool isFluidBlock(const gen::BlockDef* block) {
    return isNamed(block, "minecraft:water") ||
           isNamed(block, "minecraft:lava");
}

bool isWaterloggedState(std::uint16_t state) {
    for (const auto& [key, value] : gen::propsOf(state))
        if (key == "waterlogged") return value == "true";
    return false;
}

// The generated BlockDef table does not yet expose AbstractBlock's
// `replaceable` flag. Keep the fallback conservative: these are blocks that
// vanilla fluids routinely replace, while transparent solids such as glass
// and leaves remain blocking. Waterloggable blocks are handled separately.
bool isFluidReplaceable(std::uint16_t state) {
    if (state == 0) return true;
    const auto* block = gen::blockByState(state);
    if (!block) return false;
    const std::string_view name = block->name;
    if (name == "minecraft:fire" || name == "minecraft:soul_fire" ||
        name == "minecraft:snow" || name == "minecraft:short_grass" ||
        name == "minecraft:tall_grass" || name == "minecraft:fern" ||
        name == "minecraft:large_fern" || name == "minecraft:dead_bush" ||
        name == "minecraft:vine" || name == "minecraft:glow_lichen" ||
        name == "minecraft:torch" || name == "minecraft:redstone_torch" ||
        name == "minecraft:soul_torch" || name == "minecraft:lever" ||
        name == "minecraft:tripwire" || name == "minecraft:redstone_wire") {
        return true;
    }
    return name.ends_with("_flower") || name.ends_with("_sapling") ||
           name.ends_with("_mushroom");
}

bool isSolidSupport(std::uint16_t state) {
    if (state == 0 || isFluidBlock(gen::blockByState(state))) return false;
    const auto* block = gen::blockByState(state);
    return block && !block->transparent;
}

enum class InteractionKind { None, Obsidian, Stone, Cobblestone };

InteractionKind interactionFor(const FluidState& incoming,
                               const FluidState& target,
                               bool downward,
                               bool targetIsLiquidBlock) {
    if (incoming.id == FluidId::Empty || target.id == FluidId::Empty ||
        incoming.id == target.id) {
        return InteractionKind::None;
    }

    if (incoming.isLava() && target.isWater()) {
        // The downward LavaFluid path converts an actual water LiquidBlock
        // to stone. A waterlogged host reports water to gameplay code but is
        // not itself a LiquidBlock, so the path must only stop the lava.
        if (downward)
            return targetIsLiquidBlock ? InteractionKind::Stone
                                       : InteractionKind::None;
        return incoming.level == 0 && !incoming.falling
            ? InteractionKind::Obsidian
            : InteractionKind::Cobblestone;
    }

    if (incoming.isWater() && target.isLava()) {
        return target.level == 0 && !target.falling
            ? InteractionKind::Obsidian
            : InteractionKind::Cobblestone;
    }
    return InteractionKind::None;
}
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
        if (levelOut >= 8) levelOut = 8;
        return 0;
    }
    if (b->name == "minecraft:lava" || b->name == "minecraft:flowing_lava") {
        for (auto& [k, v] : gen::propsOf(state))
            if (k == "level") { levelOut = std::atoi(std::string(v).c_str()); break; }
        if (levelOut==-1) levelOut=0;
        if (levelOut >= 8) levelOut = 8;
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
        bool falling = lvl >= 8;
        if (falling) lvl = 8;
        return {FluidId::Water, lvl, falling};
    }
    if (b->name=="minecraft:lava" || b->name=="minecraft:flowing_lava") {
        int lvl=0;
        for(auto& [k,v]: gen::propsOf(st)) if(k=="level") lvl=std::atoi(std::string(v).c_str());
        bool falling = lvl >= 8;
        if (falling) lvl = 8;
        return {FluidId::Lava, lvl, falling};
    }
    // kelp/seagrass imply water above but not fluid at this pos
    return {FluidId::Empty,0,false};
}
void FluidSim::checkInteraction(World& w, std::int32_t x, std::int32_t y, std::int32_t z, FluidState a, FluidState b) {
    if (a.id == FluidId::Empty || b.id == FluidId::Empty || a.id == b.id)
        return;

    const FluidState atPosition = getFluidState(w, x, y, z);
    FluidState incoming = a;
    FluidState target = b;
    if (atPosition.id == a.id && atPosition.level == a.level &&
        atPosition.falling == a.falling) {
        incoming = b;
        target = a;
    } else if (atPosition.id != b.id || atPosition.level != b.level ||
               atPosition.falling != b.falling) {
        return;
    }

    // Water evaporates in the Nether rather than converting lava. Do not
    // destroy a waterlogged host: its fluid state is water, but the host is
    // not a LiquidBlock and must remain in the world.
    if (w.dimensionId() == -1 && (incoming.isWater() || target.isWater())) {
        const auto* block = gen::blockByState(w.getBlock(x, y, z));
        if (!isWaterloggedState(w.getBlock(x, y, z)) &&
            isNamed(block, "minecraft:water")) {
            w.setBlock(x, y, z, 0);
        }
        return;
    }

    // This API has no direction or source position. It models the
    // horizontal/top-neighbour rule and changes only the fluid block at the
    // supplied position; downward lava->water conversion is handled in
    // apply(), where LiquidBlock-vs-waterlogged is observable.
    if (incoming.isLava() && target.isWater()) return;
    const InteractionKind result = interactionFor(
        incoming, target, false,
        isFluidBlock(gen::blockByState(w.getBlock(x, y, z))));
    if (result == InteractionKind::None) return;
    const char* name = result == InteractionKind::Obsidian
        ? "minecraft:obsidian"
        : "minecraft:cobblestone";
    const std::uint16_t state = namedState(name);
    if (state != 0) w.setBlock(x, y, z, state);
}

bool FluidSim::canConvertToSource(std::int32_t x, std::int32_t y, std::int32_t z) {
    const std::uint16_t below = world_.getBlock(x, y - 1, z);
    int belowLevel = -1;
    const int belowKind = kindAt(below, belowLevel);
    if (!isSolidSupport(below) && !(belowKind == 0 && belowLevel == 0))
        return false;

    int sources = 0;
    static constexpr int DX[4] = {1,-1,0,0};
    static constexpr int DZ[4] = {0,0,1,-1};
    for (int d=0; d<4; ++d) {
        FluidState fs = getFluidState(world_, x + DX[d], y, z + DZ[d]);
        if (fs.id == FluidId::Water && fs.level == 0 && !fs.falling) {
            if (++sources >= 2) return true;
        }
    }
    return false;
}

void FluidSim::touch(std::int32_t x, std::int32_t y, std::int32_t z) {
    // Do not inspect World from the callback path.  World::setBlock invokes
    // this method synchronously on the caller's thread while the tick thread
    // may be reading/updating the same world.  The tick-side distance check
    // below is the single gate, and an out-of-range notification is discarded
    // there without ever entering apply().
    schedule(x, y, z, 0);
}

std::size_t FluidSim::pending() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return scheduledDue_.size();
}

void FluidSim::tick(std::int64_t now) {
    for (;;) {
        FluidTick t{};
        {
            // Pop under the queue lock, then release it before any World
            // access.  This permits a setBlock callback to enqueue another
            // notification without waiting on the simulation work.
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (queue_.empty() || queue_.top().dueTick > now) return;
            t = queue_.top();
            queue_.pop();
            const auto key = queueKey(t.x, t.y, t.z);
            const auto scheduled = scheduledDue_.find(key);
            if (scheduled == scheduledDue_.end() ||
                scheduled->second != t.dueTick)
                continue; // an earlier entry was superseded by a reschedule
            scheduledDue_.erase(scheduled);
        }
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
    // A waterlogged block reports a source FluidState for neighbour
    // interaction, but it is not a LiquidBlock and must not run ordinary
    // fluid spread. In the Nether, only an actual water LiquidBlock
    // evaporates; a waterlogged host remains intact.
    if (isWaterloggedState(st)) return;
    if (kindInt == 0 && world_.dimensionId() == -1) {
        world_.setBlock(x, y, z, 0);
        return;
    }
    const Kind kind = kindInt == 0 ? Kind::Water : Kind::Lava;
    bool isSource = level == 0;
    const bool isNether = world_.dimensionId() == -1;
    const int interval = kind == Kind::Water ? kWaterInterval : (isNether ? 10 : kLavaInterval);
    const int maxLevel = (kind == Kind::Lava ? (isNether ? 7 : 6) : 7);
    if (kind == Kind::Water && !isSource && level != 8 && level != -1) {
        if (canConvertToSource(x, y, z)) {
            world_.setBlock(x, y, z, fluidState(Kind::Water, 0));
            level = 0;
            isSource = true;
            static constexpr int DXC[4]={1,-1,0,0};
            static constexpr int DZC[4]={0,0,1,-1};
            for (int d=0; d<4; ++d) schedule(x+DXC[d], y, z+DZC[d], now + interval);
            schedule(x, y+1, z, now + interval);
        }
    }

    const FluidState current{kind == Kind::Water ? FluidId::Water
                                                  : FluidId::Lava,
                             level, level == 8};
    auto setRock = [&](std::int32_t nx, std::int32_t ny, std::int32_t nz,
                       InteractionKind result) {
        const char* name = result == InteractionKind::Obsidian
            ? "minecraft:obsidian"
            : result == InteractionKind::Stone ? "minecraft:stone"
                                               : "minecraft:cobblestone";
        world_.setBlock(nx, ny, nz, namedState(name));
    };
    // Return true when the opposing fluid consumes or blocks this attempted
    // spread. A lava flow touching water horizontally consumes the lava at
    // the current position; a downward lava flow onto water transforms only
    // the target LiquidBlock and must not replace it with lava.
    auto interactAt = [&](std::int32_t nx, std::int32_t ny, std::int32_t nz,
                          bool downward) -> bool {
        const std::uint16_t ns = world_.getBlock(nx, ny, nz);
        int nl = -1;
        const int nk = kindAt(ns, nl);
        if (nk < 0 || nk == kindInt) return false;
        const FluidState target{nk == 0 ? FluidId::Water : FluidId::Lava,
                                nl, nl == 8};
        const bool targetLiquid = isFluidBlock(gen::blockByState(ns));
        const InteractionKind result = interactionFor(
            current, target, downward, targetLiquid);
        if (current.isLava() && target.isWater() && downward) {
            if (result != InteractionKind::None) setRock(nx, ny, nz, result);
            // A waterlogged host reports water but is not replaceable by the
            // downward lava special case; either way the downward path stops.
            return true;
        }
        if (current.isLava() && target.isWater() && !downward) {
            if (result != InteractionKind::None) {
                setRock(x, y, z, result);
                return true;
            }
            return false;
        }
        if (current.isWater() && target.isLava() &&
            result != InteractionKind::None) {
            setRock(nx, ny, nz, result);
            return false;
        }
        return false;
    };

    const std::uint16_t belowState = world_.getBlock(x, y - 1, z);
    int belowLevel = -1;
    const int belowKind = kindAt(belowState, belowLevel);
    if (belowKind >= 0 && belowKind != kindInt)
        interactAt(x, y - 1, z, true);
    if (belowKind < 0 && kind == Kind::Water &&
        WaterloggableHelper::isWaterloggable(belowState) &&
        !WaterloggableHelper::getWaterlogged(belowState)) {
        const std::uint16_t waterlogged =
            WaterloggableHelper::withWaterlogged(belowState, true);
        if (waterlogged != belowState) {
            world_.setBlock(x, y - 1, z, waterlogged);
            schedule(x, y - 1, z, now + interval);
        }
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
        for (int d = 0; d < 4; ++d) {
            const std::uint16_t ns = world_.getBlock(x + DX[d], y, z + DZ[d]);
            int nl = -1;
            const int nk = kindAt(ns, nl);
            if (nk != kindInt) continue;
            if (nl == 0) best = -1;
            else best = std::min(best, nl);
        }
        int lavaStep = isNether ? 1 : 2;
        int want;
        if (best == -1) want = 1;
        else if (best >= 99) want = -2;
        else want = best + (kind == Kind::Water ? 1 : lavaStep);

        if (want == -2 || want > maxLevel) {
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
    }

    if (isSource || level < maxLevel || level == 8) {
        int lavaStep2 = isNether ? 1 : 2;
        const int nextLevel = level == 8 ? 1 : level + (kind == Kind::Water ? 1 : lavaStep2);
        if (nextLevel <= maxLevel) {
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
                        if (interactAt(nx, y, nz, false)) return;
                        continue;
                    }
                    if (nk == kindInt && nl <= nextLevel) continue;
                    if (nk != kindInt && nk >= 0) continue;
                    if (nk < 0) {
                        if (kind == Kind::Water &&
                            WaterloggableHelper::isWaterloggable(ns) &&
                            !WaterloggableHelper::getWaterlogged(ns)) {
                            const std::uint16_t waterlogged =
                                WaterloggableHelper::withWaterlogged(ns, true);
                            if (waterlogged != ns) {
                                world_.setBlock(nx, y, nz, waterlogged);
                                schedule(nx, y, nz, now + interval);
                            }
                            continue;
                        }
                        if (!isFluidReplaceable(ns)) continue;
                    }
                }
                const std::uint16_t destinationState =
                    world_.getBlock(nx, y, nz);
                if (destinationState == 0 ||
                    isFluidReplaceable(destinationState)) {
                    int placeLevel = nextLevel;
                    if (kind == Kind::Water && nextLevel == 1 && canConvertToSource(nx, y, nz)) {
                        placeLevel = 0;
                    }
                    world_.setBlock(nx, y, nz, fluidState(kind, placeLevel));
                    schedule(nx, y, nz, now + interval);
                }
            }
        }
    }
    // Lava's neighbour check inspects the top and four horizontal faces,
    // never the bottom. Water has already handled its downward and
    // horizontal targets above, so it does not need a reciprocal scan.
    if (current.isLava()) {
        if (interactAt(x, y + 1, z, false)) return;
        static constexpr int DXH[4] = {1,-1,0,0};
        static constexpr int DZH[4] = {0,0,1,-1};
        for (int d = 0; d < 4; ++d)
            if (interactAt(x + DXH[d], y, z + DZH[d], false)) return;
    }
    if (worldRevAtEntry != world_.revisionAt(x >> 4, z >> 4))
        schedule(x, y, z, now + interval * 2);
}

// WaterloggableHelper

bool WaterloggableHelper::isWaterloggable(const std::string& blockName) {
    const gen::BlockDef* block = gen::blockByName(blockName);
    return block && hasProperty(*block, "waterlogged");
}
bool WaterloggableHelper::isWaterloggable(std::uint16_t state) {
    const gen::BlockDef* d = gen::blockByState(state);
    if (!d) return false;
    if (!hasProperty(*d, "waterlogged")) return false;
    if (std::string(d->name).find("_slab")!=std::string::npos) {
        // double slab not waterloggable
        for(auto& [k,v]: gen::propsOf(state)) if(k=="type" && v=="double") return false;
    }
    return true;
}
bool WaterloggableHelper::getWaterlogged(std::uint16_t state) {
    for(auto& [k,v]: gen::propsOf(state)) if(k=="waterlogged") return v=="true";
    return false;
}
std::uint16_t WaterloggableHelper::withWaterlogged(std::uint16_t state, bool v) {
    const gen::BlockDef* d = gen::blockByState(state);
    if (!d || !isWaterloggable(state)) return state;
    std::vector<std::pair<std::string_view,std::string_view>> props;
    for(auto& [k,vv]: gen::propsOf(state)) if(k!="waterlogged") props.emplace_back(k,vv);
    props.emplace_back("waterlogged", v?"true":"false");
    return static_cast<std::uint16_t>(gen::stateWithProps(*d, props));
}

} // namespace cppfm
