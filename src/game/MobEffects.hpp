// MobEffects: vanilla status-effect registry (numeric ids are the jar's
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <cmath>
#include <utility>
#include <vector>

namespace cppfm {

inline constexpr std::int32_t kMaxEffectAmplifier = 255;

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
    case TrialOmen: return "minecraft:trial_omen";
    case RaidOmen: return "minecraft:raid_omen";
    case WindCharged: return "minecraft:wind_charged";
    case Weaving: return "minecraft:weaving";
    case Oozing: return "minecraft:oozing";
    case Infested: return "minecraft:infested";
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
        mm.emplace("bad_omen", BadOmen);           mm.emplace("trial_omen", TrialOmen);
        mm.emplace("raid_omen", RaidOmen);         mm.emplace("hero_of_the_village", HeroOfTheVillage);
        mm.emplace("wind_charged", WindCharged);   mm.emplace("weaving", Weaving);
        mm.emplace("oozing", Oozing);              mm.emplace("infested", Infested);
        return mm;
    }();
    return m;
}
} // namespace effects

struct HiddenEffectLayer {
    std::int32_t amplifier = 0;
    std::int32_t durationTicks = 0;
    bool ambient = false;
    bool showParticles = true;
    bool showIcon = true;
};

// One active effect on a living entity. Lower-amplifier, longer effects are
// retained here and resume after a stronger, shorter effect expires.
struct EffectInstance {
    std::uint8_t type = 0;
    std::int32_t amplifier = 0;         // level-1
    std::int32_t durationTicks = 0;
    // The tick loop decrements durationTicks directly. Keep the semantic
    // infinite-duration bit so a wire -1 remains infinite after that decrement.
    bool infiniteDuration = false;
    bool ambient = false;
    bool showParticles = true;
    bool showIcon = true;
    std::vector<HiddenEffectLayer> hiddenEffects;

    EffectInstance() = default;
    EffectInstance(const std::uint8_t effectType,
                   const std::int32_t effectAmplifier,
                   const std::int32_t effectDuration,
                   const bool effectAmbient = false,
                   const bool particles = true, const bool icon = true,
                   std::vector<HiddenEffectLayer> hidden = {})
        : type(effectType), amplifier(effectAmplifier),
          durationTicks(effectDuration), infiniteDuration(effectDuration == -1),
          ambient(effectAmbient),
          showParticles(particles), showIcon(icon),
          hiddenEffects(std::move(hidden)) {}

    bool expired() const {
        return !infiniteDuration && durationTicks <= 0 &&
               type != effects::InstantHealth &&
               type != effects::InstantDamage;
    }
};

inline bool effectDurationLonger(const std::int32_t lhs,
                                const std::int32_t rhs) noexcept {
    if (lhs < 0) return rhs >= 0;
    if (rhs < 0) return false;
    return lhs > rhs;
}

inline std::int32_t effectDurationForComparison(
    const EffectInstance& effect) noexcept {
    return effect.infiniteDuration ? -1 : effect.durationTicks;
}

inline bool validEffectInstance(const EffectInstance& effect) noexcept {
    if (effect.amplifier < 0 || effect.amplifier > kMaxEffectAmplifier ||
        (!effect.infiniteDuration && effect.durationTicks < -1))
        return false;
    return std::all_of(effect.hiddenEffects.begin(),
                       effect.hiddenEffects.end(),
        [](const HiddenEffectLayer& layer) {
            return layer.amplifier >= 0 &&
                   layer.amplifier <= kMaxEffectAmplifier &&
                   layer.durationTicks >= -1;
        });
}

inline int effectTickInterval(const int baseInterval,
                              const std::int32_t amplifier) {
    if (baseInterval <= 1 || amplifier >= std::numeric_limits<int>::digits)
        return 1;
    if (amplifier <= 0) return std::max(1, baseInterval);
    return std::max(1, baseInterval >> amplifier);
}

inline bool effectPeriodicTickDue(const std::int32_t remainingDuration,
                                  const int baseInterval,
                                  const std::int32_t amplifier) {
    if (remainingDuration <= 0) return false;
    const int interval = effectTickInterval(baseInterval, amplifier);
    return remainingDuration % interval == 0;
}

inline bool isInstantEffectType(const std::uint8_t type) noexcept {
    return type == effects::InstantHealth || type == effects::InstantDamage;
}

