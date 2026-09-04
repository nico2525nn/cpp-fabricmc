// MobBehaviorSpec — plan49 G-06: the immutable, per-species behavior contract
// used by the live goals for the 12 previously-unmapped species.
//
// The original interval/range/magnitude triple remains the public evidence
// contract from docs/SPEC_GAMEPLAY.md. The accessors below give that triple a
// precise meaning for executable code:
//   * action interval: the characteristic cadence of the primary action;
//   * action range: the horizontal perception/effect range. A zero (or an
//     invalid) range means that the action has no spatial gate, not "range 0";
//   * action magnitude: the primary row-specific effect amount (damage,
//     amplifier, count, etc.). It must not be reused for a different effect
//     on the same mob (for example, Elder Guardian's 3-level effect versus its
//     separate 8-damage beam branch).
//
// The extra values are small live-goal tuning parameters. They keep timing,
// thresholds, and distinct secondary branches in this descriptor without
// turning it into a polymorphic runtime behavior object. `status` continues
// to describe coverage honestly; a partial row must not be upgraded merely
// because its parameters are wired.
#pragma once
#include <cmath>
#include "Entities.hpp"

namespace cppfm {

struct MobBehaviorSpec {
    MobKind kind;
    const char* name;
    int intervalTicks;    // primary action interval (ticks)
    double rangeBlocks;   // primary action/perception range (blocks)
    double magnitude;     // primary action effect amount (damage/amplifier/count)
    const char* status;   // "full" | "partial:<what is missing>"
    const char* ref;      // vanilla citation (wiki/Yarn)

    // Optional live-goal tuning. Zero uses the primary value where a timing
    // fallback is meaningful; independent TTL, random, distance, and effect
    // values use zero as "not configured". Their accessors remain safe for
    // callers that need a divisor or modulus.
    int cooldownTicks = 0;                 // primary state/action cooldown
    int secondaryCooldownTicks = 0;        // secondary goal cooldown
    int scanIntervalTicks = 0;             // sensor/polling cadence
    int alertDurationTicks = 0;            // danger/alert TTL; zero disables it
    int chanceDenominator = 0;             // optional random 1/N gate; zero means no gate
    int variantModulo = 0;                 // bounded variant offset; zero means no offset
    double thresholdBlocks = 0.0;          // primary goal-specific distance threshold
    double secondaryThresholdBlocks = 0.0; // second goal-specific threshold
    double auxiliaryRangeBlocks = 0.0;     // special range, for example gaze range
    double gazeMinDistanceBlocks = 0.0;
    double gazeAngleDegrees = 0.0;
    double secondaryMagnitude = 0.0;       // distinct secondary effect amount
    double auxiliaryOffsetBlocks = 0.0;
    double altitudeFloorBlocks = 0.0;       // optional movement floor

    int actionInterval() const noexcept {
        return intervalTicks > 0 ? intervalTicks : 1;
    }
    double actionRange() const noexcept {
        return std::isfinite(rangeBlocks) && rangeBlocks > 0.0 ? rangeBlocks : 0.0;
    }
    double actionRangeSquared() const noexcept {
        const double range = actionRange();
        return range * range;
    }
    double actionMagnitude() const noexcept {
        return std::isfinite(magnitude) && magnitude >= 0.0 ? magnitude : 0.0;
    }
    int actionCooldown() const noexcept {
        return cooldownTicks > 0 ? cooldownTicks : actionInterval();
    }
    int secondaryCooldown() const noexcept {
        return secondaryCooldownTicks > 0 ? secondaryCooldownTicks : actionCooldown();
    }
    int scanInterval() const noexcept {
        return scanIntervalTicks > 0 ? scanIntervalTicks : actionInterval();
    }
    int alertDuration() const noexcept {
        return alertDurationTicks > 0 ? alertDurationTicks : 0;
    }
    int randomDenominator() const noexcept {
        return chanceDenominator > 0 ? chanceDenominator : 1;
    }
    int variantRangeModulo() const noexcept {
        return variantModulo > 0 ? variantModulo : 1;
    }
    double actionThreshold() const noexcept {
        return std::isfinite(thresholdBlocks) && thresholdBlocks > 0.0 ? thresholdBlocks : 0.0;
    }
    double secondaryThreshold() const noexcept {
        return std::isfinite(secondaryThresholdBlocks) && secondaryThresholdBlocks > 0.0
            ? secondaryThresholdBlocks : 0.0;
    }
    double auxiliaryRange() const noexcept {
        return std::isfinite(auxiliaryRangeBlocks) && auxiliaryRangeBlocks > 0.0
            ? auxiliaryRangeBlocks : 0.0;
    }
    double gazeMinimumDistance() const noexcept {
        return std::isfinite(gazeMinDistanceBlocks) && gazeMinDistanceBlocks > 0.0
            ? gazeMinDistanceBlocks : 0.0;
    }
    double gazeAngle() const noexcept {
        return std::isfinite(gazeAngleDegrees) && gazeAngleDegrees > 0.0
            ? gazeAngleDegrees : 0.0;
    }
    double secondaryActionMagnitude() const noexcept {
        return std::isfinite(secondaryMagnitude) && secondaryMagnitude >= 0.0
            ? secondaryMagnitude : 0.0;
    }
    double auxiliaryOffset() const noexcept {
        return std::isfinite(auxiliaryOffsetBlocks) ? auxiliaryOffsetBlocks : 0.0;
    }
    double altitudeFloor() const noexcept {
        return std::isfinite(altitudeFloorBlocks) && altitudeFloorBlocks > 0.0
            ? altitudeFloorBlocks : 0.0;
    }

