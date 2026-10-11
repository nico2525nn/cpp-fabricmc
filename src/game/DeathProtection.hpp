#pragma once

#include "Items.hpp"
#include "MobEffects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppfm::death_protection {

inline constexpr std::uint32_t kComponentId = 32;

struct Operation {
    enum class Kind {
        ClearAll,
        Apply,
        Remove,
        RemoveTag,
        TeleportRandomly,
        PlaySound
    } kind = Kind::ClearAll;
    std::vector<EffectInstance> effects;
    std::vector<std::uint8_t> removedEffects;
    float probability = 1.0F;
    float diameter = 16.0F;
    std::string tag;
    std::string sound;
    std::optional<float> soundRange;
};

struct Protection {
    std::vector<Operation> operations;
};

struct SoundCue {
    std::string id;
    std::optional<float> fixedRange;
};

struct EffectChanges {
    std::vector<std::uint8_t> removed;
    std::vector<std::uint8_t> updated;
    std::vector<float> randomTeleportDiameters;
    std::vector<SoundCue> sounds;
};

inline void appendUnique(std::vector<std::uint8_t>& values,
                         std::uint8_t value) {
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}

template <typename RandomUnit, typename IsEffectInTag>
EffectChanges apply(const Protection& protection,
                    std::vector<EffectInstance>& activeEffects,
                    RandomUnit&& randomUnit,
                    IsEffectInTag&& isEffectInTag) {
    EffectChanges changes;
    for (const auto& operation : protection.operations) {
        switch (operation.kind) {
        case Operation::Kind::ClearAll:
            for (const auto& effect : activeEffects)
                appendUnique(changes.removed, effect.type);
            activeEffects.clear();
            break;
        case Operation::Kind::Remove:
            std::erase_if(activeEffects, [&](const EffectInstance& effect) {
                const bool remove = std::find(
                    operation.removedEffects.begin(),
                    operation.removedEffects.end(), effect.type) !=
                    operation.removedEffects.end();
                if (remove) appendUnique(changes.removed, effect.type);
                return remove;
            });
            break;
        case Operation::Kind::RemoveTag:
            std::erase_if(activeEffects, [&](const EffectInstance& effect) {
                const bool remove = isEffectInTag(operation.tag, effect.type);
                if (remove) appendUnique(changes.removed, effect.type);
                return remove;
            });
            break;
        case Operation::Kind::Apply:
            if (operation.probability <= 0.0F) break;
            for (const auto& applied : operation.effects) {
                if (operation.probability < 1.0F &&
                    randomUnit() >= operation.probability)
                    continue;
                if (applyStatusEffect(activeEffects, applied))
                    appendUnique(changes.updated, applied.type);
            }
            break;
        case Operation::Kind::TeleportRandomly:
            changes.randomTeleportDiameters.push_back(operation.diameter);
            break;
        case Operation::Kind::PlaySound:
            changes.sounds.push_back(
                {operation.sound, operation.soundRange});
            break;
        }
    }
    return changes;
}

inline std::optional<std::int32_t> integerField(const nbt::Value* value) {
    if (!value) return std::nullopt;
    switch (value->tag) {
    case nbt::Byte: return value->b;
    case nbt::Short: return value->s;
    case nbt::Int: return value->i;
    default: return std::nullopt;
    }
}

inline bool booleanField(const nbt::Value* value, bool fallback,
                         bool& output) {
    if (!value) {
        output = fallback;
        return true;
    }
    if (value->tag != nbt::Byte) return false;
    output = value->b != 0;
    return true;
}

inline std::optional<HiddenEffectLayer> hiddenEffectLayerFromNbt(
    const nbt::Value& value) {
    if (value.tag != nbt::Compound) return std::nullopt;
    const auto amplifier = integerField(value.get("amplifier"));
    const auto duration = integerField(value.get("duration"));
    if (!amplifier || *amplifier < 0 ||
        *amplifier > kMaxEffectAmplifier ||
        !duration || *duration < -1)
        return std::nullopt;

    HiddenEffectLayer result;
    result.amplifier = *amplifier;
    result.durationTicks = *duration;
    if (!booleanField(value.get("ambient"), false, result.ambient) ||
        !booleanField(value.get("show_particles"), true,
                      result.showParticles) ||
        !booleanField(value.get("show_icon"), true, result.showIcon))
        return std::nullopt;
    return result;
}