inline std::vector<EffectInstance> persistentEntityEffectUpdates(
    const std::vector<std::uint8_t>& changedEffectIds,
    const std::vector<EffectInstance>& activeEffects) {
    std::vector<EffectInstance> result;
    result.reserve(changedEffectIds.size());
    for (const auto id : changedEffectIds) {
        const auto effect = std::find_if(
            activeEffects.begin(), activeEffects.end(),
            [id](const EffectInstance& active) { return active.type == id; });
        if (effect != activeEffects.end() && !isInstantEffectType(effect->type)) {
            auto snapshot = *effect;
            if (snapshot.infiniteDuration || snapshot.durationTicks < 0) {
                snapshot.infiniteDuration = true;
                snapshot.durationTicks = -1;
            }
            result.push_back(std::move(snapshot));
        }
    }
    return result;
}

inline HiddenEffectLayer hiddenLayer(const EffectInstance& effect) {
    return {effect.amplifier,
            effect.infiniteDuration ? -1 : effect.durationTicks, effect.ambient,
            effect.showParticles, effect.showIcon};
}

inline void mergeHiddenLayer(std::vector<HiddenEffectLayer>& layers,
                             const HiddenEffectLayer& incoming) {
    const auto existing = std::find_if(
        layers.begin(), layers.end(), [&](const HiddenEffectLayer& layer) {
            return layer.amplifier == incoming.amplifier;
        });
    if (existing == layers.end()) {
        layers.push_back(incoming);
    } else if (effectDurationLonger(incoming.durationTicks,
                                    existing->durationTicks)) {
        *existing = incoming;
    }
    std::sort(layers.begin(), layers.end(),
        [](const HiddenEffectLayer& lhs, const HiddenEffectLayer& rhs) {
            return lhs.amplifier > rhs.amplifier;
        });
}

// Returns true only when the currently visible instance changes. A weaker,
// longer instance is kept as a fallback without extending the stronger
// effect's timer.
inline bool applyStatusEffect(std::vector<EffectInstance>& activeEffects,
                              const EffectInstance& incoming) {
    if (!validEffectInstance(incoming)) return false;
    EffectInstance normalizedIncoming = incoming;
    if (normalizedIncoming.infiniteDuration ||
        normalizedIncoming.durationTicks == -1) {
        normalizedIncoming.infiniteDuration = true;
        normalizedIncoming.durationTicks = -1;
    }
    const auto incomingDuration =
        effectDurationForComparison(normalizedIncoming);
    auto active = std::find_if(activeEffects.begin(), activeEffects.end(),
        [&](const EffectInstance& effect) {
            return effect.type == normalizedIncoming.type;
        });
    if (active == activeEffects.end()) {
        activeEffects.push_back(std::move(normalizedIncoming));
        return true;
    }

    const auto activeDuration = effectDurationForComparison(*active);
    if (normalizedIncoming.amplifier < active->amplifier) {
        if (effectDurationLonger(incomingDuration, activeDuration))
            mergeHiddenLayer(active->hiddenEffects,
                             hiddenLayer(normalizedIncoming));
        for (const auto& layer : normalizedIncoming.hiddenEffects)
            if (effectDurationLonger(layer.durationTicks, activeDuration))
                mergeHiddenLayer(active->hiddenEffects, layer);
        return false;
    }

    if (normalizedIncoming.amplifier == active->amplifier) {
        bool changed = false;
        if (effectDurationLonger(incomingDuration, activeDuration)) {
            active->durationTicks = incomingDuration;
            active->infiniteDuration = normalizedIncoming.infiniteDuration;
            active->ambient = normalizedIncoming.ambient;
            active->showParticles = normalizedIncoming.showParticles;
            active->showIcon = normalizedIncoming.showIcon;
            std::erase_if(active->hiddenEffects,
                [&](const HiddenEffectLayer& layer) {
                    return !effectDurationLonger(
                        layer.durationTicks, incomingDuration);
                });
            changed = true;
        }
        for (const auto& layer : normalizedIncoming.hiddenEffects)
            if (effectDurationLonger(layer.durationTicks,
                                     effectDurationForComparison(*active)))
                mergeHiddenLayer(active->hiddenEffects, layer);
        return changed;
    }

    // Carry the old chain forward before pruning entries that cannot outlast
    // the replacement. A hidden layer may outlive the old active effect even
    // when the new effect itself lasts longer than that active effect.
    std::vector<HiddenEffectLayer> fallbacks =
        normalizedIncoming.hiddenEffects;
    mergeHiddenLayer(fallbacks, hiddenLayer(*active));
    for (const auto& layer : active->hiddenEffects)
        mergeHiddenLayer(fallbacks, layer);
    active->amplifier = normalizedIncoming.amplifier;
    active->durationTicks = incomingDuration;
    active->infiniteDuration = normalizedIncoming.infiniteDuration;
    active->ambient = normalizedIncoming.ambient;
    active->showParticles = normalizedIncoming.showParticles;
    active->showIcon = normalizedIncoming.showIcon;
    active->hiddenEffects = std::move(fallbacks);
    std::erase_if(active->hiddenEffects,
        [&](const HiddenEffectLayer& layer) {
            return !effectDurationLonger(layer.durationTicks,
                                         incomingDuration);
        });
    return true;
}

