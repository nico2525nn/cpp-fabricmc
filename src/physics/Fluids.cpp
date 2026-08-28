// Fluids implementation.
#include "Fluids.hpp"
#include <algorithm>

namespace cppfm {

namespace {
constexpr int kWaterInterval = 5;      // ticks per flow step
constexpr int kLavaInterval = 30;
constexpr int kMaxRunLevel = 7;        // last visible flowing level
} // namespace

int FluidSim::kindAt(std::uint16_t state, int& levelOut) const {
    levelOut = -1;
    const gen::BlockDef* b = gen::blockByState(state);
    if (!b) return -1;
    if (b->name == "minecraft:water" || b->name == "minecraft:flowing_water") {
        for (auto& [k, v] : gen::propsOf(state))
            if (k == "level") { levelOut = std::atoi(std::string(v).c_str()); break; }
        if(levelOut==-1) levelOut=0;
        return 0;                                        // water
    }
    if (b->name == "minecraft:lava" || b->name == "minecraft:flowing_lava") {
        for (auto& [k, v] : gen::propsOf(state))
            if (k == "level") { levelOut = std::atoi(std::string(v).c_str()); break; }
        if(levelOut==-1) levelOut=0;
        return 1;                                        // lava
    }
    // plan12 §8 waterlogged: stairs/slab/fence etc with waterlogged=true is water source
    for(auto&[k,v]: gen::propsOf(state)) if(k=="waterlogged" && v=="true"){ levelOut=0; return 0; }
    return -1;
}

std::uint16_t FluidSim::fluidState(Kind k, int level) const {
    const char* name = k == Kind::Water ? "minecraft:water" : "minecraft:lava";
    return static_cast<std::uint16_t>(gen::stateWithPropsList(
        name, {{"level", std::to_string(std::clamp(level, 0, 15))}}));
}

void FluidSim::touch(std::int32_t x, std::int32_t y, std::int32_t z) {
    // Simulation-distance culling via isChunkInSimulationDistance for all subsystems (plan11 §1 #6)
    // Spawn chunks (ChunkTicket SPAWN level 31 / ForcedChunks) are always ticking even outside player radius.
    if (!world_.isChunkInSimulationDistance(x >> 4, z >> 4) && !world_.isPositionInSimulationDistance(x, z)) return;
    schedule(x, y, z, 0);                                // ASAP
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
    if (kindInt < 0) return;                             // not a fluid anymore
    const Kind kind = kindInt == 0 ? Kind::Water : Kind::Lava;
    const bool isSource = level == 0;
    const int interval = kind == Kind::Water ? kWaterInterval : kLavaInterval;

    // --- interaction: opposing fluids harden
    auto solidifyCheck = [&](std::int32_t nx, std::int32_t ny,
                             std::int32_t nz) -> bool {
        const std::uint16_t ns = world_.getBlock(nx, ny, nz);
        int nl = -1;
        const int nk = kindAt(ns, nl);
        if (nk < 0) return false;
        const bool lavaHere = kind == Kind::Lava;
        const bool meetsOther =
            (lavaHere && nk == 0) || (!lavaHere && nk == 1);
        if (!meetsOther) return false;
        const std::uint16_t cobble = static_cast<std::uint16_t>(
            gen::blockNameToState().at("minecraft:cobblestone"));
        const std::uint16_t obsidian = static_cast<std::uint16_t>(
            gen::blockNameToState().at("minecraft:obsidian"));
        const std::uint16_t stone = static_cast<std::uint16_t>(
            gen::blockNameToState().at("minecraft:stone"));
        if (lavaHere) {
            // lava + water above → stone; lava source touched by water → obsidian
            if (ny == y + 1) {
                world_.setBlock(nx, ny, nz, stone);
            } else {
                world_.setBlock(nx, ny, nz, isSource ? obsidian : cobble);
            }
        } else {
            // water + lava -> obsidian for source lava else cobble/stone
            if (nl == 0) {
                world_.setBlock(nx, ny, nz, obsidian);
            } else {
                if (ny == y - 1) {
                    world_.setBlock(nx, ny, nz, stone);
                } else {
                    world_.setBlock(nx, ny, nz, cobble);
                }
            }
        }
        return true;
    };

    // --- downward flow first (check solidify before falling)
    const std::uint16_t belowState = world_.getBlock(x, y - 1, z);
    int belowLevel = -1;
    const int belowKind = kindAt(belowState, belowLevel);
    if (belowKind >= 0 && belowKind != kindInt) {
        solidifyCheck(x, y - 1, z);
        schedule(x, y - 1, z, now + interval);
    }
    const bool belowAirOrSame =
        belowState == 0 || (belowKind == kindInt);
    if (belowAirOrSame && y - 1 >= kMinY) {
        if (belowState == 0 ||
            (belowKind == kindInt && belowLevel != 8 && belowLevel != 0)) {
            const std::uint16_t falling = fluidState(kind, 8);
            if (world_.getBlock(x, y - 1, z) != falling)
                world_.setBlock(x, y - 1, z, falling);
            schedule(x, y - 1, z, now + interval);
        }
        // vanilla keeps spreading sideways from a fall too
    }

    if (!isSource && level != 8) {
        // recompute desired level from horizontal neighbours
        static constexpr int DX[4] = {1,-1,0,0};
        static constexpr int DZ[4] = {0,0,1,-1};
        int best = 99;
        bool fedByFall = false;
        for (int d = 0; d < 4; ++d) {
            const std::uint16_t ns =
                world_.getBlock(x + DX[d], y, z + DZ[d]);
            int nl = -1;
            const int nk = kindAt(ns, nl);
            if (nk != kindInt) continue;
            if (nl == 8) fedByFall = true;               // vertical feed nearby
            if (nl == 0) best = -1;                      // source neighbour
            else best = std::min(best, nl);
        }
        int want;
        if (best == -1) want = 1;                        // next to a source
        else if (best >= 99) want = -2;                  // no supply at all
        else want = best + (kind == Kind::Water ? 1 : 2);

        if (want == -2 || want > kMaxRunLevel) {
            if (world_.getBlock(x, y, z) != 0)
                world_.setBlock(x, y, z, 0);             // dry up
            static constexpr int DX2[4] = {1,-1,0,0};
            static constexpr int DZ2[4] = {0,0,1,-1};
            for (int d = 0; d < 4; ++d)
                schedule(x + DX2[d], y, z + DZ2[d], now + interval);
            schedule(x, y + 1, z, now + interval);
            return;
        }
        if (want != level) {
            world_.setBlock(x, y, z, fluidState(kind, want));
            level = want;
        }
        (void)fedByFall;
    }

    // --- sideways spread
    if (isSource || level < kMaxRunLevel || level == 8) {
        const int nextLevel =
            level == 8 ? 1 : level + (kind == Kind::Water ? 1 : 2);
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
                    if (nk != kindInt) continue;
                    if (nl <= nextLevel) continue;
                    if (nk < 0) continue;
                }
                // don't flow into the block below being open? vanilla still
                // spreads horizontally around edges; keep simple.
                world_.setBlock(nx, y, nz, fluidState(kind, nextLevel));
                schedule(nx, y, nz, now + interval);
            }
        }
    }
    // --- final solidify check where water meets lava nearby
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
    // re-check self soon ONLY if something changed this pass, so stable
    // pools stop consuming CPU.
    if (worldRevAtEntry != world_.revisionAt(x >> 4, z >> 4))
        schedule(x, y, z, now + interval * 2);
}

} // namespace cppfm
