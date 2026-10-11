// Bounded codecs for the vanilla 1.21.4 item components which were not in
// ItemComponentNbt.hpp. Include this after Items.hpp has defined ItemStack
// and the existing item-component helpers.
#pragma once

#include "../generated/EntityIds.hpp"
#include "../generated/SoundEventRegistryIds.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppfm::item_component_nbt::additional_food {

inline constexpr std::size_t kMaximumPayloadBytes =
    item_component_nbt::kMaxProtocolComponentPayloadBytes;
inline constexpr std::size_t kMaximumEntries = 1024;
inline constexpr std::size_t kMaximumIdentifierUtf16Units = 32767;

namespace detail {

enum class RegistryKind : std::uint8_t {
    Block,
    Item,
    EntityType,
    StatusEffect,
};

struct SoundValue {
    std::string id;
    std::optional<float> fixedRange;
    bool registryReference = false;
};

inline bool withinBudget(const ReadBuffer& input, std::size_t start) noexcept {
    return input.off >= start &&
           input.off - start <= kMaximumPayloadBytes;
}

inline bool onlyKeys(const nbt::Value& value,
                     std::initializer_list<std::string_view> allowed) {
    if (value.tag != nbt::Compound || value.comp.size() > allowed.size())
        return false;
    for (std::size_t i = 0; i < value.comp.size(); ++i) {
        const auto& key = value.comp[i].first;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (value.comp[j].first == key) return false;
    }
    return true;
}

inline std::optional<float> floatValue(const nbt::Value* value) {
    if (!value || value->tag != nbt::Float || !std::isfinite(value->f))
        return std::nullopt;
    return value->f;
}

inline std::optional<float> floatField(const nbt::Value& value,
                                       std::string_view key,
                                       float fallback) {
    const auto* field = value.get(key);
    if (!field) return fallback;
    return floatValue(field);
}

inline std::optional<std::int32_t> nonNegativeInteger(
    const nbt::Value* value) {
    const auto number = item_component_nbt::integer(value);
    if (!number || *number < 0) return std::nullopt;
    return number;
}

inline bool booleanField(const nbt::Value& value, std::string_view key,
                         bool fallback, bool& result) {
    const auto* field = value.get(key);
    if (!field) {
        result = fallback;
        return true;
    }
    if (field->tag != nbt::Byte || (field->b != 0 && field->b != 1))
        return false;
    result = field->b != 0;
    return true;
}

inline std::optional<std::string> identifier(const nbt::Value* value) {
    if (!value || value->tag != nbt::String) return std::nullopt;
    auto canonical = item_component_nbt::canonicalIdentifier(value->str);
    if (!canonical ||
        !item_component_nbt::withinStringLength(
            *canonical, kMaximumIdentifierUtf16Units))
        return std::nullopt;
    return canonical;
}

inline bool writeIdentifier(WriteBuffer& output, std::string_view value) {
    const auto canonical = item_component_nbt::canonicalIdentifier(value);
    return canonical &&
           item_component_nbt::withinStringLength(
               *canonical, kMaximumIdentifierUtf16Units) &&
           item_component_nbt::writeProtocolString(
               output, *canonical, kMaximumIdentifierUtf16Units);
}

inline bool validIdentifier(std::string_view value) {
    return item_component_nbt::isResourceIdentifier(value) &&
           item_component_nbt::withinStringLength(
               value, kMaximumIdentifierUtf16Units);
}

inline std::optional<std::string> readIdentifier(ReadBuffer& input) {
    auto value = item_component_nbt::readProtocolString(
        input, kMaximumIdentifierUtf16Units);
    if (!value || !validIdentifier(*value)) return std::nullopt;
    return value;
}

inline std::optional<std::int32_t> registryId(RegistryKind kind,
                                               std::string_view name) {
    const auto canonical = item_component_nbt::canonicalIdentifier(name);
    if (!canonical) return std::nullopt;
    switch (kind) {
    case RegistryKind::Block: {
        const auto id = gen::blockRegistryId(*canonical);
        if (!id || *id >
                       static_cast<std::uint32_t>(
                           std::numeric_limits<std::int32_t>::max()))
            return std::nullopt;
        return static_cast<std::int32_t>(*id);
    }
    case RegistryKind::Item: {
        const auto found = gen::itemIdByName().find(*canonical);
        if (found == gen::itemIdByName().end() ||
            found->second > static_cast<std::uint32_t>(
                                std::numeric_limits<std::int32_t>::max()))
            return std::nullopt;
        return static_cast<std::int32_t>(found->second);
    }
    case RegistryKind::EntityType: {
        const auto found = gen::entityTypeIdByName().find(*canonical);
        if (found == gen::entityTypeIdByName().end() ||
            found->second > static_cast<std::uint32_t>(
                                std::numeric_limits<std::int32_t>::max()))
            return std::nullopt;
        return static_cast<std::int32_t>(found->second);
    }
    case RegistryKind::StatusEffect: {
        const auto found = effects::byName().find(*canonical);
        if (found == effects::byName().end()) return std::nullopt;
        return static_cast<std::int32_t>(found->second);
    }
    }
    return std::nullopt;
}

inline std::optional<std::string> registryName(RegistryKind kind,
                                                std::int32_t id) {
    if (id < 0) return std::nullopt;
    const auto registryId = static_cast<std::uint32_t>(id);
    switch (kind) {
    case RegistryKind::Block: {
        const auto name = gen::blockNameByRegistryId(registryId);
        if (name.empty()) return std::nullopt;
        return std::string(name);
    }
    case RegistryKind::Item: {
        const auto& names = ItemStack::itemNamesById();
        const auto found = names.find(registryId);
        if (found == names.end()) return std::nullopt;
        return found->second;
    }
    case RegistryKind::EntityType: {
        const auto found = std::find_if(
            gen::kEntities.begin(), gen::kEntities.end(),
            [registryId](const auto& entry) {
                return entry.second == registryId;
            });
        if (found == gen::kEntities.end()) return std::nullopt;
        return std::string(found->first);
    }
    case RegistryKind::StatusEffect:
        if (registryId == 0 || registryId > effects::Infested)
            return std::nullopt;
        return std::string(
            effects::nameOf(static_cast<std::uint8_t>(registryId)));
    }
    return std::nullopt;
}

inline bool writeIdSet(const nbt::Value& saved, RegistryKind kind,
                       WriteBuffer& output) {
    if (saved.tag == nbt::String) {
        if (saved.str.empty() || saved.str.front() != '#') return false;
        const auto tagName = std::string_view(saved.str).substr(1);
        if (!validIdentifier(tagName)) return false;
        output.varint(0);
        if (!item_component_nbt::writeProtocolString(
                output, std::string(tagName),
                kMaximumIdentifierUtf16Units))
            return false;
        return output.data.size() <= kMaximumPayloadBytes;
    }

    if (saved.tag != nbt::List || saved.list.size() > kMaximumEntries ||
        (saved.list.empty() && saved.listElement != nbt::String &&
         saved.listElement != nbt::End) ||
        (!saved.list.empty() && saved.listElement != nbt::String))
        return false;
    // IDSet uses zero for a named tag; direct sets encode count + 1.
    output.varint(static_cast<std::int32_t>(saved.list.size()) + 1);
    for (const auto& entry : saved.list) {
        const auto name = identifier(&entry);
        if (!name) return false;
        const auto id = registryId(kind, *name);
        if (!id) return false;
        output.varint(*id);
        if (output.data.size() > kMaximumPayloadBytes) return false;
    }
    return true;
}

inline std::optional<nbt::Value> readIdSet(ReadBuffer& input,
                                           RegistryKind kind,
                                           std::size_t start) {
    const auto encodedCount = input.varint();
    if (encodedCount < 0 ||
        static_cast<std::size_t>(encodedCount) > kMaximumEntries + 1)
        return std::nullopt;
    if (encodedCount == 0) {
        auto tag = readIdentifier(input);
        if (!tag || !withinBudget(input, start)) return std::nullopt;
        std::string markedTag;
        markedTag.reserve(tag->size() + 1);
        markedTag.push_back('#');
        markedTag.append(*tag);
        return nbt::Value::makeString(std::move(markedTag));
    }

    const auto count = encodedCount - 1;
    nbt::Value entries = nbt::Value::makeList(
        nbt::String, static_cast<std::size_t>(count));
    for (std::int32_t index = 0; index < count; ++index) {
        const auto id = input.varint();
        auto name = registryName(kind, id);
        if (!name || !withinBudget(input, start)) return std::nullopt;
        entries.list.push_back(nbt::Value::makeString(std::move(*name)));
    }
    return entries;
}

inline bool readBoolean(ReadBuffer& input, bool& value) {
    const auto encoded = input.u8();
    if (encoded > 1) return false;
    value = encoded != 0;
    return true;
}

inline std::optional<std::string> readBoundedString(ReadBuffer& input,
                                                     std::size_t start) {
    auto value = item_component_nbt::readProtocolString(
        input, item_component_nbt::kMaxPacketStringUtf16Units);
    if (!value || !withinBudget(input, start)) return std::nullopt;
    return value;
}

inline bool writeBoundedString(WriteBuffer& output, std::string_view value) {
    if (!item_component_nbt::withinStringLength(
            value, item_component_nbt::kMaxPacketStringUtf16Units))
        return false;
    output.string(value);
    return output.data.size() <= kMaximumPayloadBytes;
}

inline std::optional<SoundValue> soundFromNbt(const nbt::Value* saved) {
    SoundValue sound;
    if (!saved) return std::nullopt;
    if (saved->tag == nbt::String) {
        auto id = item_component_nbt::canonicalIdentifier(saved->str);
        if (!id || !validIdentifier(*id)) return std::nullopt;
        sound.id = std::move(*id);
        sound.registryReference = true;
        return sound;
    }
    if (!onlyKeys(*saved, {"sound_id", "range"})) return std::nullopt;
    const auto id = identifier(saved->get("sound_id"));
    if (!id) return std::nullopt;
    sound.id = *id;
    if (const auto* range = saved->get("range")) {
        const auto number = floatValue(range);
        if (!number || *number <= 0.0F) return std::nullopt;
        sound.fixedRange = *number;
    }
    return sound;
}

inline bool writeSoundHolder(const nbt::Value* saved, WriteBuffer& output) {
    const auto sound = soundFromNbt(saved);
    if (!sound) return false;

    if (sound->registryReference) {
        const auto registryId = gen::soundEventRegistryId(sound->id);
        const auto* entry = registryId
            ? gen::soundEventByRegistryId(*registryId) : nullptr;
        if (entry && entry->fixedRange == sound->fixedRange) {
            output.varint(*registryId + 1);
            return output.data.size() <= kMaximumPayloadBytes;
        }
    }

    // RegistryEntry packet codecs reserve zero for an inline/direct holder.
    output.varint(0);
    if (!writeIdentifier(output, sound->id)) return false;
    output.boolean(sound->fixedRange.has_value());
    if (sound->fixedRange) output.f32(*sound->fixedRange);
    return output.data.size() <= kMaximumPayloadBytes;
}

inline std::optional<SoundValue> readSoundHolder(ReadBuffer& input,
                                                  std::size_t start) {
    const auto holderId = input.varint();
    if (holderId < 0 || !withinBudget(input, start)) return std::nullopt;
    if (holderId != 0) {
        const auto* entry = gen::soundEventByRegistryId(holderId - 1);
        if (!entry) return std::nullopt;
        return SoundValue{std::string(entry->name), entry->fixedRange, true};
    }

    auto id = readIdentifier(input);
    if (!id || !withinBudget(input, start)) return std::nullopt;
    SoundValue sound;
    sound.id = std::move(*id);
    bool hasRange = false;
    if (!readBoolean(input, hasRange)) return std::nullopt;
    if (hasRange) {
        const auto range = input.f32();
        if (!std::isfinite(range) || range <= 0.0F)
            return std::nullopt;
        sound.fixedRange = range;
    }
    if (!withinBudget(input, start)) return std::nullopt;
    return sound;
}

inline nbt::Value soundToNbt(const SoundValue& sound) {
    if (sound.registryReference)
        return nbt::Value::makeString(sound.id);
    nbt::Value result = nbt::Value::makeCompound();
    result.set("sound_id", nbt::Value::makeString(sound.id));
    if (sound.fixedRange)
        result.set("range", nbt::Value::makeFloat(*sound.fixedRange));
    return result;
}

inline std::optional<std::int32_t> useActionId(std::string_view name) {
    static constexpr std::array<std::string_view, 10> names{
        "none", "eat", "drink", "block", "bow", "spear", "crossbow",
        "spyglass", "toot_horn", "brush"};
    const auto found = std::find(names.begin(), names.end(), name);
    if (found == names.end()) return std::nullopt;
    return static_cast<std::int32_t>(found - names.begin());
}

inline std::optional<std::string_view> useActionName(std::int32_t id) {
    static constexpr std::array<std::string_view, 10> names{
        "none", "eat", "drink", "block", "bow", "spear", "crossbow",
        "spyglass", "toot_horn", "brush"};
    if (id < 0 || static_cast<std::size_t>(id) >= names.size())
        return std::nullopt;
    return names[static_cast<std::size_t>(id)];
}

inline std::optional<std::int32_t> equipmentSlotId(std::string_view name) {
    if (name == "mainhand" || name == "main_hand") return 0;
    if (name == "offhand" || name == "off_hand") return 1;
    static constexpr std::array<std::string_view, 5> names{
        "feet", "legs", "chest", "head", "body"};
    const auto found = std::find(names.begin(), names.end(), name);
    if (found == names.end()) return std::nullopt;
    return static_cast<std::int32_t>(found - names.begin()) + 2;
}

inline std::optional<std::string_view> equipmentSlotName(std::int32_t id) {
    static constexpr std::array<std::string_view, 7> names{
        "mainhand", "offhand", "feet", "legs", "chest", "head", "body"};
    if (id < 0 || static_cast<std::size_t>(id) >= names.size())
        return std::nullopt;
    return names[static_cast<std::size_t>(id)];
}

inline bool validateEffectDetail(const nbt::Value& effect,
                                 std::size_t depth) {
    if (depth > item_component_nbt::kMaxEffectDepth)
        return false;
    if (depth == 0) {
        if (!onlyKeys(effect, {"id", "amplifier", "duration", "ambient",
                               "show_particles", "show_icon",
                               "hidden_effect"}))
            return false;
    } else if (!onlyKeys(effect, {"amplifier", "duration", "ambient",
                                 "show_particles", "show_icon",
                                 "hidden_effect"})) {
        return false;
    }
    for (const auto key : {"amplifier", "duration"}) {
        const auto* field = effect.get(key);
        if (field) {
            const auto value = item_component_nbt::integer(field);
            if (!value) return false;
            if (std::string_view(key) == "amplifier") {
                if (*value < 0 || *value > kMaxEffectAmplifier)
                    return false;
            } else if (*value < -1) {
                return false;
            }
        }
    }
    bool ignored = false;
    if (!booleanField(effect, "ambient", false, ignored) ||
        !booleanField(effect, "show_particles", true, ignored) ||
        !booleanField(effect, "show_icon", true, ignored))
        return false;
    const auto* hidden = effect.get("hidden_effect");
    return !hidden || validateEffectDetail(*hidden, depth + 1);
}

inline bool writePotionEffect(const nbt::Value& effect,
                             WriteBuffer& output) {
    if (!onlyKeys(effect, {"id", "amplifier", "duration", "ambient",
                           "show_particles", "show_icon", "hidden_effect"}))
        return false;
    const auto effectName = identifier(effect.get("id"));
    if (!effectName) return false;
    const auto id = registryId(RegistryKind::StatusEffect, *effectName);
    if (!id || *id <= 0) return false;
    if (!validateEffectDetail(effect, 0)) return false;
    output.varint(*id);
    if (!item_component_nbt::encodeEffectDetail(effect, output)) return false;
    return output.data.size() <= kMaximumPayloadBytes;
}

inline bool readEffectDetail(ReadBuffer& input, nbt::Value& effect,
                             std::size_t depth, std::size_t start) {
    if (depth > item_component_nbt::kMaxEffectDepth) return false;
    const auto amplifier = input.varint();
    const auto duration = input.varint();
    if (amplifier < 0 || amplifier > kMaxEffectAmplifier || duration < -1 ||
        !withinBudget(input, start))
        return false;
    bool ambient = false;
    bool showParticles = false;
    bool showIcon = false;
    if (!readBoolean(input, ambient) ||
        !readBoolean(input, showParticles) ||
        !readBoolean(input, showIcon))
        return false;
    effect.set("amplifier", nbt::Value::makeInt(amplifier));
    effect.set("duration", nbt::Value::makeInt(duration));
    effect.set("ambient", nbt::Value::makeByte(ambient ? 1 : 0));
    effect.set("show_particles",
               nbt::Value::makeByte(showParticles ? 1 : 0));
    effect.set("show_icon", nbt::Value::makeByte(showIcon ? 1 : 0));
    bool hasHidden = false;
    if (!readBoolean(input, hasHidden)) return false;
    if (hasHidden) {
        nbt::Value hidden = nbt::Value::makeCompound();
        if (!readEffectDetail(input, hidden, depth + 1, start)) return false;
        effect.set("hidden_effect", std::move(hidden));
    }
    return withinBudget(input, start);
}

inline std::optional<nbt::Value> readPotionEffect(ReadBuffer& input,
                                                   std::size_t start) {
    const auto id = input.varint();
    auto name = registryName(RegistryKind::StatusEffect, id);
    if (!name || !withinBudget(input, start)) return std::nullopt;
    nbt::Value effect = nbt::Value::makeCompound();
    effect.set("id", nbt::Value::makeString(std::move(*name)));
    if (!readEffectDetail(input, effect, 0, start)) return std::nullopt;
    return effect;
}

inline bool writeConsumeEffect(const nbt::Value& saved,
                               WriteBuffer& output, std::size_t start) {
    if (!onlyKeys(saved, {"type", "effects", "probability", "diameter",
                          "sound"}))
        return false;
    const auto type = identifier(saved.get("type"));
    if (!type) return false;
    constexpr std::string_view kMinecraftPrefix = "minecraft:";
    if (!std::string_view(*type).starts_with(kMinecraftPrefix)) return false;
    const auto effectType = std::string_view(*type).substr(
        kMinecraftPrefix.size());

    if (effectType == "apply_effects") {
        if (!onlyKeys(saved, {"type", "effects", "probability"}))
            return false;
        const auto* effects = saved.get("effects");
        if (!effects || effects->tag != nbt::List ||
            effects->list.size() > kMaximumEntries ||
            (!effects->list.empty() &&
             effects->listElement != nbt::Compound))
            return false;
        const auto probability = floatField(saved, "probability", 1.0F);
        if (!probability || *probability < 0.0F || *probability > 1.0F)
            return false;
        output.varint(0);
        output.varint(static_cast<std::int32_t>(effects->list.size()));
        for (const auto& effect : effects->list) {
            if (!writePotionEffect(effect, output) ||
                output.data.size() > kMaximumPayloadBytes)
                return false;
        }
        output.f32(*probability);
    } else if (effectType == "remove_effects") {
        if (!onlyKeys(saved, {"type", "effects"})) return false;
        const auto* effects = saved.get("effects");
        if (!effects) return false;
        output.varint(1);
        if (!writeIdSet(*effects, RegistryKind::StatusEffect, output))
            return false;
    } else if (effectType == "clear_all_effects") {
        if (!onlyKeys(saved, {"type"})) return false;
        output.varint(2);
    } else if (effectType == "teleport_randomly") {
        if (!onlyKeys(saved, {"type", "diameter"})) return false;
        const auto diameter = floatField(saved, "diameter", 16.0F);
        if (!diameter || *diameter < 0.0F) return false;
        output.varint(3);
        output.f32(*diameter);
    } else if (effectType == "play_sound") {
        if (!onlyKeys(saved, {"type", "sound"})) return false;
        output.varint(4);
        if (!writeSoundHolder(saved.get("sound"), output)) return false;
    } else {
        return false;
    }
    return output.data.size() - start <= kMaximumPayloadBytes;
}

inline std::optional<nbt::Value> readConsumeEffect(ReadBuffer& input,
                                                    std::size_t start) {
    const auto type = input.varint();
    if (type < 0 || type > 4 || !withinBudget(input, start))
        return std::nullopt;
    nbt::Value result = nbt::Value::makeCompound();
    static constexpr std::array<std::string_view, 5> names{
        "apply_effects", "remove_effects", "clear_all_effects",
        "teleport_randomly", "play_sound"};
    result.set("type", nbt::Value::makeString(
                           "minecraft:" +
                           std::string(names[static_cast<std::size_t>(type)])));

    if (type == 0) {
        const auto count = input.varint();
        if (count < 0 || static_cast<std::size_t>(count) > kMaximumEntries)
            return std::nullopt;
        nbt::Value effects = nbt::Value::makeList(
            nbt::Compound, static_cast<std::size_t>(count));
        for (std::int32_t index = 0; index < count; ++index) {
            auto effect = readPotionEffect(input, start);
            if (!effect) return std::nullopt;
            effects.list.push_back(std::move(*effect));
        }
        const auto probability = input.f32();
        if (!std::isfinite(probability) || probability < 0.0F ||
            probability > 1.0F || !withinBudget(input, start))
            return std::nullopt;
        result.set("effects", std::move(effects));
        result.set("probability", nbt::Value::makeFloat(probability));
    } else if (type == 1) {
        auto effects = readIdSet(input, RegistryKind::StatusEffect, start);
        if (!effects) return std::nullopt;
        result.set("effects", std::move(*effects));
    } else if (type == 3) {
        const auto diameter = input.f32();
        if (!std::isfinite(diameter) || diameter < 0.0F ||
            !withinBudget(input, start))
            return std::nullopt;
        result.set("diameter", nbt::Value::makeFloat(diameter));
    } else if (type == 4) {
        auto sound = readSoundHolder(input, start);
        if (!sound) return std::nullopt;
        result.set("sound", soundToNbt(*sound));
    }
    return withinBudget(input, start)
        ? std::optional<nbt::Value>(std::move(result))
        : std::nullopt;
}

inline bool validList(const nbt::Value* list, nbt::Tag elementTag,
                      std::size_t maxCount, bool allowEmpty = true) {
    if (!list || list->tag != nbt::List ||
        list->list.size() > maxCount ||
        (!allowEmpty && list->list.empty()))
        return false;
    return list->list.empty() ||
           list->listElement == elementTag;
}

inline bool writeEffectList(const nbt::Value* saved, WriteBuffer& output,
                            std::size_t start) {
    if (!validList(saved, nbt::Compound, kMaximumEntries)) return false;
    output.varint(static_cast<std::int32_t>(saved->list.size()));
    for (const auto& effect : saved->list) {
        if (!writeConsumeEffect(effect, output, start) ||
            output.data.size() - start > kMaximumPayloadBytes)
            return false;
    }
    return true;
}

inline std::optional<nbt::Value> readEffectList(ReadBuffer& input,
                                                std::size_t start) {
    const auto count = input.varint();
    if (count < 0 || static_cast<std::size_t>(count) > kMaximumEntries)
        return std::nullopt;
    nbt::Value effects = nbt::Value::makeList(
        nbt::Compound, static_cast<std::size_t>(count));
    for (std::int32_t index = 0; index < count; ++index) {
        auto effect = readConsumeEffect(input, start);
        if (!effect) return std::nullopt;
        effects.list.push_back(std::move(*effect));
    }
    return effects;
}

inline bool writeFood(const nbt::Value& saved, WriteBuffer& output) {
    if (!onlyKeys(saved,
                  {"nutrition", "saturation", "can_always_eat"}))
        return false;
    const auto nutrition = nonNegativeInteger(saved.get("nutrition"));
    const auto saturation = floatValue(saved.get("saturation"));
    bool canAlwaysEat = false;
    if (!nutrition || !saturation || *saturation < 0.0F ||
        !booleanField(saved, "can_always_eat", false, canAlwaysEat))
        return false;
    output.varint(*nutrition);
    output.f32(*saturation);
    output.boolean(canAlwaysEat);
    return true;
}

inline bool writeConsumable(const nbt::Value& saved, WriteBuffer& output,
                            std::size_t start) {
    if (!onlyKeys(saved, {"consume_seconds", "animation", "sound",
                          "has_consume_particles", "on_consume_effects"}))
        return false;
    const auto seconds = floatField(saved, "consume_seconds", 1.6F);
    if (!seconds || *seconds <= 0.0F) return false;

    std::string_view action = "eat";
    if (const auto* actionValue = saved.get("animation")) {
        if (actionValue->tag != nbt::String) return false;
        action = actionValue->str;
    }
    const auto actionId = useActionId(action);
    if (!actionId) return false;

    bool particles = true;
    if (!booleanField(saved, "has_consume_particles", true, particles))
        return false;
    output.f32(*seconds);
    output.varint(*actionId);

    const nbt::Value defaultSound =
        nbt::Value::makeString("minecraft:entity.generic.eat");
    const auto* sound = saved.get("sound");
    if (!writeSoundHolder(sound ? sound : &defaultSound, output))
        return false;
    output.boolean(particles);
    const auto* effectsValue = saved.get("on_consume_effects");
    const nbt::Value emptyEffects = nbt::Value::makeList(nbt::Compound);
    if (!writeEffectList(effectsValue ? effectsValue : &emptyEffects,
                         output, start))
        return false;
    return output.data.size() - start <= kMaximumPayloadBytes;
}

inline bool writeCooldown(const nbt::Value& saved, WriteBuffer& output) {
    if (!onlyKeys(saved, {"seconds", "cooldown_group"})) return false;
    const auto seconds = floatValue(saved.get("seconds"));
    if (!seconds || *seconds <= 0.0F) return false;
    output.f32(*seconds);
    const auto* group = saved.get("cooldown_group");
    output.boolean(group != nullptr);
    if (group) {
        const auto name = identifier(group);
        if (!name || !writeIdentifier(output, *name)) return false;
    }
    return true;
}

inline bool writeTool(const nbt::Value& saved, WriteBuffer& output,
                      std::size_t start) {
    if (!onlyKeys(saved, {"rules", "default_mining_speed",
                          "damage_per_block"}))
        return false;
    const auto* rules = saved.get("rules");
    if (!validList(rules, nbt::Compound, kMaximumEntries)) return false;
    const auto defaultSpeed = floatValue(
        saved.get("default_mining_speed"));
    const auto damagePerBlock =
        nonNegativeInteger(saved.get("damage_per_block"));
    if (!defaultSpeed || *defaultSpeed < 0.0F || !damagePerBlock)
        return false;

    output.varint(static_cast<std::int32_t>(rules->list.size()));
    for (const auto& rule : rules->list) {
        if (!onlyKeys(rule, {"blocks", "speed", "correct_for_drops"}))
            return false;
        const auto* blocks = rule.get("blocks");
        if (!blocks || !writeIdSet(*blocks, RegistryKind::Block, output))
            return false;
        const auto speed = floatField(rule, "speed", 0.0F);
        if (rule.get("speed") && (!speed || *speed < 0.0F))
            return false;
        output.boolean(speed.has_value() && rule.get("speed") != nullptr);
        if (rule.get("speed")) output.f32(*speed);

        const auto* correct = rule.get("correct_for_drops");
        output.boolean(correct != nullptr);
        if (correct) {
            if (correct->tag != nbt::Byte ||
                (correct->b != 0 && correct->b != 1))
                return false;
            output.boolean(correct->b != 0);
        }
        if (output.data.size() - start > kMaximumPayloadBytes) return false;
    }
    output.f32(*defaultSpeed);
    output.varint(*damagePerBlock);
    return output.data.size() - start <= kMaximumPayloadBytes;
}

inline bool writeEquippable(const nbt::Value& saved, WriteBuffer& output,
                            std::size_t start) {
    if (!onlyKeys(saved, {"slot", "equip_sound", "asset_id",
                          "camera_overlay", "allowed_entities",
                          "dispensable", "swappable", "damage_on_hurt"}))
        return false;
    const auto* slotValue = saved.get("slot");
    if (!slotValue || slotValue->tag != nbt::String) return false;
    const auto slot = equipmentSlotId(slotValue->str);
    if (!slot) return false;
    output.varint(*slot);

    const nbt::Value defaultSound =
        nbt::Value::makeString("minecraft:item.armor.equip_generic");
    const auto* sound = saved.get("equip_sound");
    if (!writeSoundHolder(sound ? sound : &defaultSound, output))
        return false;

    for (const auto key : {"asset_id", "camera_overlay"}) {
        const auto* value = saved.get(key);
        output.boolean(value != nullptr);
        if (value) {
            const auto name = identifier(value);
            if (!name || !writeIdentifier(output, *name)) return false;
        }
    }
    const auto* allowed = saved.get("allowed_entities");
    output.boolean(allowed != nullptr);
    if (allowed &&
        !writeIdSet(*allowed, RegistryKind::EntityType, output))
        return false;

    bool dispensable = true;
    bool swappable = true;
    bool damageOnHurt = true;
    if (!booleanField(saved, "dispensable", true, dispensable) ||
        !booleanField(saved, "swappable", true, swappable) ||
        !booleanField(saved, "damage_on_hurt", true, damageOnHurt))
        return false;
    output.boolean(dispensable);
    output.boolean(swappable);
    output.boolean(damageOnHurt);
    return output.data.size() - start <= kMaximumPayloadBytes;
}

inline std::optional<nbt::Value> decode(ReadBuffer& input, std::uint32_t id,
                                        std::size_t depth) {
    if (depth > item_component_nbt::kMaxComponentDepth) return std::nullopt;
    const auto start = input.off;
    try {
        nbt::Value result;
        switch (id) {
        case 21: {
            const auto nutrition = input.varint();
            const auto saturation = input.f32();
            bool canAlwaysEat = false;
            if (nutrition < 0 || !std::isfinite(saturation) ||
                saturation < 0.0F ||
                !readBoolean(input, canAlwaysEat))
                return std::nullopt;
            result = nbt::Value::makeCompound();
            result.set("nutrition", nbt::Value::makeInt(nutrition));
            result.set("saturation", nbt::Value::makeFloat(saturation));
            result.set("can_always_eat",
                       nbt::Value::makeByte(canAlwaysEat ? 1 : 0));
            break;
        }
        case 22: {
            const auto seconds = input.f32();
            const auto actionId = input.varint();
            const auto action = useActionName(actionId);
            auto sound = readSoundHolder(input, start);
            bool particles = false;
            if (!std::isfinite(seconds) || seconds <= 0.0F || !action ||
                !sound || !readBoolean(input, particles))
                return std::nullopt;
            auto effects = readEffectList(input, start);
            if (!effects) return std::nullopt;
            result = nbt::Value::makeCompound();
            result.set("consume_seconds", nbt::Value::makeFloat(seconds));
            result.set("animation",
                       nbt::Value::makeString(std::string(*action)));
            result.set("sound", soundToNbt(*sound));
            result.set("has_consume_particles",
                       nbt::Value::makeByte(particles ? 1 : 0));
            result.set("on_consume_effects", std::move(*effects));
            break;
        }
        case 23: {
            if (depth >= item_component_nbt::kMaxComponentDepth)
                return std::nullopt;
            auto stack = ItemStack::read(input, depth + 1);
            if (stack.empty()) return std::nullopt;
            result = stack.toNbt(-1, depth + 1);
            break;
        }
        case 24: {
            const auto seconds = input.f32();
            if (!std::isfinite(seconds) || seconds <= 0.0F)
                return std::nullopt;
            bool hasGroup = false;
            if (!readBoolean(input, hasGroup)) return std::nullopt;
            result = nbt::Value::makeCompound();
            result.set("seconds", nbt::Value::makeFloat(seconds));
            if (hasGroup) {
                auto group = readIdentifier(input);
                if (!group) return std::nullopt;
                result.set("cooldown_group",
                           nbt::Value::makeString(std::move(*group)));
            }
            break;
        }
        case 26: {
            const auto count = input.varint();
            if (count < 0 ||
                static_cast<std::size_t>(count) > kMaximumEntries)
                return std::nullopt;
            nbt::Value rules = nbt::Value::makeList(
                nbt::Compound, static_cast<std::size_t>(count));
            for (std::int32_t index = 0; index < count; ++index) {
                auto blocks = readIdSet(input, RegistryKind::Block, start);
                if (!blocks) return std::nullopt;
                nbt::Value rule = nbt::Value::makeCompound();
                rule.set("blocks", std::move(*blocks));
                bool hasSpeed = false;
                if (!readBoolean(input, hasSpeed)) return std::nullopt;
                if (hasSpeed) {
                    const auto speed = input.f32();
                    if (!std::isfinite(speed) || speed < 0.0F)
                        return std::nullopt;
                    rule.set("speed", nbt::Value::makeFloat(speed));
                }
                bool hasCorrectForDrops = false;
                if (!readBoolean(input, hasCorrectForDrops))
                    return std::nullopt;
                if (hasCorrectForDrops) {
                    bool correctForDrops = false;
                    if (!readBoolean(input, correctForDrops))
                        return std::nullopt;
                    rule.set("correct_for_drops",
                             nbt::Value::makeByte(correctForDrops ? 1 : 0));
                }
                if (!withinBudget(input, start)) return std::nullopt;
                rules.list.push_back(std::move(rule));
            }
            const auto defaultSpeed = input.f32();
            const auto damagePerBlock = input.varint();
            if (!std::isfinite(defaultSpeed) || defaultSpeed < 0.0F ||
                damagePerBlock < 0)
                return std::nullopt;
            result = nbt::Value::makeCompound();
            result.set("rules", std::move(rules));
            result.set("default_mining_speed",
                       nbt::Value::makeFloat(defaultSpeed));
            result.set("damage_per_block",
                       nbt::Value::makeInt(damagePerBlock));
            break;
        }
        case 28: {
            const auto slotId = input.varint();
            const auto slot = equipmentSlotName(slotId);
            auto sound = readSoundHolder(input, start);
            if (!slot || !sound) return std::nullopt;
            result = nbt::Value::makeCompound();
            result.set("slot",
                       nbt::Value::makeString(std::string(*slot)));
            result.set("equip_sound", soundToNbt(*sound));

            for (const auto key : {"asset_id", "camera_overlay"}) {
                bool present = false;
                if (!readBoolean(input, present)) return std::nullopt;
                if (present) {
                    auto name = readIdentifier(input);
                    if (!name) return std::nullopt;
                    result.set(key,
                               nbt::Value::makeString(std::move(*name)));
                }
            }
            bool hasAllowedEntities = false;
            if (!readBoolean(input, hasAllowedEntities))
                return std::nullopt;
            if (hasAllowedEntities) {
                auto allowed = readIdSet(input, RegistryKind::EntityType,
                                         start);
                if (!allowed) return std::nullopt;
                result.set("allowed_entities", std::move(*allowed));
            }
            for (const auto key : {"dispensable", "swappable",
                                   "damage_on_hurt"}) {
                bool value = false;
                if (!readBoolean(input, value)) return std::nullopt;
                result.set(key, nbt::Value::makeByte(value ? 1 : 0));
            }
            break;
        }
        case 29: {
            auto items = readIdSet(input, RegistryKind::Item, start);
            if (!items) return std::nullopt;
            result = nbt::Value::makeCompound();
            result.set("items", std::move(*items));
            break;
        }
        case 32: {
            auto effects = readEffectList(input, start);
            if (!effects) return std::nullopt;
            result = nbt::Value::makeCompound();
            result.set("death_effects", std::move(*effects));
            break;
        }
        default:
            return std::nullopt;
        }
        if (!withinBudget(input, start)) return std::nullopt;
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace detail

// Parse and validate one component from a shared packet reader. It consumes
// exactly that component's grammar; use consumeExact for a standalone payload.
inline bool consume(ReadBuffer& input, std::uint32_t id,
                    std::size_t depth = 0) {
    return detail::decode(input, id, depth).has_value();
}

inline bool consumeExact(const std::vector<std::uint8_t>& payload,
                         std::uint32_t id, std::size_t depth = 0) {
    if (payload.size() > kMaximumPayloadBytes) return false;
    try {
        ReadBuffer input(payload);
        return consume(input, id, depth) && input.remaining() == 0;
    } catch (const std::exception&) {
        return false;
    }
}

inline std::optional<std::vector<std::uint8_t>> fromNbt(
    std::uint32_t id, const nbt::Value& saved, std::size_t depth = 0) {
    if (depth > item_component_nbt::kMaxComponentDepth) return std::nullopt;
    try {
        WriteBuffer output;
        switch (id) {
        case 21:
            if (!detail::writeFood(saved, output)) return std::nullopt;
            break;
        case 22:
            if (!detail::writeConsumable(saved, output, 0))
                return std::nullopt;
            break;
        case 23: {
            if (depth >= item_component_nbt::kMaxComponentDepth)
                return std::nullopt;
            auto stack = ItemStack::fromNbt(saved, depth + 1);
            if (!stack || stack->empty()) return std::nullopt;
            stack->write(output);
            break;
        }
        case 24:
            if (!detail::writeCooldown(saved, output))
                return std::nullopt;
            break;
        case 26:
            if (!detail::writeTool(saved, output, 0))
                return std::nullopt;
            break;
        case 28:
            if (!detail::writeEquippable(saved, output, 0))
                return std::nullopt;
            break;
        case 29:
            if (!detail::onlyKeys(saved, {"items"})) return std::nullopt;
            if (!detail::writeIdSet(*saved.get("items"),
                                    detail::RegistryKind::Item, output))
                return std::nullopt;
            break;
        case 32:
            if (!detail::onlyKeys(saved, {"death_effects"}))
                return std::nullopt;
            if (!detail::writeEffectList(saved.get("death_effects"),
                                         output, 0))
                return std::nullopt;
            break;
        default:
            return std::nullopt;
        }
        if (output.data.size() > kMaximumPayloadBytes)
            return std::nullopt;
        return std::move(output.data);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline std::optional<nbt::Value> toNbt(
    std::uint32_t id, const std::vector<std::uint8_t>& payload,
    std::size_t depth = 0) {
    if (payload.size() > kMaximumPayloadBytes ||
        depth > item_component_nbt::kMaxComponentDepth)
        return std::nullopt;
    try {
        ReadBuffer input(payload);
        auto result = detail::decode(input, id, depth);
        if (!result || input.remaining() != 0) return std::nullopt;
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace cppfm::item_component_nbt::additional_food