inline void tickHiddenEffects(EffectInstance& effect) {
    // The runtime tick path decrements durationTicks immediately afterwards.
    // Remember a direct -1 assignment before that decrement turns it negative.
    if (effect.durationTicks == -1) effect.infiniteDuration = true;
    for (auto& layer : effect.hiddenEffects)
        if (layer.durationTicks > 0) --layer.durationTicks;
    std::erase_if(effect.hiddenEffects,
        [](const HiddenEffectLayer& layer) {
            return layer.durationTicks == 0;
        });
}

inline bool promoteHiddenEffect(EffectInstance& effect) {
    if (effect.hiddenEffects.empty()) return false;
    const auto fallback = effect.hiddenEffects.front();
    for (std::size_t index = 1; index < effect.hiddenEffects.size(); ++index)
        effect.hiddenEffects[index - 1] = effect.hiddenEffects[index];
    effect.hiddenEffects.pop_back();
    effect.amplifier = fallback.amplifier;
    effect.durationTicks = fallback.durationTicks;
    effect.infiniteDuration = fallback.durationTicks == -1;
    effect.ambient = fallback.ambient;
    effect.showParticles = fallback.showParticles;
    effect.showIcon = fallback.showIcon;
    return true;
}

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
inline float meleeDamageBonusFor(const std::vector<EffectInstance>& list) {
    float bonus = 0.f;
    for (auto& e : list)
        if (e.type == effects::Strength) bonus += 3.f * (e.amplifier + 1);
        else if (e.type == effects::Weakness) bonus -= 4.f * (e.amplifier + 1);
    return bonus;
}
inline bool hasEffect(const std::vector<EffectInstance>& list, std::uint8_t id) {
    for (auto &e : list) if (e.type == id) return true;
    return false;
}
inline int amplifierFor(const std::vector<EffectInstance>& list, std::uint8_t id) {
    int best = -1;
    for (auto &e : list) if (e.type == id) best = std::max(best, int(e.amplifier));
    return best;
}
inline float absorptionFor(const std::vector<EffectInstance>& list) {
    int amp = amplifierFor(list, effects::Absorption);
    if (amp < 0) return 0.f;
    return 4.f * float(amp + 1);
}
inline bool isInvisible(const std::vector<EffectInstance>& list) { return hasEffect(list, effects::Invisibility); }
inline bool isGlowing(const std::vector<EffectInstance>& list) { return hasEffect(list, effects::Glowing); }
// Hunger effect adds exhaustion per tick; saturation effect restores food
inline float hungerExhaustionPerTick(const std::vector<EffectInstance>& list) {
    int amp = amplifierFor(list, effects::Hunger);
    if (amp < 0) return 0.f;
    return 0.005f * float(amp + 1);
}
// Levitation velocity per Yarn LivingEntity: vy += (0.05*(amp+1) - vy)*0.2, fallDistance reset
inline double levitationVelocity(int amp) { return 0.05 * double(amp + 1); }

namespace mob_effect_dur {
    constexpr int TRIAL_OMEN_PER_LEVEL = 15 * 60 * 20; // 18000 ticks per BadOmen level (vanilla TrialOmen)
    constexpr int RAID_OMEN_DURATION = 30 * 20;        // 600 ticks, amplifier = BadOmen level (vanilla RaidOmen)
}
inline bool shouldApplySaturation(const std::vector<EffectInstance>& list, int tickNo) {
    int amp = amplifierFor(list, effects::Saturation);
    if (amp < 0) return false;
    int period = std::max(1, 2 >> amp);
    return tickNo % period == 0;
}
inline bool shouldApplyHunger(const std::vector<EffectInstance>& list, int tickNo) {
    int amp = amplifierFor(list, effects::Hunger);
    if (amp < 0) return false;
    return tickNo % 40 == 0;
}
inline bool shouldApplyRegeneration(const std::vector<EffectInstance>& list, int tickNo) {
    int amp = amplifierFor(list, effects::Regeneration);
    if (amp < 0) return false;
    int period = std::max(1, 50 >> amp);
    return tickNo % period == 0;
}
inline bool shouldApplyPoison(const std::vector<EffectInstance>& list, int tickNo) {
    int amp = amplifierFor(list, effects::Poison);
    if (amp < 0) return false;
    int period = std::max(1, 25 >> amp);
    return tickNo % period == 0;
}
inline bool shouldApplyWither(const std::vector<EffectInstance>& list, int tickNo) {
    int amp = amplifierFor(list, effects::Wither);
    if (amp < 0) return false;
    int period = std::max(1, 40 >> amp);
    return tickNo % period == 0;
}
} // namespace cppfm
