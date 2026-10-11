// Converts supported 1.21.4 item components between saved item NBT and their
// protocol-769 payloads. Unknown component data remains opaque and is retained
// by the owning item record.
#pragma once

#include "../core/ByteBuffer.hpp"
#include "../core/NBTValue.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/BlockStates.hpp"
#include "../generated/BlockRegistryIds.hpp"
#include "../core/Utf8.hpp"
#include "MobEffects.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cppfm::item_component_nbt {

inline constexpr std::string_view kPotionContents = "minecraft:potion_contents";
inline constexpr std::size_t kMaxCustomEffects = 1024;
inline constexpr std::size_t kMaxEffectDepth = 32;
inline constexpr std::size_t kMaxComponentDepth = 32;
inline constexpr std::size_t kMaxContainerSlots = 256;
inline constexpr std::size_t kMaxNestedItemComponents = 4096;
inline constexpr std::size_t kMaxProtocolComponentPayloadBytes = 1024u * 1024u;
inline constexpr std::size_t kMaxBookPages = 100;
inline constexpr std::size_t kMaxBookTitleUtf16Units = 32;
inline constexpr std::size_t kMaxWritableBookPageUtf16Units = 1024;
inline constexpr std::size_t kMaxPacketStringUtf16Units = 32767;

inline bool isKnownItemId(std::int32_t id) {
    if (id <= 0) return false;
    static const auto knownItemIds = [] {
        std::unordered_set<std::uint32_t> ids;
        ids.reserve(gen::kItems.size());
        for (const auto& item : gen::kItems) ids.insert(item.second);
        return ids;
    }();
    return knownItemIds.contains(static_cast<std::uint32_t>(id));
}

inline constexpr std::array<std::string_view, 46> kPotionNames{
    "minecraft:water", "minecraft:mundane", "minecraft:thick",
    "minecraft:awkward", "minecraft:night_vision",
    "minecraft:long_night_vision", "minecraft:invisibility",
    "minecraft:long_invisibility", "minecraft:leaping",
    "minecraft:long_leaping", "minecraft:strong_leaping",
    "minecraft:fire_resistance", "minecraft:long_fire_resistance",
    "minecraft:swiftness", "minecraft:long_swiftness",
    "minecraft:strong_swiftness", "minecraft:slowness",
    "minecraft:long_slowness", "minecraft:strong_slowness",
    "minecraft:water_breathing", "minecraft:long_water_breathing",
    "minecraft:healing", "minecraft:strong_healing", "minecraft:harming",
    "minecraft:strong_harming", "minecraft:poison",
    "minecraft:long_poison", "minecraft:strong_poison",
    "minecraft:regeneration", "minecraft:long_regeneration",
    "minecraft:strong_regeneration", "minecraft:strength",
    "minecraft:long_strength", "minecraft:strong_strength",
    "minecraft:weakness", "minecraft:long_weakness", "minecraft:luck",
    "minecraft:turtle_master", "minecraft:long_turtle_master",
    "minecraft:strong_turtle_master", "minecraft:slow_falling",
    "minecraft:long_slow_falling", "minecraft:wind_charged",
    "minecraft:weaving", "minecraft:oozing", "minecraft:infested"};

inline constexpr std::array<std::string_view, 42> kEnchantmentNames{
    "minecraft:aqua_affinity", "minecraft:bane_of_arthropods",
    "minecraft:binding_curse", "minecraft:blast_protection",
    "minecraft:breach", "minecraft:channeling", "minecraft:density",
    "minecraft:depth_strider", "minecraft:efficiency",
    "minecraft:feather_falling", "minecraft:fire_aspect",
    "minecraft:fire_protection", "minecraft:flame", "minecraft:fortune",
    "minecraft:frost_walker", "minecraft:impaling", "minecraft:infinity",
    "minecraft:knockback", "minecraft:looting", "minecraft:loyalty",
    "minecraft:luck_of_the_sea", "minecraft:lure", "minecraft:mending",
    "minecraft:multishot", "minecraft:piercing", "minecraft:power",
    "minecraft:projectile_protection", "minecraft:protection",
    "minecraft:punch", "minecraft:quick_charge", "minecraft:respiration",
    "minecraft:riptide", "minecraft:sharpness", "minecraft:silk_touch",
    "minecraft:smite", "minecraft:soul_speed", "minecraft:sweeping_edge",
    "minecraft:swift_sneak", "minecraft:thorns", "minecraft:unbreaking",
    "minecraft:vanishing_curse", "minecraft:wind_burst"};

inline constexpr std::array<std::string_view, 18> kTrimPatternNames{
    "minecraft:bolt", "minecraft:coast", "minecraft:dune", "minecraft:eye",
    "minecraft:flow", "minecraft:host", "minecraft:raiser", "minecraft:rib",
    "minecraft:sentry", "minecraft:shaper", "minecraft:silence",
    "minecraft:snout", "minecraft:spire", "minecraft:tide", "minecraft:vex",
    "minecraft:ward", "minecraft:wayfinder", "minecraft:wild"};

inline constexpr std::array<std::string_view, 11> kTrimMaterialNames{
    "minecraft:amethyst", "minecraft:copper", "minecraft:diamond",
    "minecraft:emerald", "minecraft:gold", "minecraft:iron",
    "minecraft:lapis", "minecraft:netherite", "minecraft:quartz",
    "minecraft:redstone", "minecraft:resin"};

inline std::optional<std::int32_t> registryId(
    std::string_view name, const auto& registry) {
    std::string qualified;
    if (name.find(':') == std::string_view::npos) {
        qualified.reserve(name.size() + 10);
        qualified = "minecraft:";
        qualified.append(name);
        name = qualified;
    }
    const auto it = std::find(registry.begin(), registry.end(), name);
    if (it == registry.end()) return std::nullopt;
    return static_cast<std::int32_t>(it - registry.begin());
}

inline std::optional<std::int32_t> enchantmentId(std::string_view name) {
    if (name == "minecraft:binding" || name == "binding") return 2;
    if (name == "minecraft:vanishing" || name == "vanishing") return 40;
    return registryId(name, kEnchantmentNames);
}

inline std::optional<std::string_view> enchantmentName(std::int32_t id) {
    if (id < 0 || static_cast<std::size_t>(id) >= kEnchantmentNames.size())
        return std::nullopt;
    return kEnchantmentNames[static_cast<std::size_t>(id)];
}

inline std::optional<std::int32_t> trimPatternId(std::string_view name) {
    return registryId(name, kTrimPatternNames);
}

inline std::optional<std::string_view> trimPatternName(std::int32_t id) {
    if (id < 0 || static_cast<std::size_t>(id) >= kTrimPatternNames.size())
        return std::nullopt;
    return kTrimPatternNames[static_cast<std::size_t>(id)];
}

inline std::optional<std::int32_t> trimMaterialId(std::string_view name) {
    return registryId(name, kTrimMaterialNames);
}

inline std::optional<std::string_view> trimMaterialName(std::int32_t id) {
    if (id < 0 || static_cast<std::size_t>(id) >= kTrimMaterialNames.size())
        return std::nullopt;
    return kTrimMaterialNames[static_cast<std::size_t>(id)];
}

inline std::optional<std::int32_t> potionId(std::string_view name) {
    std::string qualified;
    if (name.find(':') == std::string_view::npos) {
        qualified.reserve(name.size() + 10);
        qualified = "minecraft:";
        qualified.append(name);
        name = qualified;
    }
    const auto it = std::find(kPotionNames.begin(), kPotionNames.end(), name);
    if (it == kPotionNames.end()) return std::nullopt;
    return static_cast<std::int32_t>(it - kPotionNames.begin());
}

inline std::optional<std::string_view> potionName(std::int32_t id) {
    if (id < 0 || static_cast<std::size_t>(id) >= kPotionNames.size())
        return std::nullopt;
    return kPotionNames[static_cast<std::size_t>(id)];
}

