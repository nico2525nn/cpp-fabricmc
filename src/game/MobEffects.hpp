// MobEffects: vanilla status-effect registry (numeric ids are the jar's
// registration order; mob_effect is not a network-synced registry) and the
// per-entity EffectInstance model with tick handling hooks.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <cmath>
#include <vector>

namespace cppfm {

namespace effects {
enum Id : std::uint8_t {
    Speed = 1, Slowness, Haste, MiningFatigue, Strength, InstantHealth,
    InstantDamage, JumpBoost, Nausea, Regeneration, Resistance, FireResistance,
    WaterBreathing, Invisibility, Blindness, NightVision, Hunger, Weakness,
    Poison, Wither, HealthBoost, Absorption, Saturation, Glowing, Levitation,
    Luck, Unluck, SlowFalling, ConduitPower, DolphinsGrace, BadOmen,
    HeroOfTheVillage, Darkness, TrialOmen, RaidOmen, WindCharged, Weaving,
    Oozing, Infested
};

inline const char* nameOf(std::uint8_t id) {
    switch (id) {
    case Speed: return "minecraft:speed";
    case Slowness: return "minecraft:slowness";
    case Haste: return "minecraft:haste";
    case MiningFatigue: return "minecraft:mining_fatigue";
    case Strength: return "minecraft:strength";
    case InstantHealth: return "minecraft:instant_health";
    case InstantDamage: return "minecraft:instant_damage";
    case JumpBoost: return "minecraft:jump_boost";
    case Nausea: return "minecraft:nausea";
    case Regeneration: return "minecraft:regeneration";
    case Resistance: return "minecraft:resistance";
    case FireResistance: return "minecraft:fire_resistance";
    case WaterBreathing: return "minecraft:water_breathing";
    case Invisibility: return "minecraft:invisibility";
    case Blindness: return "minecraft:blindness";
    case NightVision: return "minecraft:night_vision";
    case Hunger: return "minecraft:hunger";
    case Weakness: return "minecraft:weakness";
    case Poison: return "minecraft:poison";
    case Wither: return "minecraft:wither";
    case HealthBoost: return "minecraft:health_boost";
    case Absorption: return "minecraft:absorption";
    case Saturation: return "minecraft:saturation";
    case Glowing: return "minecraft:glowing";
    case Levitation: return "minecraft:levitation";
    case Luck: return "minecraft:luck";
    case Unluck: return "minecraft:unluck";
    case SlowFalling: return "minecraft:slow_falling";
    case ConduitPower: return "minecraft:conduit_power";
    case DolphinsGrace: return "minecraft:dolphins_grace";
    case BadOmen: return "minecraft:bad_omen";
    case HeroOfTheVillage: return "minecraft:hero_of_the_village";
    case Darkness: return "minecraft:darkness";
    default: return "minecraft:speed";
    }
}

inline const std::unordered_map<std::string, std::uint8_t>& byName() {
    static const std::unordered_map<std::string, std::uint8_t> m = [] {
        std::unordered_map<std::string, std::uint8_t> mm;
        for (std::uint8_t i = 1; i <= Infested; ++i)
            mm.emplace(nameOf(i), i);
        // common short aliases for commands
        mm.emplace("speed", Speed);       mm.emplace("slowness", Slowness);
        mm.emplace("haste", Haste);       mm.emplace("mining_fatigue", MiningFatigue);
        mm.emplace("strength", Strength); mm.emplace("instant_health", InstantHealth);
        mm.emplace("instant_damage", InstantDamage);
        mm.emplace("jump_boost", JumpBoost); mm.emplace("nausea", Nausea);
        mm.emplace("regeneration", Regeneration); mm.emplace("resistance", Resistance);
        mm.emplace("fire_resistance", FireResistance);
        mm.emplace("water_breathing", WaterBreathing);
        mm.emplace("invisibility", Invisibility); mm.emplace("blindness", Blindness);
        mm.emplace("night_vision", NightVision);   mm.emplace("hunger", Hunger);
        mm.emplace("weakness", Weakness);          mm.emplace("poison", Poison);
        mm.emplace("wither", Wither);              mm.emplace("health_boost", HealthBoost);
        mm.emplace("absorption", Absorption);      mm.emplace("saturation", Saturation);
        mm.emplace("glowing", Glowing);            mm.emplace("levitation", Levitation);
        mm.emplace("luck", Luck);                  mm.emplace("unluck", Unluck);
        mm.emplace("slow_falling", SlowFalling);   mm.emplace("darkness", Darkness);
        return mm;
    }();
    return m;
}
} // namespace effects

// One active effect on a living entity.
struct EffectInstance {
    std::uint8_t type = 0;
    std::int8_t amplifier = 0;          // level-1
    std::int32_t durationTicks = 0;
    bool ambient = false;
    bool showParticles = true;
    bool showIcon = true;

    bool expired() const { return durationTicks <= 0 && type != effects::InstantHealth &&
                                   type != effects::InstantDamage; }
};

inline int effectFlags(const EffectInstance& e) {
    int f = 0;
    if (e.ambient) f |= 0x01;
    if (e.showParticles) f |= 0x02;
    if (e.showIcon) f |= 0x04;
    return f;
}

// Attribute modifiers applied by effects (vanilla magnitudes).
inline double speedModifierFor(const std::vector<EffectInstance>& list) {
    double mod = 0;
    for (auto& e : list) {
        if (e.type == effects::Speed) mod += 0.20 * (e.amplifier + 1);
        if (e.type == effects::Slowness) mod -= 0.15 * (e.amplifier + 1);
    }
    return mod;
}
inline double digSpeedMultiplierFor(const std::vector<EffectInstance>& list) {
    double mult = 1.0;
    for (auto& e : list) {
        if (e.type == effects::Haste) mult *= 1.0 + 0.10 * (e.amplifier + 1) * 3.0 / 2.0;
        if (e.type == effects::MiningFatigue)
            mult *= (e.amplifier >= 0 ? 0.3 : 1.0) * std::pow(0.7, -e.amplifier);
    }
    return mult;
}
inline float meleeDamageBonusFor(const std::vector<EffectInstance>& list) {
    float bonus = 0.f;
    for (auto& e : list)
        if (e.type == effects::Strength) bonus += 3.f * (e.amplifier + 1);
        else if (e.type == effects::Weakness) bonus -= 4.f * (e.amplifier + 1);
    return bonus;
}

} // namespace cppfm