    bool withinActionRange(double distance) const noexcept {
        if (!std::isfinite(distance) || distance < 0.0) return false;
        const double range = actionRange();
        return range == 0.0 || distance <= range;
    }
    bool withinActionRangeSquared(double distanceSquared) const noexcept {
        if (!std::isfinite(distanceSquared) || distanceSquared < 0.0) return false;
        const double range = actionRange();
        return range == 0.0 || distanceSquared <= range * range;
    }
    bool withinAuxiliaryRange(double distance) const noexcept {
        if (!std::isfinite(distance) || distance < 0.0) return false;
        const double range = auxiliaryRange();
        return range != 0.0 && distance <= range;
    }
    bool withinAuxiliaryRangeSquared(double distanceSquared) const noexcept {
        if (!std::isfinite(distanceSquared) || distanceSquared < 0.0) return false;
        const double range = auxiliaryRange();
        return range != 0.0 && distanceSquared <= range * range;
    }
    bool beyondActionThreshold(double distance) const noexcept {
        if (!std::isfinite(distance) || distance < 0.0) return false;
        const double threshold = actionThreshold();
        return threshold == 0.0 || distance >= threshold;
    }
    bool withinSecondaryThresholdSquared(double distanceSquared) const noexcept {
        if (!std::isfinite(distanceSquared) || distanceSquared < 0.0) return false;
        const double threshold = secondaryThreshold();
        return threshold != 0.0 && distanceSquared < threshold * threshold;
    }
};

inline const MobBehaviorSpec* mobBehaviorSpec(MobKind k) {
    // Values mirror the live goals (AiBrain.cpp). Goal code must use the
    // accessors rather than reintroducing literals for these parameters.
    static const MobBehaviorSpec kTable[] = {
        {
            .kind = MobKind::Witch, .name = "witch", .intervalTicks = 40,
            .rangeBlocks = 16.0, .magnitude = 6.0,
            .status = "partial:heal-at-low-hp only, no per-distance potion choice",
            .ref = "wiki Witch: drinks fire-res/swiftness/heal, throws harming/poison/slowness/weakness by range",
            .cooldownTicks = 40,
        },
        {
            .kind = MobKind::Guardian, .name = "guardian", .intervalTicks = 60,
            .rangeBlocks = 15.0, .magnitude = 6.0,
            .status = "partial:beam damage 6 (elder 8) applied instantly, no 2s charge-up visual",
            .ref = "wiki Guardian: beam charge ~2s then 6 DPS laser + thorns",
            .cooldownTicks = 60, .secondaryMagnitude = 8.0,
        },
        {
            .kind = MobKind::ElderGuardian, .name = "elder_guardian", .intervalTicks = 60,
            .rangeBlocks = 50.0, .magnitude = 3.0,
            .status = "partial:beam shares guardian path; Mining Fatigue III sphere not applied",
            .ref = "wiki Elder Guardian: Mining Fatigue III within 50 blocks for 60s",
            .cooldownTicks = 60, .secondaryMagnitude = 8.0,
        },
        {
            .kind = MobKind::Strider, .name = "strider", .intervalTicks = 40,
            .rangeBlocks = 0.0, .magnitude = 1.0,
            .status = "partial:shiver-off-lava 40t + saddle steer; cold-biome/rain shiver not gated",
            .ref = "wiki Strider: shivers outside lava, saddle-controlled, cold damage in snow",
            .cooldownTicks = 40,
        },
        {
            .kind = MobKind::Frog, .name = "frog", .intervalTicks = 40,
            .rangeBlocks = 6.0, .magnitude = 1.0,
            .status = "partial:tongue eats small slime within 6 (eat dist 1.5); magma-cube froglight missing (drops slime_ball)",
            .ref = "wiki Frog: tongue eats small slimes/magma cubes, magma cube -> froglight",
            .cooldownTicks = 40, .chanceDenominator = 40, .thresholdBlocks = 1.5,
        },
        {
            .kind = MobKind::Camel, .name = "camel", .intervalTicks = 55,
            .rangeBlocks = 12.0, .magnitude = 2.0,
            .status = "partial:dash 4-12 blocks every 55t; 2-seat riding not implemented",
            .ref = "wiki Camel: dash with cooldown, seats 2 players, 1.5-block step",
            .cooldownTicks = 55, .thresholdBlocks = 4.0,
        },
        {
            .kind = MobKind::Sniffer, .name = "sniffer", .intervalTicks = 120,
            .rangeBlocks = 0.0, .magnitude = 1.0,
            .status = "partial:sniff cadence 120t, torchflower-seed dig 1/3; scent-following path missing",
            .ref = "wiki Sniffer: sniffs then digs torchflower/pitcher seeds",
            .cooldownTicks = 120, .chanceDenominator = 3,
        },
        {
            .kind = MobKind::Armadillo, .name = "armadillo", .intervalTicks = 60,
            .rangeBlocks = 5.0, .magnitude = 1.0,
            .status = "full:roll-up on danger (TTL 60t, scan 5t), unroll in water/safety",
            .ref = "wiki Armadillo: rolls up vs undead/sprinting players, scute shed",
            .cooldownTicks = 60, .secondaryCooldownTicks = 20,
            .scanIntervalTicks = 5, .alertDurationTicks = 80,
            .secondaryThresholdBlocks = 3.0,
        },
        {
            .kind = MobKind::Breeze, .name = "breeze", .intervalTicks = 32,
            .rangeBlocks = 16.0, .magnitude = 1.0,
            .status = "full:wind-charge every 32t, jump (min dist 4) every 40t",
            .ref = "wiki Breeze: wind-charge deflectable, long jumps between platforms",
            .cooldownTicks = 32, .secondaryCooldownTicks = 40,
            .thresholdBlocks = 4.0,
        },
        {
            .kind = MobKind::Creaking, .name = "creaking", .intervalTicks = 20,
            .rangeBlocks = 24.0, .magnitude = 1.0,
            .status = "partial:gaze-freeze + heart link transient; daylight dismantle missing",
            .ref = "wiki Creaking: moves only unseen, bound to creaking heart, dismantles by day",
            .cooldownTicks = 20, .thresholdBlocks = 1.9,
            .secondaryThresholdBlocks = 12.0, .auxiliaryRangeBlocks = 32.0,
            .gazeMinDistanceBlocks = 0.1, .gazeAngleDegrees = 60.0,
        },
        {
            .kind = MobKind::Bogged, .name = "bogged", .intervalTicks = 60,
            .rangeBlocks = 16.0, .magnitude = 1.0,
            .status = "partial:poison-arrow shot; 1/6 shear-to-mushroom drop missing",
            .ref = "wiki Bogged: skeleton variant firing poison tipped arrows",
            .cooldownTicks = 40, .thresholdBlocks = 5.0,
        },
        {
            .kind = MobKind::Phantom, .name = "phantom", .intervalTicks = 200,
            .rangeBlocks = 12.0, .magnitude = 1.0,
            .status = "partial:orbit r=12+size%8 at +12, swoop every 200t; 3-day insomnia gate missing",
            .ref = "wiki Phantom: spawns after 3+ insomniac nights, swoops, burns at dawn",
            .cooldownTicks = 200, .variantModulo = 8, .thresholdBlocks = 1.5,
            .auxiliaryOffsetBlocks = 12.0, .altitudeFloorBlocks = 60.0,
        },
    };
    for (const auto& s : kTable) if (s.kind == k) return &s;
    return nullptr;
}

} // namespace cppfm