inline std::optional<EffectInstance> effectFromNbt(
    const nbt::Value& value) {
    if (value.tag != nbt::Compound) return std::nullopt;
    const auto* id = value.get("id");
    if (!id || id->tag != nbt::String) return std::nullopt;
    const auto found = effects::byName().find(id->str);
    if (found == effects::byName().end()) return std::nullopt;

    const auto amplifier = integerField(value.get("amplifier"));
    const auto duration = integerField(value.get("duration"));
    if (!amplifier || *amplifier < 0 ||
        *amplifier > kMaxEffectAmplifier ||
        !duration || *duration < -1)
        return std::nullopt;

    EffectInstance result;
    result.type = found->second;
    result.amplifier = *amplifier;
    result.durationTicks = *duration;
    result.infiniteDuration = *duration == -1;
    if (!booleanField(value.get("ambient"), false, result.ambient) ||
        !booleanField(value.get("show_particles"), true,
                      result.showParticles) ||
        !booleanField(value.get("show_icon"), true, result.showIcon))
        return std::nullopt;
    const auto* hidden = value.get("hidden_effect");
    for (std::size_t depth = 0; hidden; ++depth) {
        if (depth >= 64) return std::nullopt;
        const auto layer = hiddenEffectLayerFromNbt(*hidden);
        if (!layer) return std::nullopt;
        result.hiddenEffects.push_back(*layer);
        hidden = hidden->get("hidden_effect");
    }
    return result;
}

inline std::optional<Protection> fromDeathEffects(
    const nbt::Value* savedEffects) {
    if (!savedEffects || savedEffects->tag != nbt::List ||
        (!savedEffects->list.empty() &&
         savedEffects->listElement != nbt::Compound))
        return std::nullopt;

    Protection result;
    result.operations.reserve(savedEffects->list.size());
    for (const auto& saved : savedEffects->list) {
        const auto* type = saved.get("type");
        if (saved.tag != nbt::Compound || !type || type->tag != nbt::String)
            return std::nullopt;
        std::string_view name(type->str);
        constexpr std::string_view kMinecraftPrefix = "minecraft:";
        if (name.starts_with(kMinecraftPrefix)) {
            name.remove_prefix(kMinecraftPrefix.size());
        } else if (name.find(':') != std::string_view::npos) {
            return std::nullopt;
        }

        if (name == "clear_all_effects") {
            Operation operation;
            operation.kind = Operation::Kind::ClearAll;
            result.operations.push_back(std::move(operation));
            continue;
        }
        if (name == "apply_effects") {
            Operation operation;
            operation.kind = Operation::Kind::Apply;
            if (const auto* probability = saved.get("probability")) {
                if (probability->tag != nbt::Float ||
                    !std::isfinite(probability->f) ||
                    probability->f < 0.0F || probability->f > 1.0F)
                    return std::nullopt;
                operation.probability = probability->f;
            }
            const auto* effectsList = saved.get("effects");
            if (!effectsList || effectsList->tag != nbt::List ||
                (!effectsList->list.empty() &&
                 effectsList->listElement != nbt::Compound))
                return std::nullopt;
            operation.effects.reserve(effectsList->list.size());
            for (const auto& savedEffect : effectsList->list) {
                const auto effect = effectFromNbt(savedEffect);
                if (!effect) return std::nullopt;
                operation.effects.push_back(*effect);
            }
            result.operations.push_back(std::move(operation));
            continue;
        }
        if (name == "remove_effects") {
            Operation operation;
            operation.kind = Operation::Kind::Remove;
            const auto* ids = saved.get("effects");
            if (!ids) return std::nullopt;
            if (ids->tag == nbt::List) {
                if (!ids->list.empty() && ids->listElement != nbt::String)
                    return std::nullopt;
                for (const auto& id : ids->list) {
                    if (id.tag != nbt::String) return std::nullopt;
                    const auto found = effects::byName().find(id.str);
                    if (found != effects::byName().end())
                        operation.removedEffects.push_back(found->second);
                }
            } else if (ids->tag == nbt::String && !ids->str.empty() &&
                       ids->str.front() == '#') {
                operation.kind = Operation::Kind::RemoveTag;
                operation.tag = ids->str.substr(1);
                if (operation.tag.find(':') == std::string::npos)
                    operation.tag.insert(0, "minecraft:");
            } else {
                return std::nullopt;
            }
            result.operations.push_back(std::move(operation));
            continue;
        }
        if (name == "teleport_randomly") {
            Operation operation;
            operation.kind = Operation::Kind::TeleportRandomly;
            if (const auto* diameter = saved.get("diameter")) {
                if (diameter->tag != nbt::Float ||
                    !std::isfinite(diameter->f) || diameter->f < 0.0F)
                    return std::nullopt;
                operation.diameter = diameter->f;
            }
            result.operations.push_back(std::move(operation));
            continue;
        }
        if (name == "play_sound") {
            const auto* sound = saved.get("sound");
            if (!sound) return std::nullopt;
            const auto* soundId = sound->tag == nbt::String
                ? sound : sound->get("sound_id");
            if (!soundId || soundId->tag != nbt::String)
                return std::nullopt;
            Operation operation;
            operation.kind = Operation::Kind::PlaySound;
            operation.sound = soundId->str;
            if (sound->tag == nbt::Compound) {
                if (const auto* range = sound->get("range")) {
                    if (range->tag != nbt::Float ||
                        !std::isfinite(range->f) || range->f <= 0.0F)
                        return std::nullopt;
                    operation.soundRange = range->f;
                }
            }
            result.operations.push_back(std::move(operation));
            continue;
        }
        // The component codec only admits the five vanilla consume-effect
        // variants. Refuse any unexpected persisted/custom value rather than
        // silently treating a partial death-protection component as valid.
        return std::nullopt;
    }
    return result;
}

