// sniffer/armadillo/breeze/creaking/bogged/phantom).
//
// Each row is the "interval / range / magnitude" triple from docs/SPEC_GAMEPLAY.md
// G-06, mirrored from the live goal implementation in AiBrain.cpp (cited per
// row) plus the vanilla reference (wiki/Yarn). `status` marks how much of the
// vanilla behavior the goal actually covers — partial rows stay honest and
// must NOT be upgraded to full without implementing the missing piece.
#pragma once
#include <cstddef>
#include "Entities.hpp"

namespace cppfm {

struct MobBehaviorSpec {
    MobKind kind;
    const char* name;
    int intervalTicks;    // characteristic action interval (ticks)
    double rangeBlocks;   // perception / effect range (blocks)
    double magnitude;     // effect amount (damage / amplifier / count)
    const char* status;   // "full" | "partial:<what is missing>"
    const char* ref;      // vanilla citation (wiki/Yarn)
};

inline const MobBehaviorSpec* mobBehaviorSpec(MobKind k) {
    // Values mirror the live goals (AiBrain.cpp) — single source of truth for
    // the two wired goals (witch/guardian read interval/range from here).
    static const MobBehaviorSpec kTable[] = {
        {MobKind::Witch, "witch", 40, 16.0, 6.0,
         "partial:heal-at-low-hp only, no per-distance potion choice",
         "wiki Witch: drinks fire-res/swiftness/heal, throws harming/poison/slowness/weakness by range"},
        {MobKind::Guardian, "guardian", 60, 15.0, 6.0,
         "partial:beam damage 6 (elder 8) applied instantly, no 2s charge-up visual",
         "wiki Guardian: beam charge ~2s then 6 DPS laser + thorns"},
        {MobKind::ElderGuardian, "elder_guardian", 60, 50.0, 3.0,
         "partial:beam shares guardian path; Mining Fatigue III sphere not applied",
         "wiki Elder Guardian: Mining Fatigue III within 50 blocks for 60s"},
        {MobKind::Strider, "strider", 40, 0.0, 1.0,
         "partial:shiver-off-lava 40t + saddle steer; cold-biome/rain shiver not gated",
         "wiki Strider: shivers outside lava, saddle-controlled, cold damage in snow"},
        {MobKind::Frog, "frog", 40, 6.0, 1.0,
         "partial:tongue eats small slime within 6 (eat dist 1.5); magma-cube froglight missing (drops slime_ball)",
         "wiki Frog: tongue eats small slimes/magma cubes, magma cube -> froglight"},
        {MobKind::Camel, "camel", 55, 12.0, 2.0,
         "partial:dash 4-12 blocks every 55t; 2-seat riding not implemented",
         "wiki Camel: dash with cooldown, seats 2 players, 1.5-block step"},
        {MobKind::Sniffer, "sniffer", 120, 0.0, 1.0,
         "partial:sniff cadence 120t, torchflower-seed dig 1/3; scent-following path missing",
         "wiki Sniffer: sniffs then digs torchflower/pitcher seeds"},
        {MobKind::Armadillo, "armadillo", 60, 5.0, 1.0,
         "full:roll-up on danger (TTL 60t, scan 5t), unroll in water/safety",
         "wiki Armadillo: rolls up vs undead/sprinting players, scute shed"},
        {MobKind::Breeze, "breeze", 32, 16.0, 1.0,
         "full:wind-charge every 32t, jump (min dist 4) every 40t",
         "wiki Breeze: wind-charge deflectable, long jumps between platforms"},
        {MobKind::Creaking, "creaking", 20, 24.0, 1.0,
         "partial:gaze-freeze + heart link transient; daylight dismantle missing",
         "wiki Creaking: moves only unseen, bound to creaking heart, dismantles by day"},
        {MobKind::Bogged, "bogged", 60, 16.0, 1.0,
         "partial:poison-arrow shot; 1/6 shear-to-mushroom drop missing",
         "wiki Bogged: skeleton variant firing poison tipped arrows"},
        {MobKind::Phantom, "phantom", 200, 12.0, 1.0,
         "partial:orbit r=12+size%8 at +12, swoop every 200t; 3-day insomnia gate missing",
         "wiki Phantom: spawns after 3+ insomniac nights, swoops, burns at dawn"},
    };
    for (auto& s : kTable) if (s.kind == k) return &s;
    return nullptr;
}

} // namespace cppfm
