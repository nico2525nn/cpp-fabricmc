#pragma once

#include "Items.hpp"
#include "DeathProtection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace cppfm::food_components {

inline constexpr std::uint32_t kFood = 21;
inline constexpr std::uint32_t kConsumable = 22;
inline constexpr std::uint32_t kUseRemainder = 23;
inline constexpr std::uint32_t kSuspiciousStewEffects = 42;
inline constexpr std::uint32_t kOminousBottleAmplifier = 51;
inline constexpr std::int32_t kFullFoodLevel = 20;

struct Food {
    std::int32_t nutrition = 0;
    float saturationModifier = 0.0F;
    bool canAlwaysEat = false;
};

inline bool canConsumeAtFoodLevel(const Food& food,
                                  const std::int32_t foodLevel) {
    return foodLevel < kFullFoodLevel || food.canAlwaysEat;
}

inline const std::vector<std::uint8_t>* payloadFor(
    const ItemStack& stack, std::uint32_t componentId) {
    const auto found = std::find_if(
        stack.components.begin(), stack.components.end(),
        [componentId](const auto& component) {
            return component.first == componentId;
        });
    return found == stack.components.end() ? nullptr : &found->second;
}

inline bool explicitlyRemoved(const ItemStack& stack,
                              std::uint32_t componentId) {
    return std::find(stack.removedComponents.begin(),
                     stack.removedComponents.end(), componentId) !=
           stack.removedComponents.end();
}

inline std::optional<Food> readFood(const ItemStack& stack) {
    const auto* payload = payloadFor(stack, kFood);
    if (!payload) return std::nullopt;
    const auto saved = item_component_nbt::additional_food::toNbt(
        kFood, *payload);
    if (!saved) return std::nullopt;
    const auto* nutrition = saved->get("nutrition");
    const auto* saturation = saved->get("saturation");
    const auto* canAlwaysEat = saved->get("can_always_eat");
    if (!nutrition || nutrition->tag != nbt::Int || nutrition->i < 0 ||
        !saturation || saturation->tag != nbt::Float ||
        !std::isfinite(saturation->f) || saturation->f < 0.0F ||
        !canAlwaysEat || canAlwaysEat->tag != nbt::Byte ||
        (canAlwaysEat->b != 0 && canAlwaysEat->b != 1))
        return std::nullopt;
    return Food{nutrition->i, saturation->f, canAlwaysEat->b != 0};
}

inline std::optional<nbt::Value> readConsumable(const ItemStack& stack) {
    const auto* payload = payloadFor(stack, kConsumable);
    if (!payload) return std::nullopt;
    return item_component_nbt::additional_food::toNbt(kConsumable, *payload);
}

inline std::optional<float> consumeSeconds(const ItemStack& stack,
                                           const bool vanillaConsumable) {
    if (explicitlyRemoved(stack, kConsumable)) return std::nullopt;
    if (payloadFor(stack, kConsumable)) {
        const auto consumable = readConsumable(stack);
        if (!consumable) return std::nullopt;
        const auto* seconds = consumable->get("consume_seconds");
        if (!seconds || seconds->tag != nbt::Float ||
            !std::isfinite(seconds->f) || seconds->f <= 0.0F)
            return std::nullopt;
        return seconds->f;
    }
    if (!vanillaConsumable) return std::nullopt;

    const auto name = stack.name();
    if (name == "minecraft:dried_kelp") return 0.8F;
    if (name == "minecraft:honey_bottle") return 2.0F;
    return 1.6F;
}

inline std::optional<ItemStack> readUseRemainder(const ItemStack& stack) {
    const auto* payload = payloadFor(stack, kUseRemainder);
    if (!payload) return std::nullopt;
    const auto saved = item_component_nbt::additional_food::toNbt(
        kUseRemainder, *payload);
    return saved ? ItemStack::fromNbt(*saved) : std::nullopt;
}

inline std::optional<std::int32_t> readOminousBottleAmplifier(
    const ItemStack& stack) {
    const auto* payload = payloadFor(stack, kOminousBottleAmplifier);
    if (!payload) return 0;
    const auto saved = item_component_nbt::additional_other::toNbt(
        kOminousBottleAmplifier, *payload, 0);
    if (!saved || saved->tag != nbt::Int || saved->i < 0 || saved->i > 4)
        return std::nullopt;
    return saved->i;
}