inline std::optional<std::int32_t> integer(const nbt::Value* value) {
    if (!value) return std::nullopt;
    switch (value->tag) {
    case nbt::Byte: return value->b;
    case nbt::Short: return value->s;
    case nbt::Int: return value->i;
    default: return std::nullopt;
    }
}

inline bool boolean(const nbt::Value* value, bool fallback, bool& result) {
    if (!value) {
        result = fallback;
        return true;
    }
    if (value->tag == nbt::Byte) {
        result = value->b != 0;
        return true;
    }
    if (value->tag == nbt::Int) {
        result = value->i != 0;
        return true;
    }
    return false;
}

inline bool encodeEffectDetail(const nbt::Value& effect, WriteBuffer& output,
                              std::size_t depth = 0) {
    if (effect.tag != nbt::Compound || depth > kMaxEffectDepth) return false;

    const auto* amplifierTag = effect.get("amplifier");
    const auto* durationTag = effect.get("duration");
    const auto amplifierValue = integer(amplifierTag);
    const auto durationValue = integer(durationTag);
    if ((amplifierTag && !amplifierValue) ||
        (durationTag && !durationValue))
        return false;
    const auto amplifier = amplifierValue.value_or(0);
    const auto duration = durationValue.value_or(0);
    if (amplifier < 0 || duration < 0) return false;

    bool ambient = false;
    bool showParticles = true;
    bool showIcon = true;
    if (!boolean(effect.get("ambient"), false, ambient) ||
        !boolean(effect.get("show_particles"), true, showParticles) ||
        !boolean(effect.get("show_icon"), true, showIcon))
        return false;

    output.varint(amplifier);
    output.varint(duration);
    output.boolean(ambient);
    output.boolean(showParticles);
    output.boolean(showIcon);

    const auto* hidden = effect.get("hidden_effect");
    if (!hidden) {
        output.boolean(false);
        return true;
    }
    output.boolean(true);
    return encodeEffectDetail(*hidden, output, depth + 1);
}

inline std::optional<std::vector<std::uint8_t>> potionContentsToProtocol(
    const nbt::Value* savedComponents) {
    if (!savedComponents || savedComponents->tag != nbt::Compound)
        return std::nullopt;
    const auto* saved = savedComponents->get(kPotionContents);
    if (!saved) return std::nullopt;

    WriteBuffer output;
    if (saved->tag == nbt::String) {
        const auto id = potionId(saved->str);
        if (!id) return std::nullopt;
        output.boolean(true);
        output.varint(*id);
        output.boolean(false);
        output.varint(0);
        output.boolean(false);
        return std::move(output.data);
    }
    if (saved->tag != nbt::Compound) return std::nullopt;

    const auto* potion = saved->get("potion");
    if (potion) {
        if (potion->tag != nbt::String) return std::nullopt;
        const auto id = potionId(potion->str);
        if (!id) return std::nullopt;
        output.boolean(true);
        output.varint(*id);
    } else {
        output.boolean(false);
    }

    const auto* color = saved->get("custom_color");
    if (color) {
        if (color->tag != nbt::Int) return std::nullopt;
        output.boolean(true);
        output.i32(color->i);
    } else {
        output.boolean(false);
    }

    const auto* customEffects = saved->get("custom_effects");
    if (customEffects && customEffects->tag != nbt::List)
        return std::nullopt;
    const std::size_t effectCount = customEffects ? customEffects->list.size() : 0;
    if (effectCount > kMaxCustomEffects) return std::nullopt;
    output.varint(static_cast<std::int32_t>(effectCount));
    if (customEffects) {
        for (const auto& effect : customEffects->list) {
            const auto* idTag = effect.get("id");
            if (!idTag || idTag->tag != nbt::String) return std::nullopt;
            const auto idIt = effects::byName().find(idTag->str);
            if (idIt == effects::byName().end()) return std::nullopt;
            output.varint(idIt->second);
            if (!encodeEffectDetail(effect, output)) return std::nullopt;
        }
    }

    const auto* customName = saved->get("custom_name");
    if (customName) {
        if (customName->tag != nbt::String) return std::nullopt;
        output.boolean(true);
        output.string(customName->str);
    } else {
        output.boolean(false);
    }
    return std::move(output.data);
}

inline bool decodeEffectDetail(ReadBuffer& input, nbt::Value& effect,
                               std::size_t depth = 0) {
    if (depth > kMaxEffectDepth) return false;
    const auto amplifier = input.varint();
    const auto duration = input.varint();
    if (amplifier < 0 || duration < 0) return false;
    effect.set("amplifier", nbt::Value::makeInt(amplifier));
    effect.set("duration", nbt::Value::makeInt(duration));
    effect.set("ambient", nbt::Value::makeByte(input.boolean() ? 1 : 0));
    effect.set("show_particles",
               nbt::Value::makeByte(input.boolean() ? 1 : 0));
    effect.set("show_icon", nbt::Value::makeByte(input.boolean() ? 1 : 0));
    if (input.boolean()) {
        nbt::Value hidden = nbt::Value::makeCompound();
        if (!decodeEffectDetail(input, hidden, depth + 1)) return false;
        effect.set("hidden_effect", std::move(hidden));
    }
    return true;
}

inline void erase(nbt::Value& compound, std::string_view key) {
    if (compound.tag != nbt::Compound) return;
    compound.comp.erase(std::remove_if(compound.comp.begin(), compound.comp.end(),
        [key](const auto& field) { return field.first == key; }),
        compound.comp.end());
}