inline std::optional<Protection> fromStack(const ItemStack& stack) {
    if (stack.empty()) return std::nullopt;
    const auto component = std::find_if(
        stack.components.begin(), stack.components.end(),
        [](const auto& entry) { return entry.first == kComponentId; });
    if (component != stack.components.end()) {
        const auto saved = item_component_nbt::additional_food::toNbt(
            kComponentId, component->second);
        if (!saved) return std::nullopt;
        return fromDeathEffects(saved->get("death_effects"));
    }

    const bool removed = std::find(stack.removedComponents.begin(),
                                   stack.removedComponents.end(),
                                   kComponentId) !=
                         stack.removedComponents.end();
    if (removed || stack.name() != "minecraft:totem_of_undying")
        return std::nullopt;

    nbt::Value savedEffects = nbt::Value::makeList(nbt::Compound, 2);
    nbt::Value clearAll = nbt::Value::makeCompound();
    clearAll.set("type", nbt::Value::makeString(
                              "minecraft:clear_all_effects"));
    savedEffects.list.push_back(std::move(clearAll));

    nbt::Value apply = nbt::Value::makeCompound();
    apply.set("type", nbt::Value::makeString("minecraft:apply_effects"));
    auto defaults = nbt::Value::makeList(nbt::Compound, 3);
    const auto addDefault = [&defaults](const char* id, int amplifier,
                                        int duration) {
        nbt::Value effect = nbt::Value::makeCompound();
        effect.set("id", nbt::Value::makeString(id));
        effect.set("amplifier", nbt::Value::makeInt(amplifier));
        effect.set("duration", nbt::Value::makeInt(duration));
        effect.set("ambient", nbt::Value::makeByte(0));
        effect.set("show_particles", nbt::Value::makeByte(1));
        effect.set("show_icon", nbt::Value::makeByte(1));
        defaults.list.push_back(std::move(effect));
    };
    addDefault("minecraft:regeneration", 1, 900);
    addDefault("minecraft:absorption", 1, 100);
    addDefault("minecraft:fire_resistance", 0, 800);
    apply.set("effects", std::move(defaults));
    apply.set("probability", nbt::Value::makeFloat(1.0F));
    savedEffects.list.push_back(std::move(apply));
    return fromDeathEffects(&savedEffects);
}

}  // namespace cppfm::death_protection
