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


void StructureGenerator::villageHouse(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                     std::int32_t bx, std::int32_t bz, int gy) {
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
void StructureGenerator::villageFarm(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                    std::int32_t bx, std::int32_t bz, int gy) {
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
void StructureGenerator::villageChurch(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                      std::int32_t bx, std::int32_t bz, int gy) {
    Writer w{chunk, cx, cz};
    const auto cobble = B("minecraft:cobblestone")->defaultState;
    const auto glassP = B("minecraft:glass")->defaultState;
    const auto torchB = B("minecraft:torch")->defaultState;
    // 7x7 church with higher walls
    for (int dy=0; dy<=5; ++dy) for (int dz=0; dz<7; ++dz) for (int dx=0; dx<7; ++dx){
        const bool wall = dx==0||dx==6||dz==0||dz==6||dy==5||dy==0;
        if (!wall) continue;
        w.set(bx+dx, gy+1+dy, bz+dz, cobble);
    }
    w.set(bx+3, gy+1, bz, 0, true); w.set(bx+3, gy+2, bz, 0, true);
    w.set(bx+3, gy+3, bz+3, torchB, true);
    w.set(bx, gy+3, bz+3, glassP); w.set(bx+6, gy+3, bz+3, glassP);
    // steeple
    w.set(bx+3, gy+7, bz+3, cobble, true);
    w.set(bx+3, gy+8, bz+3, torchB, true);
}
void StructureGenerator::villageJigsaw(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                      std::int32_t ox, std::int32_t oz, int depth,
                                      const GroundFn& ground) {
    if (depth <= 0) return;
    const double r = structHash(seed_, ox, oz, 0xBEEF + depth*0x9E37ULL);
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
    // recurse to 4 neighbours
    const int step = 12;
    const std::int32_t nx[4]={ox+step, ox-step, ox, ox};
    const std::int32_t nz[4]={oz, oz, oz+step, oz-step};
    for (int i=0;i<4;++i) if (depth>1) {
        // avoid overlapping centre well area
        if (std::abs(nx[i])<8 && std::abs(nz[i])<8) continue;
        // probabilistic branch
        if (structHash(seed_, nx[i], nz[i], 0xCAFE) < 0.7)
            villageJigsaw(chunk, cx, cz, nx[i], nz[i], depth-1, ground);
    }
}
void StructureGenerator::villagePiece(Chunk& chunk, std::int32_t cx,
                                      std::int32_t cz, std::int32_t ox,
                                      std::int32_t oz, const GroundFn& ground) {
    Writer w{chunk, cx, cz};
    const auto cobble = B("minecraft:cobblestone")->defaultState;
    const auto waterS = static_cast<std::uint16_t>(gen::stateWithPropsList("minecraft:water", {{"level", "0"}}));
    const int baseY = ground(ox + 8, oz + 8);
    // ---- well at origin (3x3 cobble ring, water inside) — kept but now via Jigsaw root
    for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
        const int x = ox + dx, z = oz + dz;
        const bool rim = std::abs(dx)==1 || std::abs(dz)==1;
        w.set(x, baseY-1, z, cobble, true);
        w.set(x, baseY, z, rim ? cobble : waterS, true);
        if (!rim) w.set(x, baseY-2, z, waterS, true);
    }
    // Jigsaw-like recursive placement: 5 initial branches around well, depth 2-3
    const int branches[4][2]={{10,0},{-10,0},{0,10},{0,-10}};
    for (auto &b : branches) {
        villageJigsaw(chunk, cx, cz, ox + b[0], oz + b[1], 3, ground);
    }
    // additional scattered lots for density (fallback)
    for (int lz=-2; lz<=2; ++lz) for (int lx=-2; lx<=2; ++lx){
        if (lx==0 && lz==0) continue;
        if (std::abs(lx)==1 && std::abs(lz)==1) continue; // already covered by jigsaw
        double r = structHash(seed_, ox+lx*7, oz+lz*7, 0x5A17C);
        if (r < 0.15) {
            villageJigsaw(chunk, cx, cz, ox+lx*10, oz+lz*10, 2, ground);
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

void StructureGenerator::jungleTemplePiece(Chunk& chunk, std::int32_t cx,
                                           std::int32_t cz, std::int32_t ox,
                                           std::int32_t oz, const GroundFn& ground) {
    Writer w{chunk, cx, cz};
    const auto mossy = B("minecraft:mossy_cobblestone") ? B("minecraft:mossy_cobblestone")->defaultState
                    : B("minecraft:cobblestone")->defaultState;
    const auto cobble = B("minecraft:cobblestone")->defaultState;
    const auto chiseled = B("minecraft:chiseled_stone_bricks") ? B("minecraft:chiseled_stone_bricks")->defaultState : cobble;
    const int baseY = ground(ox + 6, oz + 6);
    // simple 12x12 temple base with walls 4 high, similar to pyramid but smaller and mossy
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
    // inner chamber door and treasure
    w.set(ox + 6, baseY + 1, oz, 0, true);
    w.set(ox + 6, baseY + 2, oz, 0, true);
    w.box(ox + 4, baseY + 1, oz + 4, ox + 8, baseY + 2, oz + 8, chiseled, true);
    w.set(ox + 6, baseY + 1, oz + 6, B("minecraft:chest") ? B("minecraft:chest")->defaultState : cobble, true);
}

void StructureGenerator::iglooPiece(Chunk& chunk, std::int32_t cx,
                                    std::int32_t cz, std::int32_t ox,
                                    std::int32_t oz, const GroundFn& ground) {
    Writer w{chunk, cx, cz};
    const auto snow = B("minecraft:snow_block")->defaultState;
    const auto ice = B("minecraft:ice") ? B("minecraft:ice")->defaultState : snow;
    const int baseY = ground(ox + 4, oz + 4);
    // 7x7 dome simplified as 5 high hollow box with snow
    for (int dy = 0; dy < 4; ++dy) {
        for (int dz = -3; dz <= 3; ++dz)
            for (int dx = -3; dx <= 3; ++dx) {
                const bool shell = std::abs(dx) == 3 || std::abs(dz) == 3 || dy == 3;
                const bool interior = !shell && dy > 0;
                if (shell) w.set(ox + dx + 4, baseY + 1 + dy, oz + dz + 4, snow, true);
                if (interior && dy == 1) w.set(ox + dx + 4, baseY + 1 + dy, oz + dz + 4, 0, true);
            }
    }
    // door hole south side
    w.set(ox + 4, baseY + 1, oz + 7, 0, true);
    w.set(ox + 4, baseY + 2, oz + 7, 0, true);
    // interior features: crafting + furnace + bed
    w.set(ox + 5, baseY + 1, oz + 5, B("minecraft:crafting_table") ? B("minecraft:crafting_table")->defaultState : snow, true);
    w.set(ox + 3, baseY + 1, oz + 5, B("minecraft:furnace") ? B("minecraft:furnace")->defaultState : snow, true);
    w.set(ox + 4, baseY + 1, oz + 3, ice, true);
}

void StructureGenerator::swampHutPiece(Chunk& chunk, std::int32_t cx,
                                       std::int32_t cz, std::int32_t ox,
                                       std::int32_t oz, const GroundFn& ground) {
    Writer w{chunk, cx, cz};
    const auto planks = B("minecraft:spruce_planks") ? B("minecraft:spruce_planks")->defaultState
                        : B("minecraft:oak_planks")->defaultState;
    const auto log = B("minecraft:oak_log")->defaultState;
    const auto cauldron = B("minecraft:cauldron") ? B("minecraft:cauldron")->defaultState : planks;
    const int baseY = ground(ox + 4, oz + 4);
    // stilts
    for (int dx = 0; dx < 7; ++dx)
        for (int dz = 0; dz < 7; ++dz) {
            const bool isPost = (dx % 6 == 0 && dz % 6 == 0);
            if (isPost) {
                for (int h = -2; h <= 0; ++h) w.set(ox + dx, baseY + h, oz + dz, log, true);
            }
        }
    // platform 7x7
    for (int dx = 0; dx < 7; ++dx)
        for (int dz = 0; dz < 7; ++dz) {
            w.set(ox + dx, baseY + 1, oz + dz, planks, true);
        }
    // walls 3 high, with door gap front and windows
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
    // carve door and windows
    w.set(ox + 3, baseY + 2, oz, 0, true);
    w.set(ox + 3, baseY + 3, oz, 0, true);
    w.set(ox, baseY + 3, oz + 3, B("minecraft:glass") ? B("minecraft:glass")->defaultState : 0, true);
    // interior cauldron + crafting
    w.set(ox + 2, baseY + 2, oz + 2, cauldron, true);
    w.set(ox + 4, baseY + 2, oz + 4, B("minecraft:crafting_table") ? B("minecraft:crafting_table")->defaultState : planks, true);
}

void StructureGenerator::strongholdPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                        std::int32_t ox, std::int32_t oz, const GroundFn& ground) {
    Writer w{chunk, cx, cz};
    const auto stoneBricks = B("minecraft:stone_bricks") ? B("minecraft:stone_bricks")->defaultState : B("minecraft:cobblestone")->defaultState;
    const auto mossy = B("minecraft:mossy_stone_bricks") ? B("minecraft:mossy_stone_bricks")->defaultState : stoneBricks;
    const auto cracked = B("minecraft:cracked_stone_bricks") ? B("minecraft:cracked_stone_bricks")->defaultState : stoneBricks;
    const auto portalFrame = B("minecraft:end_portal_frame") ? B("minecraft:end_portal_frame")->defaultState : stoneBricks;
    const auto torch = B("minecraft:torch")->defaultState;
    // underground room at y ~ -10 to -5 (or ground-35)
    int surfaceY = ground(ox+1, oz+1);
    int baseY = std::clamp(surfaceY - 30, kMinY+5, 40);
    // Use placer-configured pieces weighting if available, else simple 3x3 room 5x5 outer, 3x3 inner air
    for (int dx=-1; dx<=4; ++dx) for (int dz=-1; dz<=4; ++dz){
        int x = ox+dx, z = oz+dz;
        bool edge = dx==-1||dx==4||dz==-1||dz==4;
        bool corner = (std::abs(dx)==1 && std::abs(dz)==1 && false);
        // floor
        w.set(x, baseY, z, edge ? mossy : stoneBricks, true);
        // walls 3 high
        for (int dy=1; dy<=3; ++dy){
            if (edge) w.set(x, baseY+dy, z, (dx==-1||dx==4||dz==-1||dz==4) ? stoneBricks : stoneBricks, false);
            else if (dx>=0 && dx<=3 && dz>=0 && dz<=3 && dy<=2) {
                // interior air – carve
                w.set(x, baseY+dy, z, 0, true);
            }
        }
        // ceiling
        w.set(x, baseY+4, z, cracked, true);
    }
    // doorway south
    w.set(ox+1, baseY+1, oz+4, 0, true); w.set(ox+1, baseY+2, oz+4, 0, true);
    w.set(ox+2, baseY+1, oz+4, 0, true); w.set(ox+2, baseY+2, oz+4, 0, true);
    // portal frame in centre north wall (decor)
    w.set(ox+1, baseY+1, oz, portalFrame, true);
    w.set(ox+2, baseY+1, oz, portalFrame, true);
    w.set(ox+1, baseY+1, oz+1, torch, true);
}
void StructureGenerator::mineshaftPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                       std::int32_t ox, std::int32_t oz, const GroundFn& ground) {
    Writer w{chunk, cx, cz};
    const auto oakPlanks = B("minecraft:oak_planks")->defaultState;
    const auto oakFence = B("minecraft:oak_fence") ? B("minecraft:oak_fence")->defaultState : oakPlanks;
    const auto rail = B("minecraft:rail") ? B("minecraft:rail")->defaultState : oakPlanks;
    const auto cobweb = B("minecraft:cobweb") ? B("minecraft:cobweb")->defaultState : 0;
    int surfaceY = ground(ox, oz);
    int baseY = std::clamp(surfaceY - 20, kMinY+5, 50);
    // 3-high corridor along X axis, 3 wide
    for (int dx=0; dx<9; ++dx){
        for (int dz=-1; dz<=1; ++dz){
            int x=ox+dx, z=oz+dz;
            bool wall = dz==-1 || dz==1;
            w.set(x, baseY, z, oakPlanks, true); // floor
            if (wall) {
                w.set(x, baseY+1, z, oakFence, false);
                w.set(x, baseY+2, z, oakFence, false);
            } else {
                w.set(x, baseY+1, z, 0, true);
                w.set(x, baseY+2, z, 0, true);
                if (dx%3==0) w.set(x, baseY, z, rail, true);
            }
            w.set(x, baseY+3, z, oakPlanks, true); // ceiling
        }
        if (dx%5==0) {
            int x=ox+dx;
            w.set(x, baseY+1, oz, cobweb);
        }
    }
}
void StructureGenerator::monumentPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                       std::int32_t ox, std::int32_t oz, const GroundFn& ground) {
    Writer w{chunk, cx, cz};
    const auto prismarine = B("minecraft:prismarine") ? B("minecraft:prismarine")->defaultState : B("minecraft:stone_bricks")->defaultState;
    const auto bricks = B("minecraft:prismarine_bricks") ? B("minecraft:prismarine_bricks")->defaultState : prismarine;
    const auto dark = B("minecraft:dark_prismarine") ? B("minecraft:dark_prismarine")->defaultState : bricks;
    const auto lantern = B("minecraft:sea_lantern") ? B("minecraft:sea_lantern")->defaultState : prismarine;
    const auto gold = B("minecraft:gold_block") ? B("minecraft:gold_block")->defaultState : prismarine;
    const auto water = static_cast<std::uint16_t>(gen::stateWithPropsList("minecraft:water", {{"level","0"}}));
    int baseY = 39;
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
                if (dy==1 && dx==28 && dz==28) mat = gold;
                else if (dy<3) mat = dark;
                else if (dy==10 && (dx==28||dz==28)) mat = lantern;
            }
            if (dx>2&&dx<55&&dz>2&&dz<55&& dy>2&&dy<20 && mat==prismarine) mat = water;
            if (mat==0) w.set(wx, py, wz, 0, true);
            else w.set(wx, py, wz, mat, true);
        }
    }
    const auto sponge = B("minecraft:sponge") ? B("minecraft:sponge")->defaultState : prismarine;
    w.set(ox+28, baseY+10, oz+28, sponge, true);
}
void StructureGenerator::mansionPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                      std::int32_t ox, std::int32_t oz, const GroundFn& ground) {
    Writer w{chunk, cx, cz};
    const auto planks = B("minecraft:dark_oak_planks") ? B("minecraft:dark_oak_planks")->defaultState : B("minecraft:oak_planks")->defaultState;
    const auto log = B("minecraft:dark_oak_log") ? B("minecraft:dark_oak_log")->defaultState : planks;
    const auto cobble = B("minecraft:cobblestone")->defaultState;
    const auto chest = B("minecraft:chest") ? B("minecraft:chest")->defaultState : cobble;
    int surfaceY = ground(ox+20, oz+20);
    int baseY = std::clamp(surfaceY+1, 70, 85);
    for (int dx=0; dx<40; ++dx) for (int dz=0; dz<40; ++dz) {
        int wx = ox+dx, wz = oz+dz;
        bool edge = dx==0||dx==39||dz==0||dz==39;
        w.set(wx, baseY, wz, planks, true);
        w.set(wx, baseY+7, wz, planks, true);
        if (edge) {
            for (int dy=1; dy<=6; ++dy) w.set(wx, baseY+dy, wz, cobble, true);
            for (int dy=8; dy<=12; ++dy) w.set(wx, baseY+dy, wz, planks, true);
        }
        bool corner = (dx==0||dx==39) && (dz==0||dz==39);
        if (corner) for (int dy=1; dy<=12; ++dy) w.set(wx, baseY+dy, wz, log, true);
    }
    for (int dx=10; dx<30; dx+=10) for (int dz=0; dz<40; ++dz) {
        for (int dy=1; dy<=6; ++dy) w.set(ox+dx, baseY+dy, oz+dz, planks, true);
        for (int dy=8; dy<=12; ++dy) w.set(ox+dx, baseY+dy, oz+dz, planks, true);
    }
    for (int dz=10; dz<30; dz+=10) for (int dx=0; dx<40; ++dx) {
        for (int dy=1; dy<=6; ++dy) w.set(ox+dx, baseY+dy, oz+dz, planks, true);
        for (int dy=8; dy<=12; ++dy) w.set(ox+dx, baseY+dy, oz+dz, planks, true);
    }
    w.set(ox+20, baseY+1, oz, 0, true); w.set(ox+20, baseY+2, oz, 0, true);
    w.set(ox+20, baseY+8, oz+10, 0, true); w.set(ox+20, baseY+9, oz+10, 0, true);
    w.set(ox+5, baseY+1, oz+5, chest, true);
    w.set(ox+35, baseY+1, oz+35, chest, true);
}
void StructureGenerator::trialChambersPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                      std::int32_t ox, std::int32_t oz, const GroundFn& ground) {
    Writer w{chunk, cx, cz};
    const auto tuff = B("minecraft:tuff") ? B("minecraft:tuff")->defaultState : B("minecraft:stone_bricks")->defaultState;
    const auto tuffBricks = B("minecraft:tuff_bricks") ? B("minecraft:tuff_bricks")->defaultState : tuff;
    const auto chiseledTuff = B("minecraft:chiseled_tuff") ? B("minecraft:chiseled_tuff")->defaultState : tuffBricks;
    const auto waxedChiseled = B("minecraft:waxed_chiseled_copper") ? B("minecraft:waxed_chiseled_copper")->defaultState : chiseledTuff;
    const auto copperBulb = B("minecraft:copper_bulb") ? B("minecraft:copper_bulb")->defaultState : tuffBricks;
    const auto waxedBulb = B("minecraft:waxed_copper_bulb") ? B("minecraft:waxed_copper_bulb")->defaultState : copperBulb;
    const auto spawner = B("minecraft:trial_spawner") ? B("minecraft:trial_spawner")->defaultState : tuffBricks;
    const auto vault = B("minecraft:vault") ? B("minecraft:vault")->defaultState : tuffBricks;
    const auto dispenser = B("minecraft:dispenser") ? B("minecraft:dispenser")->defaultState : tuff;
    const auto polishedTuff = B("minecraft:polished_tuff") ? B("minecraft:polished_tuff")->defaultState : tuffBricks;
    int surfaceY = ground(ox+8, oz+8);
    int baseY = std::clamp(surfaceY - 30, kMinY+5, 20);
    auto chamberAt = [&](int cxo, int czo, int kind) {
        int sz = 18; if (kind==2) sz=14; if (kind==3) sz=22;
        for (int dx=0; dx<sz; ++dx) for (int dz=0; dz<sz; ++dz) {
            int wx=cxo+dx, wz=czo+dz;
            bool edge=dx==0||dx==sz-1||dz==0||dz==sz-1;
            w.set(wx, baseY, wz, tuffBricks, true);
            w.set(wx, baseY+6, wz, tuffBricks, true);
            if (edge){ for(int dy=1;dy<=5;++dy) w.set(wx, baseY+dy,wz,tuff,true); if((dx%6==0||dz%6==0)&&dx%3==0) w.set(wx,baseY+1,wz,chiseledTuff,true); if(dx==0&&dz%4==0) w.set(wx,baseY+2,wz,waxedChiseled,true); }
            else if(dx%7==3&&dz%7==3){ for(int dy=1;dy<=3;++dy) w.set(wx,baseY+dy,wz,0,true); if(kind==1) w.set(wx,baseY+1,wz,spawner,true); else if(kind==2) w.set(wx,baseY+1,wz,vault,true); else w.set(wx,baseY+1,wz,spawner,true); if(dx==sz/2&&dz==sz/2) w.set(wx,baseY+3,wz,waxedBulb,true); }
            else for(int dy=1;dy<=5;++dy) (void)w.set(wx,baseY+dy,wz,0,false);
        }
        w.set(cxo+2, baseY+1, czo+2, dispenser, true);
        w.set(cxo+sz-3, baseY+1, czo+sz-3, copperBulb, true);
    };
    auto straightCorridor = [&](int cxo,int czo,int len,int dir){
        for(int i=0;i<len;++i){ int wx=cxo+(dir==0?i:dir==1?-i:0); int wz=czo+(dir==2?i:dir==3?-i:0);
            for(int dw=-1;dw<=1;++dw){ int px=wx+(dir>=2?dw:0); int pz=wz+(dir<2?dw:0);
                w.set(px,baseY,pz,tuffBricks,true); w.set(px,baseY+4,pz,tuffBricks,true);
                if(std::abs(dw)==1) for(int dy=1;dy<=3;++dy) w.set(px,baseY+dy,pz,tuff,true); else for(int dy=1;dy<=3;++dy) w.set(px,baseY+dy,pz,0,true);
            } if(i%4==0) w.set(wx,baseY+1,wz,copperBulb,true);
        }
    };
    auto intersectionAt = [&](int cxo,int czo){
        for(int dx=-4;dx<=4;++dx) for(int dz=-4;dz<=4;++dz){ int wx=cxo+dx,wz=czo+dz; bool cross=(std::abs(dx)<=1||std::abs(dz)<=1); if(!cross) continue; w.set(wx,baseY,wz,tuffBricks,true); w.set(wx,baseY+5,wz,tuffBricks,true); bool edge=std::abs(dx)==4||std::abs(dz)==4; if(edge) for(int dy=1;dy<=4;++dy) w.set(wx,baseY+dy,wz,tuff,true); else for(int dy=1;dy<=4;++dy) (void)w.set(wx,baseY+dy,wz,0,true); } w.set(cxo,baseY+1,czo,spawner,true); w.set(cxo,baseY+2,czo,waxedBulb,true);
    };
    auto atriumAt = [&](int cxo,int czo){
        for(int dx=-6;dx<=6;++dx) for(int dz=-6;dz<=6;++dz){ int wx=cxo+dx,wz=czo+dz; bool edge=std::abs(dx)==6||std::abs(dz)==6; w.set(wx,baseY,wz,polishedTuff,true); w.set(wx,baseY+7,wz,tuffBricks,true); if(edge) for(int dy=1;dy<=6;++dy) w.set(wx,baseY+dy,wz,tuff,true); else for(int dy=1;dy<=6;++dy) (void)w.set(wx,baseY+dy,wz,0,false); } w.set(cxo,baseY+1,czo,vault,true);
    };
    const double r0 = structHash(seed_, ox, oz, 0xBEEF);
    const double r1 = structHash(seed_, ox, oz, 0xCAFE);
    for(int dx=-2;dx<=2;++dx) for(int dz=-2;dz<=2;++dz){ int wx=ox+dx,wz=oz+dz; bool edge=std::abs(dx)==2||std::abs(dz)==2; w.set(wx,baseY,wz,tuffBricks,true); w.set(wx,baseY+4,wz,tuffBricks,true); if(edge) for(int dy=1;dy<=3;++dy) w.set(wx,baseY+dy,wz,tuff,true); else for(int dy=1;dy<=3;++dy) (void)w.set(wx,baseY+dy,wz,0,false); } w.set(ox,baseY+1,oz,spawner,true);
    straightCorridor(ox+3, oz, 10, 0); straightCorridor(ox-3, oz, 8, 1);
    int chamberKind = int(r0*4)%4; chamberAt(ox+10, oz-9, chamberKind); if(r0>0.5){ int k2=int(r1*4)%4; chamberAt(ox-24, oz+4, k2); }
    intersectionAt(ox, oz+12); if(r1>0.35) atriumAt(ox+18, oz+16); straightCorridor(ox+28, oz-2, 6, 0);
}
void StructureGenerator::endCityPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                      std::int32_t ox, std::int32_t oz, const GroundFn& ground) {
    Writer w{chunk, cx, cz};
    const auto endBricks = B("minecraft:end_stone_bricks") ? B("minecraft:end_stone_bricks")->defaultState : B("minecraft:end_stone")->defaultState;
    const auto purpur = B("minecraft:purpur_block") ? B("minecraft:purpur_block")->defaultState : endBricks;
    int baseY = 70;
    if (ground) baseY = ground(ox+4, oz+4);
    if (baseY < 55) baseY = 70;
    for (int dx=0; dx<9; ++dx) for (int dz=0; dz<9; ++dz) {
        int wx = ox+dx, wz = oz+dz;
        w.set(wx, baseY, wz, endBricks, true);
        for (int dy=1; dy<=12; ++dy) {
            bool wall = dx==0||dx==8||dz==0||dz==8;
            if (!wall) continue;
            w.set(wx, baseY+dy, wz, purpur, true);
        }
    }
    w.set(ox+4, baseY+1, oz+4, B("minecraft:chest")?B("minecraft:chest")->defaultState:endBricks, true);
}

