// Structures implementation: chunk-local piece fillers.
#include "Structures.hpp"
#include "../game/World.hpp"
#include <algorithm>
#include <functional>

namespace cppfm::worldgen {

namespace {

const gen::BlockDef* B(const char* name) {
    return gen::blockByName(name);
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
        if (!overwriteSolid && slot != 0 && slot != 0) return false;
        if (!overwriteSolid && slot != 0) return false;   // keep terrain
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


void StructureGenerator::villagePiece(Chunk& chunk, std::int32_t cx,
                                      std::int32_t cz, std::int32_t ox,
                                      std::int32_t oz, const GroundFn& ground) {
    Writer w{chunk, cx, cz};
    const auto cobble = B("minecraft:cobblestone")->defaultState;
    const auto planks = B("minecraft:oak_planks")->defaultState;
    const auto log    = B("minecraft:oak_log")->defaultState;
    const auto path   = B("minecraft:dirt_path")
                            ? B("minecraft:dirt_path")->defaultState : 0;
    const auto glassP = B("minecraft:glass")->defaultState;
    const auto torchB = B("minecraft:torch")->defaultState;
    const auto farmland = B("minecraft:farmland")
                              ? B("minecraft:farmland")->defaultState : 0;
    const auto wheat  = B("minecraft:wheat")
                            ? B("minecraft:wheat")->defaultState : 0;
    const auto waterS = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:water", {{"level", "0"}}));
    const auto cobbleSlab = B("minecraft:cobblestone_slab")
                                ? B("minecraft:cobblestone_slab")->defaultState
                                : cobble;

    const int baseY = ground(ox + 8, oz + 8);   // village centre height

    // ---- well at origin (3x3 cobble ring, water inside)
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            const int x = ox + dx, z = oz + dz;
            const bool rim = std::abs(dx) == 1 || std::abs(dz) == 1;
            w.set(x, baseY - 1, z, cobble, true);
            w.set(x, baseY, z, rim ? cobble : waterS, true);
            if (!rim) w.set(x, baseY - 2, z, waterS, true);
        }

    // ---- deterministic lot layout on a 5x5 grid of 8-block lots
    for (int lz = -2; lz <= 2; ++lz)
        for (int lx = -2; lx <= 2; ++lx) {
            if (lx == 0 && lz == 0) continue;                 // well lot
            const double r = structHash(seed_, ox + lx, oz + lz, 0xBEEF);
            const std::int32_t bx = ox + lx * 10, bz = oz + lz * 10;
            const int gy = ground(bx + 3, bz + 3);
            // paths along both axes of each lot
            for (int t = 0; t < 6; ++t) {
                w.set(bx + t, gy, bz, path, true);
                w.set(bx, gy, bz + t, path, true);
            }
            if (r < 0.42) {                                   // house
                for (int dy = 0; dy <= 3; ++dy)
                    for (int dzz = 0; dzz < 5; ++dzz)
                        for (int dxx = 0; dxx < 5; ++dxx) {
                            const bool wall =
                                dxx == 0 || dxx == 4 || dzz == 0 || dzz == 4 ||
                                dy == 3 || dy == 0;
                            if (!wall) continue;
                            const std::uint16_t mat =
                                dy == 0 || dy == 3 ? log : planks;
                            w.set(bx + dxx, gy + 1 + dy, bz + dzz, mat);
                        }
                // door gap + windows + torch inside
                w.set(bx + 2, gy + 1, bz, 0, true);
                w.set(bx + 2, gy + 2, bz, 0, true);
                w.set(bx, gy + 2, bz + 2, glassP);
                w.set(bx + 4, gy + 2, bz + 2, glassP);
                w.set(bx + 2, gy + 1, bz + 2, torchB, true);
            } else if (r < 0.70) {                            // farm plot
                for (int dzz = 1; dzz < 6; ++dzz)
                    for (int dxx = 1; dxx < 7; ++dxx) {
                        const bool waterChannel = dxx == 4 && dzz == 3;
                        w.set(bx + dxx, gy, bz + dzz,
                              waterChannel ? waterS : farmland, true);
                        if (!waterChannel) w.set(bx + dxx, gy + 1, bz + dzz, wheat);
                    }
            } else if (r < 0.82) {                            // lamp post
                w.set(bx + 2, gy + 1, bz + 2, B("minecraft:fence") ?
                          B("minecraft:fence")->defaultState : log);
                w.set(bx + 2, gy + 2, bz + 2, B("minecraft:fence") ?
                          B("minecraft:fence")->defaultState : log);
                w.set(bx + 2, gy + 3, bz + 2, torchB, true);
            }
        }
}

void StructureGenerator::pyramidPiece(Chunk& chunk, std::int32_t cx,
                                      std::int32_t cz, std::int32_t ox,
                                      std::int32_t oz, const GroundFn& ground) {
    Writer w{chunk, cx, cz};
    const auto sandstone = B("minecraft:sandstone")->defaultState;
    const auto chiseled  = B("minecraft:chiseled_sandstone")
                               ? B("minecraft:chiseled_sandstone")->defaultState
                               : sandstone;
    const int baseY = ground(ox + 8, oz + 8);
    // stepped pyramid, half-size 9
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
    // hidden chamber
    w.box(ox + 7, baseY, oz + 7, ox + 11, baseY + 1, oz + 11,
          B("minecraft:air")->defaultState, true);
}

void StructureGenerator::outpostPiece(Chunk& chunk, std::int32_t cx,
                                      std::int32_t cz, std::int32_t ox,
                                      std::int32_t oz, const GroundFn& ground) {
    Writer w{chunk, cx, cz};
    const auto darkLog = B("minecraft:dark_oak_log")->defaultState;
    const auto darkPlanks = B("minecraft:dark_oak_planks")->defaultState;
    const int baseY = ground(ox + 4, oz + 4);
    // watchtower: 5x5 platform with tall posts
    for (int dxx = 0; dxx < 5; ++dxx)
        for (int dzz = 0; dzz < 5; ++dzz) {
            const bool corner = (dxx % 4 == 0) && (dzz % 4 == 0);
            for (int h = 0; h <= 10; ++h)
                if (corner) w.set(ox + dxx, baseY + h, oz + dzz, darkLog, true);
            w.set(ox + dxx, baseY + 10, oz + dzz, darkPlanks, true);
        }
    w.set(ox + 2, baseY + 11, oz + 2, B("minecraft:torch")->defaultState, true);
}

void StructureGenerator::generateChunk(Chunk& chunk, std::int32_t cx,
                                       std::int32_t cz, const GroundFn& ground) {
    for (const auto& s : structureSets()) {
        const StructureAt at = structureAtChunk(s, seed_, cx, cz);
        if (!at.present) continue;
        // biome gate using the climate source at the origin
        if (!s.biomes.empty()) {
            const std::string& picked = biomes_->sample(at.originX + 8, 63,
                                                        at.originZ + 8);
            bool ok = false;
            for (auto* want : s.biomes)
                if (picked.find(want) != std::string::npos) { ok = true; break; }
            if (!ok) continue;
        }
        const std::string name = s.name;
        if (name.find("village") != std::string::npos)
            villagePiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("desert_pyramid") != std::string::npos)
            pyramidPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("pillager_outpost") != std::string::npos)
            outpostPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else continue;                                    // others: gated later
    }
}

} // namespace cppfm::worldgen