inline bool applyPotionContentsFromProtocol(
    nbt::Value& item, const std::vector<std::uint8_t>& payload) {
    try {
        ReadBuffer input(payload);
        const bool hasPotion = input.boolean();
        std::optional<std::string_view> potion;
        if (hasPotion) potion = potionName(input.varint());
        if (hasPotion && !potion) return false;

        const bool hasColor = input.boolean();
        std::optional<std::int32_t> color;
        if (hasColor) color = input.i32();

        const auto effectCount = input.varint();
        if (effectCount < 0 ||
            static_cast<std::size_t>(effectCount) > kMaxCustomEffects)
            return false;
        nbt::Value customEffects = nbt::Value::makeList(nbt::Compound);
        customEffects.list.reserve(static_cast<std::size_t>(effectCount));
        for (std::int32_t i = 0; i < effectCount; ++i) {
            const auto effectId = input.varint();
            if (effectId <= 0 || effectId > effects::Infested) return false;
            nbt::Value effect = nbt::Value::makeCompound();
            effect.set("id", nbt::Value::makeString(effects::nameOf(
                static_cast<std::uint8_t>(effectId))));
            if (!decodeEffectDetail(input, effect)) return false;
            customEffects.list.push_back(std::move(effect));
        }

        const bool hasName = input.boolean();
        std::optional<std::string> customName;
        if (hasName) customName = input.string(32767);
        if (input.remaining() != 0) return false;

        nbt::Value* components = item.get("components");
        if (!components || components->tag != nbt::Compound) {
            item.set("components", nbt::Value::makeCompound());
            components = item.get("components");
        }
        nbt::Value* potionData = components->get(kPotionContents);
        if (!potionData || potionData->tag != nbt::Compound) {
            components->set(std::string(kPotionContents), nbt::Value::makeCompound());
            potionData = components->get(kPotionContents);
        }

        if (potion) potionData->set(
            "potion", nbt::Value::makeString(std::string(*potion)));
        else erase(*potionData, "potion");
        if (color) potionData->set("custom_color", nbt::Value::makeInt(*color));
        else erase(*potionData, "custom_color");
        if (!customEffects.list.empty())
            potionData->set("custom_effects", std::move(customEffects));
        else
            erase(*potionData, "custom_effects");
        if (customName)
            potionData->set("custom_name",
                            nbt::Value::makeString(std::move(*customName)));
        else
            erase(*potionData, "custom_name");
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

struct ProtocolComponent {
    std::uint32_t id = 0;
    std::vector<std::uint8_t> payload;
};

inline std::optional<std::vector<std::uint8_t>> containerComponentNbtToProtocol(
    const nbt::Value& saved, std::size_t depth);
inline std::optional<nbt::Value> containerComponentProtocolToNbt(
    const std::vector<std::uint8_t>& payload, std::size_t depth);

inline std::optional<std::uint32_t> componentId(std::string_view name) {
    if (name == "minecraft:custom_data") return 0;
    if (name == "minecraft:max_stack_size") return 1;
    if (name == "minecraft:max_damage") return 2;
    if (name == "minecraft:damage") return 3;
    if (name == "minecraft:unbreakable") return 4;
    if (name == "minecraft:custom_name") return 5;
    if (name == "minecraft:item_name") return 6;
    if (name == "minecraft:item_model") return 7;
    if (name == "minecraft:lore") return 8;
    if (name == "minecraft:rarity") return 9;
    if (name == "minecraft:enchantments") return 10;
    if (name == "minecraft:can_place_on") return 11;
    if (name == "minecraft:can_break") return 12;
    if (name == "minecraft:custom_model_data") return 14;
    if (name == "minecraft:hide_additional_tooltip") return 15;
    if (name == "minecraft:hide_tooltip") return 16;
    if (name == "minecraft:repair_cost") return 17;
    if (name == "minecraft:creative_slot_lock") return 18;
    if (name == "minecraft:enchantment_glint_override") return 19;
    if (name == "minecraft:intangible_projectile") return 20;
    if (name == "minecraft:damage_resistant") return 25;
    if (name == "minecraft:enchantable") return 27;
    if (name == "minecraft:glider") return 30;
    if (name == "minecraft:tooltip_style") return 31;
    if (name == "minecraft:stored_enchantments") return 33;
    if (name == "minecraft:dyed_color") return 34;
    if (name == "minecraft:map_color") return 35;
    if (name == "minecraft:map_id") return 36;
    if (name == "minecraft:potion_contents") return 41;
    if (name == "minecraft:suspicious_stew_effects") return 42;
    if (name == "minecraft:writable_book_content") return 43;
    if (name == "minecraft:written_book_content") return 44;
    if (name == "minecraft:trim") return 45;
    if (name == "minecraft:map_decorations") return 37;
    if (name == "minecraft:map_post_processing") return 38;
    if (name == "minecraft:debug_stick_state") return 46;
    if (name == "minecraft:entity_data") return 47;
    if (name == "minecraft:bucket_entity_data") return 48;
    if (name == "minecraft:block_entity_data") return 49;
    if (name == "minecraft:ominous_bottle_amplifier") return 51;
    if (name == "minecraft:recipes") return 53;
    if (name == "minecraft:note_block_sound") return 58;
    if (name == "minecraft:base_color") return 60;
    if (name == "minecraft:container") return 62;
    if (name == "minecraft:lock") return 65;
    if (name == "minecraft:container_loot") return 66;
    return std::nullopt;
}

inline std::optional<std::string_view> componentName(std::uint32_t id) {
    switch (id) {
    case 0: return "minecraft:custom_data";
    case 1: return "minecraft:max_stack_size";
    case 2: return "minecraft:max_damage";
    case 3: return "minecraft:damage";
    case 4: return "minecraft:unbreakable";
    case 5: return "minecraft:custom_name";
    case 6: return "minecraft:item_name";
    case 7: return "minecraft:item_model";
    case 8: return "minecraft:lore";
    case 9: return "minecraft:rarity";
    case 10: return "minecraft:enchantments";
    case 11: return "minecraft:can_place_on";
    case 12: return "minecraft:can_break";
    case 14: return "minecraft:custom_model_data";
    case 15: return "minecraft:hide_additional_tooltip";
    case 16: return "minecraft:hide_tooltip";
    case 17: return "minecraft:repair_cost";
    case 18: return "minecraft:creative_slot_lock";
    case 19: return "minecraft:enchantment_glint_override";
    case 20: return "minecraft:intangible_projectile";
    case 25: return "minecraft:damage_resistant";
    case 27: return "minecraft:enchantable";
    case 30: return "minecraft:glider";
    case 31: return "minecraft:tooltip_style";
    case 33: return "minecraft:stored_enchantments";
    case 34: return "minecraft:dyed_color";
    case 35: return "minecraft:map_color";
    case 36: return "minecraft:map_id";
    case 41: return kPotionContents;
    case 42: return "minecraft:suspicious_stew_effects";
    case 43: return "minecraft:writable_book_content";
    case 44: return "minecraft:written_book_content";
    case 45: return "minecraft:trim";
    case 37: return "minecraft:map_decorations";
    case 38: return "minecraft:map_post_processing";
    case 46: return "minecraft:debug_stick_state";
    case 47: return "minecraft:entity_data";
    case 48: return "minecraft:bucket_entity_data";
    case 49: return "minecraft:block_entity_data";
    case 51: return "minecraft:ominous_bottle_amplifier";
    case 53: return "minecraft:recipes";
    case 58: return "minecraft:note_block_sound";
    case 60: return "minecraft:base_color";
    case 62: return "minecraft:container";
    case 65: return "minecraft:lock";
    case 66: return "minecraft:container_loot";
    default: return std::nullopt;
    }
}

inline std::optional<nbt::Value> readAnonymousNbt(ReadBuffer& input) {
    try {
        nbt::Parser parser(input);
        return parser.readAnonymousRoot();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline std::optional<nbt::Value> readAnonymousNbt(
    const std::vector<std::uint8_t>& bytes) {
    try {
        ReadBuffer input(bytes);
        auto decoded = readAnonymousNbt(input);
        if (!decoded || input.remaining() != 0) return std::nullopt;
        return decoded;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline std::vector<std::uint8_t> writeAnonymousNbt(const nbt::Value& value) {
    if (!nbt::isValidTag(value.tag) || value.tag == nbt::End)
        throw std::invalid_argument("invalid item component NBT root");
    WriteBuffer output;
    output.u8(static_cast<std::uint8_t>(value.tag));
    nbt::writePayload(output, value);
    return std::move(output.data);
}

inline bool writeOptionalAnonymousNbt(const nbt::Value* value,
                                      WriteBuffer& output) {
    if (!value) {
        output.u8(nbt::End);
        return true;
    }
    try {
        const auto bytes = writeAnonymousNbt(*value);
        output.raw(bytes.data(), bytes.size());
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

inline bool readOptionalAnonymousNbt(ReadBuffer& input,
                                     std::optional<nbt::Value>& value) {
    if (input.remaining() == 0) return false;
    if (input.p[input.off] == nbt::End) {
        ++input.off;
        value.reset();
        return true;
    }
    auto decoded = readAnonymousNbt(input);
    if (!decoded) return false;
    value = std::move(*decoded);
    return true;
}

inline bool isResourceIdentifier(std::string_view value) {
    const auto separator = value.find(':');
    if (separator != std::string_view::npos &&
        value.find(':', separator + 1) != std::string_view::npos)
        return false;
    const auto namespacePart = separator == std::string_view::npos
        ? std::string_view("minecraft") : value.substr(0, separator);
    const auto pathPart = separator == std::string_view::npos
        ? value : value.substr(separator + 1);
    if (namespacePart.empty() || pathPart.empty()) return false;
    const auto validNamespaceChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.' || c == '-';
    };
    const auto validPathChar = [&](char c) {
        return validNamespaceChar(c) || c == '/';
    };
    return std::all_of(namespacePart.begin(), namespacePart.end(),
                       validNamespaceChar) &&
           std::all_of(pathPart.begin(), pathPart.end(), validPathChar);
}

inline bool withinStringLength(std::string_view value,
                               std::size_t maxUtf16Units) noexcept {
    const auto length = utf8::utf16Length(value);
    return length && *length <= maxUtf16Units;
}

inline std::size_t maximumUtf8Bytes(std::size_t maxUtf16Units) noexcept {
    constexpr auto max = std::numeric_limits<std::size_t>::max();
    return maxUtf16Units > max / 3 ? max : maxUtf16Units * 3;
}

inline bool writeProtocolString(WriteBuffer& output, const std::string& value,
                                std::size_t maxUtf16Units) {
    if (!withinStringLength(value, maxUtf16Units)) return false;
    output.string(value);
    return true;
}

inline std::optional<std::string> readProtocolString(
    ReadBuffer& input, std::size_t maxUtf16Units) {
    try {
        auto value = input.string(maximumUtf8Bytes(maxUtf16Units));
        if (!withinStringLength(value, maxUtf16Units)) return std::nullopt;
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline bool writeFilteredStringPair(const nbt::Value& saved,
                                    std::size_t maxUtf16Units,
                                    WriteBuffer& output) {
    const nbt::Value* raw = &saved;
    const nbt::Value* filtered = nullptr;
    if (saved.tag == nbt::Compound) {
        raw = saved.get("raw");
        filtered = saved.get("filtered");
    }
    if (!raw || raw->tag != nbt::String ||
        !withinStringLength(raw->str, maxUtf16Units) ||
        (filtered && (filtered->tag != nbt::String ||
                      !withinStringLength(filtered->str, maxUtf16Units))))
        return false;
    output.string(raw->str);
    output.boolean(filtered != nullptr);
    if (filtered) output.string(filtered->str);
    return true;
}

inline std::optional<nbt::Value> readFilteredStringPair(
    ReadBuffer& input, std::size_t maxUtf16Units) {
    try {
        auto raw = readProtocolString(input, maxUtf16Units);
        if (!raw) return std::nullopt;
        const bool hasFiltered = input.boolean();
        nbt::Value pair = nbt::Value::makeCompound();
        pair.set("raw", nbt::Value::makeString(std::move(*raw)));
        if (hasFiltered) {
            auto filtered = readProtocolString(input, maxUtf16Units);
            if (!filtered) return std::nullopt;
            pair.set("filtered", nbt::Value::makeString(
                                     std::move(*filtered)));
        }
        return pair;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline bool writeFilteredTextPair(const nbt::Value& saved,
                                 WriteBuffer& output) {
    const nbt::Value* raw = &saved;
    const nbt::Value* filtered = nullptr;
    if (saved.tag == nbt::Compound) {
        raw = saved.get("raw");
        filtered = saved.get("filtered");
    }
    if (!raw) return false;
    try {
        const auto rawBytes = writeAnonymousNbt(*raw);
        output.raw(rawBytes.data(), rawBytes.size());
        // RawFilteredPair's optional filtered text is anonOptionalNbt: an
        // absent value is the NBT End tag itself, not a boolean prefix.
        return writeOptionalAnonymousNbt(filtered, output);
    } catch (const std::exception&) {
        return false;
    }
}

inline std::optional<nbt::Value> readFilteredTextPair(ReadBuffer& input) {
    auto raw = readAnonymousNbt(input);
    if (!raw) return std::nullopt;
    nbt::Value pair = nbt::Value::makeCompound();
    pair.set("raw", std::move(*raw));
    std::optional<nbt::Value> filtered;
    if (!readOptionalAnonymousNbt(input, filtered)) return std::nullopt;
    if (filtered) pair.set("filtered", std::move(*filtered));
    return pair;
}

inline bool writeBookContent(const nbt::Value& saved, std::uint32_t id,
                             WriteBuffer& output) {
    if (saved.tag != nbt::Compound || (id != 43 && id != 44)) return false;
    const auto* pages = saved.get("pages");
    if (pages && (pages->tag != nbt::List ||
                  pages->list.size() > kMaxBookPages))
        return false;

    if (id == 44) {
        const auto* title = saved.get("title");
        const auto* author = saved.get("author");
        if (!title || !author || author->tag != nbt::String ||
            !withinStringLength(author->str,
                                kMaxPacketStringUtf16Units) ||
            !writeFilteredStringPair(*title,
                                     kMaxBookTitleUtf16Units, output))
            return false;
        const auto generation = integer(saved.get("generation"));
        if (saved.get("generation") && !generation) return false;
        const auto generationValue = generation.value_or(0);
        if (generationValue < 0 || generationValue > 3) return false;
        if (!writeProtocolString(output, author->str,
                                 kMaxPacketStringUtf16Units))
            return false;
        output.varint(generationValue);
    }

    const auto pageCount = pages ? pages->list.size() : 0;
    output.varint(static_cast<std::int32_t>(pageCount));
    if (pages) {
        for (const auto& page : pages->list) {
            if (page.tag != nbt::Compound) return false;
            const auto* raw = page.get("raw");
            if (!raw) return false;
            if (id == 43) {
                if (!writeFilteredStringPair(
                        page, kMaxWritableBookPageUtf16Units, output))
                    return false;
            } else {
                if (!writeFilteredTextPair(page, output)) return false;
            }
        }
    }

    if (id == 44) {
        bool resolved = false;
        if (!boolean(saved.get("resolved"), false, resolved)) return false;
        output.boolean(resolved);
    }
    return true;
}

inline std::optional<nbt::Value> readBookContent(
    const std::vector<std::uint8_t>& payload, std::uint32_t id) {
    if (id != 43 && id != 44) return std::nullopt;
    try {
        ReadBuffer input(payload);
        nbt::Value book = nbt::Value::makeCompound();
        if (id == 44) {
            auto title = readFilteredStringPair(
                input, kMaxBookTitleUtf16Units);
            if (!title) return std::nullopt;
            book.set("title", std::move(*title));
            auto author = readProtocolString(
                input, kMaxPacketStringUtf16Units);
            if (!author) return std::nullopt;
            book.set("author", nbt::Value::makeString(std::move(*author)));
            const auto generation = input.varint();
            if (generation < 0 || generation > 3) return std::nullopt;
            book.set("generation", nbt::Value::makeInt(generation));
        }
        const auto count = input.varint();
        if (count < 0 || static_cast<std::size_t>(count) > kMaxBookPages)
            return std::nullopt;
        nbt::Value pages = nbt::Value::makeList(nbt::Compound);
        pages.list.reserve(static_cast<std::size_t>(count));
        for (std::int32_t pageIndex = 0; pageIndex < count; ++pageIndex) {
            auto page = id == 43
                ? readFilteredStringPair(
                      input, kMaxWritableBookPageUtf16Units)
                : readFilteredTextPair(input);
            if (!page) return std::nullopt;
            pages.list.push_back(std::move(*page));
        }
        book.set("pages", std::move(pages));
        if (id == 44) book.set("resolved",
                               nbt::Value::makeByte(input.boolean() ? 1 : 0));
        if (input.remaining() != 0) return std::nullopt;
        return book;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline bool validContainerLoot(const nbt::Value& saved) {
    if (saved.tag != nbt::Compound) return false;
    const auto* lootTable = saved.get("loot_table");
    const auto* seed = saved.get("seed");
    return lootTable && lootTable->tag == nbt::String &&
           isResourceIdentifier(lootTable->str) &&
           (!seed || seed->tag == nbt::Long);
}

inline std::optional<std::string_view> rarityName(std::int32_t id);

inline bool consumeProtocolComponentPayload(ReadBuffer& input,
                                             std::uint32_t id,
                                             std::size_t depth = 0) {
    if (depth > kMaxComponentDepth) return false;
    try {
        const auto readCount = [&]() -> std::optional<std::int32_t> {
            const auto count = input.varint();
            if (count < 0 ||
                static_cast<std::size_t>(count) > kMaxCustomEffects)
                return std::nullopt;
            return count;
        };
        const auto readNbt = [&]() {
            return readAnonymousNbt(input).has_value();
        };
        const auto readOptionalNbt = [&]() {
            std::optional<nbt::Value> value;
            return readOptionalAnonymousNbt(input, value);
        };

        switch (id) {
        case 0: {
            const auto value = readAnonymousNbt(input);
            return value && value->tag == nbt::Compound;
        }
        case 1:
        case 2:
        case 3:
        case 17:
        case 27:
        case 36: {
            const auto value = input.varint();
            return value >= 0 && (id != 1 || value > 0);
        }
        case 4:
        case 19:
            (void)input.boolean();
            return true;
        case 20: // Unit: has no bytes on the protocol wire.
            return true;
        case 5:
        case 6:
        case 37:
        case 46:
        case 47:
        case 48:
        case 49:
        case 53:
        case 65:
            return readNbt();
        case 43: {
            const auto count = input.varint();
            if (count < 0 || static_cast<std::size_t>(count) > kMaxBookPages)
                return false;
            for (std::int32_t i = 0; i < count; ++i)
                if (!readFilteredStringPair(
                        input, kMaxWritableBookPageUtf16Units))
                    return false;
            return true;
        }
        case 44: {
            if (!readFilteredStringPair(
                    input, kMaxBookTitleUtf16Units) ||
                !readProtocolString(input, kMaxPacketStringUtf16Units))
                return false;
            const auto generation = input.varint();
            if (generation < 0 || generation > 3) return false;
            const auto count = input.varint();
            if (count < 0 || static_cast<std::size_t>(count) > kMaxBookPages)
                return false;
            for (std::int32_t i = 0; i < count; ++i)
                if (!readFilteredTextPair(input)) return false;
            (void)input.boolean();
            return true;
        }
        case 66: {
            const auto component = readAnonymousNbt(input);
            return component && validContainerLoot(*component);
        }
        case 7:
        case 25:
        case 31:
        case 58:
            (void)input.string(32767);
            return true;
        case 8: {
            const auto count = readCount();
            if (!count) return false;
            for (std::int32_t i = 0; i < *count; ++i)
                if (!readOptionalNbt()) return false;
            return true;
        }
        case 9: {
            const auto rarity = rarityName(input.varint());
            return rarity.has_value();
        }
        case 10:
        case 33: {
            const auto count = readCount();
            if (!count ||
                static_cast<std::size_t>(*count) > kEnchantmentNames.size())
                return false;
            std::array<bool, kEnchantmentNames.size()> seen{};
            for (std::int32_t i = 0; i < *count; ++i) {
                const auto enchantmentId = input.varint();
                const auto enchantment = enchantmentName(enchantmentId);
                const auto level = input.varint();
                if (!enchantment || level < 0 ||
                    seen[static_cast<std::size_t>(enchantmentId)])
                    return false;
                seen[static_cast<std::size_t>(enchantmentId)] = true;
            }
            (void)input.boolean();
            return true;
        }
        case 11:
        case 12: {
            const auto predicateCount = readCount();
            if (!predicateCount) return false;
            for (std::int32_t predicate = 0;
                 predicate < *predicateCount; ++predicate) {
                if (input.boolean()) {
                    const auto holderSet = input.varint();
                    if (holderSet < 0) return false;
                    if (holderSet == 0) {
                        (void)input.string(32767); // registry tag identifier
                    } else {
                        const auto blockCount = holderSet - 1;
                        if (blockCount > static_cast<std::int32_t>(
                                             kMaxCustomEffects))
                            return false;
                        for (std::int32_t block = 0; block < blockCount; ++block)
                            if (input.varint() < 0) return false;
                    }
                }
                if (input.boolean()) {
                    const auto propertyCount = readCount();
                    if (!propertyCount) return false;
                    for (std::int32_t property = 0;
                         property < *propertyCount; ++property) {
                        (void)input.string(32767);
                        if (input.boolean()) {
                            (void)input.string(32767);
                        } else {
                            (void)input.string(32767);
                            (void)input.string(32767);
                        }
                    }
                }
                if (!readOptionalNbt()) return false;
            }
            (void)input.boolean(); // showTooltip
            return true;
        }
        case 14: {
            const auto readFloats = [&]() {
                const auto count = readCount();
                if (!count) return false;
                for (std::int32_t i = 0; i < *count; ++i)
                    (void)input.f32();
                return true;
            };
            const auto readFlags = [&]() {
                const auto count = readCount();
                if (!count) return false;
                for (std::int32_t i = 0; i < *count; ++i)
                    (void)input.boolean();
                return true;
            };
            const auto readStrings = [&]() {
                const auto count = readCount();
                if (!count) return false;
                for (std::int32_t i = 0; i < *count; ++i)
                    (void)input.string(32767);
                return true;
            };
            const auto readColors = [&]() {
                const auto count = readCount();
                if (!count) return false;
                for (std::int32_t i = 0; i < *count; ++i)
                    (void)input.i32();
                return true;
            };
            return readFloats() && readFlags() && readStrings() &&
                   readColors();
        }
        case 15:
        case 16:
        case 18:
        case 30:
            return true;
        case 38:
        case 51:
        case 60:
            return input.varint() >= 0;
        case 34:
            (void)input.i32();
            (void)input.boolean();
            return true;
        case 35:
            (void)input.i32();
            return true;
        case 41: {
            if (input.boolean() && !potionName(input.varint())) return false;
            if (input.boolean()) (void)input.i32();
            const auto count = readCount();
            if (!count) return false;
            for (std::int32_t i = 0; i < *count; ++i) {
                const auto effectId = input.varint();
                if (effectId <= 0 || effectId > effects::Infested)
                    return false;
                nbt::Value effect = nbt::Value::makeCompound();
                if (!decodeEffectDetail(input, effect)) return false;
            }
            if (input.boolean()) (void)input.string(32767);
            return true;
        }
        case 45: {
            const auto material = trimMaterialName(input.varint());
            const auto pattern = trimPatternName(input.varint());
            if (!material || !pattern) return false;
            (void)input.boolean();
            return true;
        }
        case 62: {
            const auto count = input.varint();
            if (count < 0 ||
                static_cast<std::size_t>(count) > kMaxContainerSlots)
                return false;
            for (std::int32_t slot = 0; slot < count; ++slot) {
                const auto stackCount = input.varint();
                if (stackCount < 0 || stackCount > 32767) return false;
                if (stackCount == 0) continue;
                if (!isKnownItemId(input.varint())) return false;
                const auto added = input.varint();
                const auto removed = input.varint();
                if (added < 0 || removed < 0 ||
                    static_cast<std::size_t>(added) >
                        kMaxNestedItemComponents ||
                    static_cast<std::size_t>(removed) >
                        kMaxNestedItemComponents)
                    return false;
                std::array<bool, 67> seen{};
                for (std::int32_t component = 0; component < added;
                     ++component) {
                    const auto nestedId = input.varint();
                    if (nestedId < 0 || nestedId >=
                            static_cast<std::int32_t>(seen.size()) ||
                        seen[static_cast<std::size_t>(nestedId)] ||
                        !consumeProtocolComponentPayload(
                            input, static_cast<std::uint32_t>(nestedId),
                            depth + 1))
                        return false;
                    seen[static_cast<std::size_t>(nestedId)] = true;
                }
                for (std::int32_t component = 0; component < removed;
                     ++component) {
                    const auto nestedId = input.varint();
                    if (nestedId < 0 || nestedId >=
                            static_cast<std::int32_t>(seen.size()) ||
                        seen[static_cast<std::size_t>(nestedId)])
                        return false;
                    seen[static_cast<std::size_t>(nestedId)] = true;
                }
            }
            return true;
        }
        default:
            return false;
        }
    } catch (const std::exception&) {
        return false;
    }
}

inline std::optional<std::vector<std::uint8_t>> readProtocolComponentPayload(
    ReadBuffer& input, std::uint32_t id) {
    const auto start = input.off;
    if (!consumeProtocolComponentPayload(input, id)) return std::nullopt;
    const auto length = input.off - start;
    if (length > kMaxProtocolComponentPayloadBytes) return std::nullopt;
    const auto* begin = input.p + start;
    return std::vector<std::uint8_t>(begin, begin + length);
}

inline bool isValidProtocolComponentPayload(
    std::uint32_t id, const std::vector<std::uint8_t>& payload) {
    ReadBuffer input(payload);
    return consumeProtocolComponentPayload(input, id) &&
           input.remaining() == 0;
}

inline std::optional<std::int32_t> rarityId(std::string_view rarity) {
    if (rarity == "common") return 0;
    if (rarity == "uncommon") return 1;
    if (rarity == "rare") return 2;
    if (rarity == "epic") return 3;
    return std::nullopt;
}

inline std::optional<std::string_view> rarityName(std::int32_t id) {
    constexpr std::array<std::string_view, 4> names{
        "common", "uncommon", "rare", "epic"};
    if (id < 0 || static_cast<std::size_t>(id) >= names.size())
        return std::nullopt;
    return names[static_cast<std::size_t>(id)];
}

inline bool encodeListField(const nbt::Value& compound,
                            std::string_view fieldName, nbt::Tag elementTag,
                            WriteBuffer& output,
                            const std::function<void(const nbt::Value&)>& encode) {
    const auto* field = compound.get(fieldName);
    if (!field) {
        output.varint(0);
        return true;
    }
    if (field->tag != nbt::List || field->list.size() > kMaxCustomEffects)
        return false;
    output.varint(static_cast<std::int32_t>(field->list.size()));
    for (const auto& value : field->list) {
        if (value.tag != elementTag) return false;
        encode(value);
    }
    return true;
}

inline std::optional<std::vector<std::uint8_t>> customModelDataToProtocol(
    const nbt::Value& saved) {
    if (saved.tag != nbt::Compound) return std::nullopt;
    WriteBuffer output;
    if (!encodeListField(saved, "floats", nbt::Float, output,
                         [&](const nbt::Value& v) { output.f32(v.f); }) ||
        !encodeListField(saved, "flags", nbt::Byte, output,
                         [&](const nbt::Value& v) {
                             if (v.b != 0 && v.b != 1)
                                 throw std::invalid_argument("invalid custom model flag");
                             output.boolean(v.b != 0);
                         }) ||
        !encodeListField(saved, "strings", nbt::String, output,
                         [&](const nbt::Value& v) { output.string(v.str); }) ||
        !encodeListField(saved, "colors", nbt::Int, output,
                         [&](const nbt::Value& v) { output.i32(v.i); }))
        return std::nullopt;
    return std::move(output.data);
}

inline std::optional<std::vector<std::uint8_t>> blockPredicatesToProtocol(
    const nbt::Value& saved) {
    if (saved.tag != nbt::Compound) return std::nullopt;
    const auto* predicates = saved.get("predicates");
    if (!predicates || predicates->tag != nbt::List ||
        predicates->list.size() > kMaxCustomEffects)
        return std::nullopt;

    WriteBuffer output;
    output.varint(static_cast<std::int32_t>(predicates->list.size()));
    for (const auto& predicate : predicates->list) {
        if (predicate.tag != nbt::Compound) return std::nullopt;
        const auto* blocks = predicate.get("blocks");
        output.boolean(blocks != nullptr);
        if (blocks) {
            const auto writeBlockSet = [&](std::string_view blockName) {
                if (!blockName.empty() && blockName.front() == '#') {
                    output.varint(0); // named registry tag
                    auto tagName = blockName.substr(1);
                    if (tagName.find(':') == std::string_view::npos)
                        output.string("minecraft:" + std::string(tagName));
                    else
                        output.string(tagName);
                    return true;
                }
                std::string qualifiedName;
                if (blockName.find(':') == std::string_view::npos) {
                    qualifiedName = "minecraft:";
                    qualifiedName.append(blockName);
                    blockName = qualifiedName;
                }
                const auto id = gen::blockRegistryId(blockName);
                if (!id) return false;
                output.varint(2); // one inline registry entry: count + 1
                output.varint(static_cast<std::int32_t>(*id));
                return true;
            };
            if (blocks->tag == nbt::String) {
                if (!writeBlockSet(blocks->str)) return std::nullopt;
            } else if (blocks->tag == nbt::List &&
                       blocks->list.size() <= kMaxCustomEffects) {
                if (blocks->list.size() == 1 &&
                    blocks->list.front().tag == nbt::String &&
                    !blocks->list.front().str.empty() &&
                    blocks->list.front().str.front() == '#') {
                    if (!writeBlockSet(blocks->list.front().str))
                        return std::nullopt;
                } else {
                    output.varint(static_cast<std::int32_t>(
                        blocks->list.size() + 1));
                    for (const auto& blockName : blocks->list) {
                        if (blockName.tag != nbt::String ||
                            (!blockName.str.empty() &&
                             blockName.str.front() == '#'))
                            return std::nullopt;
                        std::string qualifiedName;
                        std::string_view name = blockName.str;
                        if (name.find(':') == std::string_view::npos) {
                            qualifiedName = "minecraft:";
                            qualifiedName.append(name);
                            name = qualifiedName;
                        }
                        const auto id = gen::blockRegistryId(name);
                        if (!id) return std::nullopt;
                        output.varint(static_cast<std::int32_t>(
                            *id));
                    }
                }
            } else {
                return std::nullopt;
            }
        }

        const auto* state = predicate.get("state");
        output.boolean(state != nullptr);
        if (state) {
            if (state->tag != nbt::Compound ||
                state->comp.size() > kMaxCustomEffects)
                return std::nullopt;
            output.varint(static_cast<std::int32_t>(state->comp.size()));
            for (const auto& [name, propertyValue] : state->comp) {
                if (propertyValue.tag != nbt::String) return std::nullopt;
                output.string(name);
                output.boolean(true); // exact property match
                output.string(propertyValue.str);
            }
        }

        const auto* blockNbt = predicate.get("nbt");
        output.boolean(blockNbt != nullptr);
        if (blockNbt) {
            if (blockNbt->tag != nbt::Compound) return std::nullopt;
            try {
                const auto encoded = writeAnonymousNbt(*blockNbt);
                output.raw(encoded.data(), encoded.size());
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }
    }

    bool showTooltip = true;
    if (!boolean(saved.get("show_in_tooltip"), true, showTooltip))
        return std::nullopt;
    output.boolean(showTooltip);
    return std::move(output.data);
}

inline std::optional<nbt::Value> blockPredicatesFromProtocol(
    const std::vector<std::uint8_t>& payload) {
    try {
        ReadBuffer input(payload);
        const auto count = input.varint();
        if (count < 0 || static_cast<std::size_t>(count) > kMaxCustomEffects)
            return std::nullopt;
        nbt::Value result = nbt::Value::makeCompound();
        nbt::Value predicates = nbt::Value::makeList(nbt::Compound);
        predicates.list.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index = 0; index < count; ++index) {
            nbt::Value predicate = nbt::Value::makeCompound();
            if (input.boolean()) {
                const auto holderSet = input.varint();
                if (holderSet < 0) return std::nullopt;
                if (holderSet == 0) {
                    predicate.set("blocks", nbt::Value::makeString(
                        "#" + input.string(32767)));
                } else {
                    const auto blockCount = holderSet - 1;
                    if (blockCount > static_cast<std::int32_t>(
                                         kMaxCustomEffects))
                        return std::nullopt;
                    nbt::Value blocks = nbt::Value::makeList(nbt::String);
                    blocks.list.reserve(static_cast<std::size_t>(blockCount));
                    for (std::int32_t block = 0; block < blockCount; ++block) {
                        const auto registryId = input.varint();
                        if (registryId < 0) return std::nullopt;
                        const auto name = gen::blockNameByRegistryId(
                            static_cast<std::uint32_t>(registryId));
                        if (name.empty()) return std::nullopt;
                        blocks.list.push_back(nbt::Value::makeString(
                            std::string(name)));
                    }
                    predicate.set("blocks", std::move(blocks));
                }
            }
            if (input.boolean()) {
                const auto propertyCount = input.varint();
                if (propertyCount < 0 ||
                    static_cast<std::size_t>(propertyCount) >
                        kMaxCustomEffects)
                    return std::nullopt;
                nbt::Value state = nbt::Value::makeCompound();
                for (std::int32_t property = 0;
                     property < propertyCount; ++property) {
                    auto name = input.string(32767);
                    const bool exact = input.boolean();
                    if (!exact) {
                        auto minValue = input.string(32767);
                        auto maxValue = input.string(32767);
                        nbt::Value range = nbt::Value::makeCompound();
                        range.set("min", nbt::Value::makeString(
                            std::move(minValue)));
                        range.set("max", nbt::Value::makeString(
                            std::move(maxValue)));
                        state.set(std::move(name), std::move(range));
                    } else {
                        state.set(std::move(name),
                            nbt::Value::makeString(input.string(32767)));
                    }
                }
                predicate.set("state", std::move(state));
            }
            if (input.boolean()) {
                auto blockNbt = readAnonymousNbt(input);
                if (!blockNbt || blockNbt->tag != nbt::Compound)
                    return std::nullopt;
                predicate.set("nbt", std::move(*blockNbt));
            }
            predicates.list.push_back(std::move(predicate));
        }
        result.set("predicates", std::move(predicates));
        result.set("show_in_tooltip", nbt::Value::makeByte(
            input.boolean() ? 1 : 0));
        if (input.remaining() != 0) return std::nullopt;
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline std::optional<ProtocolComponent> protocolComponentFromNbt(
    std::string_view name, const nbt::Value& saved,
    std::size_t depth = 0) {
    const auto id = componentId(name);
    if (!id) return std::nullopt;
    WriteBuffer output;

    if (*id == 62) {
        if (depth >= kMaxComponentDepth) return std::nullopt;
        auto payload = containerComponentNbtToProtocol(saved, depth + 1);
        if (!payload) return std::nullopt;
        return ProtocolComponent{*id, std::move(*payload)};
    }
    if (*id == 11 || *id == 12) {
        auto payload = blockPredicatesToProtocol(saved);
        if (!payload) return std::nullopt;
        return ProtocolComponent{*id, std::move(*payload)};
    }
    if (*id == 43 || *id == 44) {
        if (!writeBookContent(saved, *id, output)) return std::nullopt;
        return ProtocolComponent{*id, std::move(output.data)};
    }

    if (*id == 0 || *id == 5 || *id == 6) {
        if (*id == 0 && saved.tag != nbt::Compound) return std::nullopt;
        try {
            return ProtocolComponent{*id, writeAnonymousNbt(saved)};
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    if (*id == 20) {
        if (saved.tag != nbt::Compound || !saved.comp.empty())
            return std::nullopt;
        return ProtocolComponent{*id, {}};
    }
    if (*id == 37 || *id == 46 || *id == 47 ||
        *id == 48 || *id == 49 || *id == 53 || *id == 65 ||
        *id == 66) {
        if (*id == 66 && !validContainerLoot(saved))
            return std::nullopt;
        try {
            return ProtocolComponent{*id, writeAnonymousNbt(saved)};
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    if (*id == 7 || *id == 25 || *id == 31 || *id == 58) {
        if (saved.tag != nbt::String) return std::nullopt;
        output.string(saved.str);
        return ProtocolComponent{*id, std::move(output.data)};
    }
    if (*id == 1 || *id == 2 || *id == 3 || *id == 17 ||
        *id == 27 || *id == 36 || *id == 38 || *id == 51 || *id == 60) {
        const auto value = integer(&saved);
        if (!value || *value < 0 || (*id == 1 && *value == 0))
            return std::nullopt;
        output.varint(*value);
        return ProtocolComponent{*id, std::move(output.data)};
    }
    if (*id == 35) {
        if (saved.tag != nbt::Int) return std::nullopt;
        output.i32(saved.i);
        return ProtocolComponent{*id, std::move(output.data)};
    }
    if (*id == 4) {
        if (saved.tag != nbt::Compound) return std::nullopt;
        bool showTooltip = true;
        if (!boolean(saved.get("show_in_tooltip"), true, showTooltip))
            return std::nullopt;
        output.boolean(showTooltip);
        return ProtocolComponent{*id, std::move(output.data)};
    }
    if (*id == 19) {
        bool glint = false;
        if (!boolean(&saved, false, glint)) return std::nullopt;
        output.boolean(glint);
        return ProtocolComponent{*id, std::move(output.data)};
    }
    if (*id == 8) {
        if (saved.tag != nbt::List ||
            saved.list.size() > kMaxCustomEffects)
            return std::nullopt;
        output.varint(static_cast<std::int32_t>(saved.list.size()));
        try {
            for (const auto& line : saved.list) {
                const auto encoded = writeAnonymousNbt(line);
                output.raw(encoded.data(), encoded.size());
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
        return ProtocolComponent{*id, std::move(output.data)};
    }
    if (*id == 9) {
        if (saved.tag != nbt::String) return std::nullopt;
        const auto rarity = rarityId(saved.str);
        if (!rarity) return std::nullopt;
        output.varint(*rarity);
        return ProtocolComponent{*id, std::move(output.data)};
    }
    if (*id == 14) {
        std::optional<std::vector<std::uint8_t>> encoded;
        try {
            encoded = customModelDataToProtocol(saved);
        } catch (const std::exception&) {
            return std::nullopt;
        }
        if (!encoded) return std::nullopt;
        return ProtocolComponent{*id, std::move(*encoded)};
    }
    if (*id == 15 || *id == 16 || *id == 18 || *id == 30) {
        if (saved.tag != nbt::Compound || !saved.comp.empty())
            return std::nullopt;
        return ProtocolComponent{*id, {}};
    }
    if (*id == 34) {
        if (saved.tag != nbt::Compound) return std::nullopt;
        const auto* rgb = saved.get("rgb");
        if (!rgb || rgb->tag != nbt::Int) return std::nullopt;
        bool showTooltip = true;
        if (!boolean(saved.get("show_in_tooltip"), true, showTooltip))
            return std::nullopt;
        output.i32(rgb->i);
        output.boolean(showTooltip);
        return ProtocolComponent{*id, std::move(output.data)};
    }
    if (*id == 41) {
        nbt::Value wrapper = nbt::Value::makeCompound();
        wrapper.set(std::string(kPotionContents), saved);
        auto encoded = potionContentsToProtocol(&wrapper);
        if (!encoded) return std::nullopt;
        return ProtocolComponent{*id, std::move(*encoded)};
    }
    if (*id == 10 || *id == 33) {
        if (saved.tag != nbt::Compound) return std::nullopt;
        const auto* levels = saved.get("levels");
        if (!levels || levels->tag != nbt::Compound ||
            levels->comp.size() > kMaxCustomEffects)
            return std::nullopt;
        std::vector<std::pair<std::int32_t, std::int32_t>> entries;
        entries.reserve(levels->comp.size());
        for (const auto& [name, value] : levels->comp) {
            const auto enchantment = enchantmentId(name);
            const auto level = integer(&value);
            if (!enchantment || !level || *level < 0) return std::nullopt;
            entries.emplace_back(*enchantment, *level);
        }
        bool showTooltip = true;
        if (!boolean(saved.get("show_in_tooltip"), true, showTooltip))
            return std::nullopt;
        output.varint(static_cast<std::int32_t>(entries.size()));
        for (const auto& [enchantment, level] : entries) {
            output.varint(enchantment);
            output.varint(level);
        }
        output.boolean(showTooltip);
        return ProtocolComponent{*id, std::move(output.data)};
    }
    if (*id == 45) {
        if (saved.tag != nbt::Compound) return std::nullopt;
        const auto* pattern = saved.get("pattern");
        const auto* material = saved.get("material");
        if (!pattern || pattern->tag != nbt::String || !material ||
            material->tag != nbt::String)
            return std::nullopt;
        const auto patternId = trimPatternId(pattern->str);
        const auto materialId = trimMaterialId(material->str);
        if (!patternId || !materialId) return std::nullopt;
        bool showTooltip = true;
        if (!boolean(saved.get("show_in_tooltip"), true, showTooltip))
            return std::nullopt;
        output.varint(*materialId);
        output.varint(*patternId);
        output.boolean(showTooltip);
        return ProtocolComponent{*id, std::move(output.data)};
    }
    return std::nullopt;
}

inline bool applyProtocolComponentToNbt(
    nbt::Value& item, std::uint32_t id,
    const std::vector<std::uint8_t>* payload, std::size_t depth = 0) {
    const auto name = componentName(id);
    if (!name) return false;
    if (!payload) {
        if (auto* components = item.get("components")) erase(*components, *name);
        return true;
    }

    nbt::Value value;
    try {
        if (id == 62) {
            if (depth >= kMaxComponentDepth) return false;
            auto decoded = containerComponentProtocolToNbt(*payload,
                                                            depth + 1);
            if (!decoded) return false;
            value = std::move(*decoded);
        } else if (id == 11 || id == 12) {
            auto decoded = blockPredicatesFromProtocol(*payload);
            if (!decoded) return false;
            value = std::move(*decoded);
        } else if (id == 43 || id == 44) {
            auto decoded = readBookContent(*payload, id);
            if (!decoded) return false;
            value = std::move(*decoded);
        } else if (id == 20) {
            if (!payload->empty()) return false;
            value = nbt::Value::makeCompound();
        } else if (id == 0 || id == 5 || id == 6) {
            auto decoded = readAnonymousNbt(*payload);
            if (!decoded || (id == 0 && decoded->tag != nbt::Compound))
                return false;
            value = std::move(*decoded);
        } else if (id == 37 || id == 46 || id == 47 ||
                   id == 48 || id == 49 || id == 53 || id == 65 ||
                   id == 66) {
            auto decoded = readAnonymousNbt(*payload);
            if (!decoded) return false;
            if (id == 66 && !validContainerLoot(*decoded)) return false;
            value = std::move(*decoded);
        } else if (id == 7 || id == 25 || id == 31 || id == 58) {
            ReadBuffer input(*payload);
            value = nbt::Value::makeString(input.string(32767));
            if (input.remaining() != 0) return false;
        } else if (id == 1 || id == 2 || id == 3 || id == 17 ||
                   id == 27 || id == 36 || id == 38 || id == 51 || id == 60) {
            ReadBuffer input(*payload);
            const auto integerValue = input.varint();
            if (integerValue < 0 || (id == 1 && integerValue == 0) ||
                input.remaining() != 0)
                return false;
            value = nbt::Value::makeInt(integerValue);
        } else if (id == 35) {
            ReadBuffer input(*payload);
            value = nbt::Value::makeInt(input.i32());
            if (input.remaining() != 0) return false;
        } else if (id == 4) {
            ReadBuffer input(*payload);
            const bool showTooltip = input.boolean();
            if (input.remaining() != 0) return false;
            value = nbt::Value::makeCompound();
            value.set("show_in_tooltip",
                      nbt::Value::makeByte(showTooltip ? 1 : 0));
        } else if (id == 19) {
            ReadBuffer input(*payload);
            value = nbt::Value::makeByte(input.boolean() ? 1 : 0);
            if (input.remaining() != 0) return false;
        } else if (id == 8) {
            ReadBuffer input(*payload);
            const auto count = input.varint();
            if (count < 0 ||
                static_cast<std::size_t>(count) > kMaxCustomEffects)
                return false;
            nbt::Tag elementTag = nbt::End;
            nbt::Value lore = nbt::Value::makeList(elementTag);
            lore.list.reserve(static_cast<std::size_t>(count));
            for (std::int32_t i = 0; i < count; ++i) {
                auto line = readAnonymousNbt(input);
                if (!line) return false;
                if (elementTag == nbt::End) {
                    elementTag = line->tag;
                    lore.listElement = elementTag;
                } else if (line->tag != elementTag) {
                    return false;
                }
                lore.list.push_back(std::move(*line));
            }
            if (input.remaining() != 0) return false;
            value = std::move(lore);
        } else if (id == 9) {
            ReadBuffer input(*payload);
            const auto rarity = rarityName(input.varint());
            if (!rarity || input.remaining() != 0) return false;
            value = nbt::Value::makeString(std::string(*rarity));
        } else if (id == 14) {
            ReadBuffer input(*payload);
            nbt::Value customModelData = nbt::Value::makeCompound();
            const auto readFloats = [&](const char* key, nbt::Tag tag,
                                        auto&& readValue) {
                const auto count = input.varint();
                if (count < 0 ||
                    static_cast<std::size_t>(count) > kMaxCustomEffects)
                    return false;
                nbt::Value values = nbt::Value::makeList(tag);
                values.list.reserve(static_cast<std::size_t>(count));
                for (std::int32_t i = 0; i < count; ++i)
                    values.list.push_back(readValue());
                customModelData.set(key, std::move(values));
                return true;
            };
            if (!readFloats("floats", nbt::Float,
                            [&] { return nbt::Value::makeFloat(input.f32()); }) ||
                !readFloats("flags", nbt::Byte,
                            [&] { return nbt::Value::makeByte(
                                      input.boolean() ? 1 : 0); }) ||
                !readFloats("strings", nbt::String,
                            [&] { return nbt::Value::makeString(
                                      input.string(32767)); }) ||
                !readFloats("colors", nbt::Int,
                            [&] { return nbt::Value::makeInt(input.i32()); }) ||
                input.remaining() != 0)
                return false;
            value = std::move(customModelData);
        } else if (id == 15 || id == 16 || id == 18 || id == 30) {
            if (!payload->empty()) return false;
            value = nbt::Value::makeCompound();
        } else if (id == 34) {
            ReadBuffer input(*payload);
            const auto rgb = input.i32();
            const bool showTooltip = input.boolean();
            if (input.remaining() != 0) return false;
            value = nbt::Value::makeCompound();
            value.set("rgb", nbt::Value::makeInt(rgb));
            value.set("show_in_tooltip",
                      nbt::Value::makeByte(showTooltip ? 1 : 0));
        } else if (id == 41) {
            if (!applyPotionContentsFromProtocol(item, *payload)) return false;
            return true;
        } else if (id == 10 || id == 33) {
            ReadBuffer input(*payload);
            const auto count = input.varint();
            if (count < 0 || static_cast<std::size_t>(count) > kMaxCustomEffects)
                return false;
            nbt::Value levels = nbt::Value::makeCompound();
            for (std::int32_t i = 0; i < count; ++i) {
                const auto enchantmentId = input.varint();
                const auto level = input.varint();
                const auto enchantment = enchantmentName(enchantmentId);
                if (!enchantment || level < 0) return false;
                levels.set(std::string(*enchantment),
                           nbt::Value::makeInt(level));
            }
            const bool showTooltip = input.boolean();
            if (input.remaining() != 0) return false;
            const auto* existingComponents = item.get("components");
            const auto* existing = existingComponents
                ? existingComponents->get(*name) : nullptr;
            value = existing && existing->tag == nbt::Compound
                ? *existing : nbt::Value::makeCompound();
            value.set("levels", std::move(levels));
            value.set("show_in_tooltip",
                      nbt::Value::makeByte(showTooltip ? 1 : 0));
        } else if (id == 45) {
            ReadBuffer input(*payload);
            const auto materialId = input.varint();
            const auto patternId = input.varint();
            const auto pattern = trimPatternName(patternId);
            const auto material = trimMaterialName(materialId);
            if (!pattern || !material) return false;
            const bool showTooltip = input.boolean();
            if (input.remaining() != 0) return false;
            const auto* existingComponents = item.get("components");
            const auto* existing = existingComponents
                ? existingComponents->get(*name) : nullptr;
            value = existing && existing->tag == nbt::Compound
                ? *existing : nbt::Value::makeCompound();
            value.set("pattern", nbt::Value::makeString(std::string(*pattern)));
            value.set("material", nbt::Value::makeString(std::string(*material)));
            value.set("show_in_tooltip",
                      nbt::Value::makeByte(showTooltip ? 1 : 0));
        } else {
            return false;
        }
    } catch (const std::exception&) {
        return false;
    }

    nbt::Value* components = item.get("components");
    if (!components || components->tag != nbt::Compound) {
        item.set("components", nbt::Value::makeCompound());
        components = item.get("components");
    }
    components->set(std::string(*name), std::move(value));
    return true;
}

} // namespace cppfm::item_component_nbt