void StructureGenerator::generateChunk(Chunk& chunk, std::int32_t cx,
                                       std::int32_t cz, const GroundFn& ground) {
    if (placer_) {
        // stronghold via placer
        if (auto* pf = placer_->getPlaced("minecraft:stronghold")) {
            std::int32_t oCx, oCz;
            if (placer_->findOrigin(*pf, cx, cz, oCx, oCz)) {
                // also check shouldPlace at origin (frequency)
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
    // fallback stronghold check even if placer missing: deterministic hash <0.002
    {
        double h = structHash(seed_, cx, cz, 0x5354524FULL); // "STR0"
        if (h < 0.002) {
            strongholdPiece(chunk, cx, cz, cx*16, cz*16, ground);
        }
    }
    for (const auto& s : structureSets()) {
        const StructureAt at = structureAtChunk(s, seed_, cx, cz);
        if (!at.present) continue;
        // trial_chambers deep_dark origin reject (vanilla)
        if (std::string(s.name).find("trial_chambers") != std::string::npos || std::string(s.name).find("trial_chamber") != std::string::npos) {
            const std::string& picked = biomes_->sample(at.originX + 8, 63, at.originZ + 8);
            if (picked.find("deep_dark") != std::string::npos) continue;
        }
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
        else if (name.find("trial_chamber") != std::string::npos)
            trialChambersPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("end_city") != std::string::npos)
            endCityPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else if (name.find("mineshaft") != std::string::npos)
            mineshaftPiece(chunk, cx, cz, at.originX, at.originZ, ground);
        else continue;
    }
}

} // namespace cppfm::worldgen