inline std::optional<death_protection::Protection> readStewEffects(
    const ItemStack& stack) {
    death_protection::Protection result;
    const auto* payload = payloadFor(stack, kSuspiciousStewEffects);
    if (!payload) return result;
    const auto saved = item_component_nbt::additional_other::toNbt(
        kSuspiciousStewEffects, *payload, 0);
    if (!saved || saved->tag != nbt::List ||
        (!saved->list.empty() && saved->listElement != nbt::Compound))
        return std::nullopt;

    death_protection::Operation operation;
    operation.kind = death_protection::Operation::Kind::Apply;
    operation.effects.reserve(saved->list.size());
    for (const auto& entry : saved->list) {
        if (entry.tag != nbt::Compound) return std::nullopt;
        const auto* name = entry.get("effect");
        const auto* duration = entry.get("duration");
        if (!name || name->tag != nbt::String || !duration ||
            duration->tag != nbt::Int || duration->i < 0)
            return std::nullopt;
        const auto canonical = item_component_nbt::canonicalIdentifier(
            name->str);
        const auto found = canonical
            ? effects::byName().find(*canonical) : effects::byName().end();
        if (found == effects::byName().end()) return std::nullopt;
        operation.effects.emplace_back(found->second, 0, duration->i);
    }
    if (!operation.effects.empty())
        result.operations.push_back(std::move(operation));
    return result;
}

inline std::optional<ItemStack> defaultUseRemainder(
    const ItemStack& consumed) {
    const auto name = consumed.name();
    if (name == "minecraft:beetroot_soup" ||
        name == "minecraft:mushroom_stew" ||
        name == "minecraft:rabbit_stew" ||
        name == "minecraft:suspicious_stew")
        return ItemStack::ofName("minecraft:bowl");
    if (name == "minecraft:honey_bottle")
        return ItemStack::ofName("minecraft:glass_bottle");
    if (name == "minecraft:milk_bucket")
        return ItemStack::ofName("minecraft:bucket");
    return std::nullopt;
}

inline float saturationGain(const Food& food) {
    const double gain = static_cast<double>(food.nutrition) *
                        static_cast<double>(food.saturationModifier) * 2.0;
    if (!(gain > 0.0)) return 0.0F;
    // The hunger manager clamps final saturation to food level, which is at
    // most 20. Capping here avoids overflow from hostile but finite component
    // values without changing any observable result.
    return static_cast<float>(std::min(gain, 20.0));
}

inline death_protection::Protection vanillaConsumeEffects(
    const std::string_view itemName,
    const std::int32_t ominousBottleAmplifier = 0) {
    using Kind = death_protection::Operation::Kind;
    death_protection::Protection result;
    const auto addEffects = [&](std::initializer_list<EffectInstance> effects,
                                const float probability = 1.0F) {
        death_protection::Operation operation;
        operation.kind = Kind::Apply;
        operation.probability = probability;
        operation.effects.assign(effects.begin(), effects.end());
        result.operations.push_back(std::move(operation));
    };
    const auto effect = [](const std::uint8_t type, const int amplifier,
                           const int duration) {
        return EffectInstance{type, amplifier, duration, false, true, true};
    };

    if (itemName == "minecraft:golden_apple") {
        addEffects({effect(effects::Regeneration, 1, 100),
                    effect(effects::Absorption, 0, 2400)});
    } else if (itemName == "minecraft:enchanted_golden_apple") {
        addEffects({effect(effects::Regeneration, 1, 400),
                    effect(effects::Absorption, 3, 2400),
                    effect(effects::Resistance, 0, 6000),
                    effect(effects::FireResistance, 0, 6000)});
    } else if (itemName == "minecraft:chorus_fruit") {
        death_protection::Operation operation;
        operation.kind = Kind::TeleportRandomly;
        operation.diameter = 16.0F;
        result.operations.push_back(std::move(operation));
    } else if (itemName == "minecraft:honey_bottle") {
        death_protection::Operation operation;
        operation.kind = Kind::Remove;
        operation.removedEffects.push_back(effects::Poison);
        result.operations.push_back(std::move(operation));
    } else if (itemName == "minecraft:milk_bucket") {
        death_protection::Operation operation;
        operation.kind = Kind::ClearAll;
        result.operations.push_back(std::move(operation));
    } else if (itemName == "minecraft:ominous_bottle") {
        addEffects({effect(effects::BadOmen, ominousBottleAmplifier,
                           120000)});
    } else if (itemName == "minecraft:rotten_flesh") {
        addEffects({effect(effects::Hunger, 0, 600)}, 0.8F);
    } else if (itemName == "minecraft:chicken") {
        addEffects({effect(effects::Hunger, 0, 600)}, 0.3F);
    } else if (itemName == "minecraft:spider_eye") {
        addEffects({effect(effects::Poison, 0, 100)});
    } else if (itemName == "minecraft:pufferfish") {
        addEffects({effect(effects::Hunger, 2, 300),
                    effect(effects::Nausea, 0, 300),
                    effect(effects::Poison, 3, 1200)});
    } else if (itemName == "minecraft:poisonous_potato") {
        addEffects({effect(effects::Poison, 0, 100)}, 0.6F);
    }
    return result;
}

}  // namespace cppfm::food_components
