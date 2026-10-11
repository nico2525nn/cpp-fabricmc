#pragma once

// Additional, deliberately bounded codecs for 1.21.4 protocol-769 item
// components. Include this after Items.hpp has defined ItemStack and after
// ItemComponentNbt.hpp has declared its shared NBT/string helpers.

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppfm::item_component_nbt::additional_other {

// These are implementation safety limits, not claims about vanilla's codec
// limits (except where an explicit vanilla cap is noted below).
inline constexpr std::size_t kMaxPayloadBytes = 1024u * 1024u;
inline constexpr std::size_t kMaxEntries = 1024;
inline constexpr std::size_t kMaxNbtNodes = 4096;
inline constexpr std::size_t kMaxNbtDepth = 32;
inline constexpr std::size_t kMaxFireworkColors = 4096;
inline constexpr std::size_t kMaxProfileProperties = 16; // vanilla profile packet codec
inline constexpr std::size_t kMaxFireworkExplosions = 256; // vanilla MAX_EXPLOSIONS
inline constexpr std::size_t kMaxBannerLayers = 1024;

namespace detail {

// Frozen vanilla 1.21.4 registry order, extracted from the exact packet-body
// fixtures loaded by EmbeddedData: assets/registry/registry_minecraft__*.bin.
// These tables intentionally do not claim to include modded registry entries.
inline constexpr std::array<std::string_view, 8> kInstrumentRegistryKeys{
    "minecraft:admire_goat_horn",
    "minecraft:call_goat_horn",
    "minecraft:dream_goat_horn",
    "minecraft:feel_goat_horn",
    "minecraft:ponder_goat_horn",
    "minecraft:seek_goat_horn",
    "minecraft:sing_goat_horn",
    "minecraft:yearn_goat_horn",
};

inline constexpr std::array<std::string_view, 19> kJukeboxSongRegistryKeys{
    "minecraft:11", "minecraft:13", "minecraft:5", "minecraft:blocks",
    "minecraft:cat", "minecraft:chirp", "minecraft:creator",
    "minecraft:creator_music_box", "minecraft:far", "minecraft:mall",
    "minecraft:mellohi", "minecraft:otherside", "minecraft:pigstep",
    "minecraft:precipice", "minecraft:relic", "minecraft:stal",
    "minecraft:strad", "minecraft:wait", "minecraft:ward",
};

inline constexpr std::array<std::string_view, 43> kBannerPatternRegistryKeys{
    "minecraft:base", "minecraft:border", "minecraft:bricks",
    "minecraft:circle", "minecraft:creeper", "minecraft:cross",
    "minecraft:curly_border", "minecraft:diagonal_left",
    "minecraft:diagonal_right", "minecraft:diagonal_up_left",
    "minecraft:diagonal_up_right", "minecraft:flow", "minecraft:flower",
    "minecraft:globe", "minecraft:gradient", "minecraft:gradient_up",
    "minecraft:guster", "minecraft:half_horizontal",
    "minecraft:half_horizontal_bottom", "minecraft:half_vertical",
    "minecraft:half_vertical_right", "minecraft:mojang", "minecraft:piglin",
    "minecraft:rhombus", "minecraft:skull", "minecraft:small_stripes",
    "minecraft:square_bottom_left", "minecraft:square_bottom_right",
    "minecraft:square_top_left", "minecraft:square_top_right",
    "minecraft:straight_cross", "minecraft:stripe_bottom",
    "minecraft:stripe_center", "minecraft:stripe_downleft",
    "minecraft:stripe_downright", "minecraft:stripe_left",
    "minecraft:stripe_middle", "minecraft:stripe_right",
    "minecraft:stripe_top", "minecraft:triangle_bottom",
    "minecraft:triangle_top", "minecraft:triangles_bottom",
    "minecraft:triangles_top",
};

// DyeColor.getId() wire order from the 1.21.4 enum constructor.
inline constexpr std::array<std::string_view, 16> kDyeColorNames{
    "white", "orange", "magenta", "light_blue", "yellow", "lime",
    "pink", "gray", "light_gray", "cyan", "purple", "blue", "brown",
    "green", "red", "black",
};

static_assert(kInstrumentRegistryKeys.size() == 8 &&
              kInstrumentRegistryKeys.front() ==
                  "minecraft:admire_goat_horn" &&
              kInstrumentRegistryKeys.back() == "minecraft:yearn_goat_horn");
static_assert(kJukeboxSongRegistryKeys.size() == 19 &&
              kJukeboxSongRegistryKeys.front() == "minecraft:11" &&
              kJukeboxSongRegistryKeys.back() == "minecraft:ward");
static_assert(kBannerPatternRegistryKeys.size() == 43 &&
              kBannerPatternRegistryKeys.front() == "minecraft:base" &&
              kBannerPatternRegistryKeys.back() ==
                  "minecraft:triangles_top");
static_assert(kDyeColorNames.size() == 16 && kDyeColorNames.front() == "white" &&
              kDyeColorNames.back() == "black");

template <std::size_t Size>
inline constexpr std::optional<std::int32_t> registryId(
    const std::array<std::string_view, Size>& keys,
    std::string_view key) noexcept {
    for (std::size_t index = 0; index < keys.size(); ++index)
        if (keys[index] == key)
            return static_cast<std::int32_t>(index);
    return std::nullopt;
}

template <std::size_t Size>
inline constexpr std::optional<std::string_view> registryKey(
    const std::array<std::string_view, Size>& keys,
    std::int32_t id) noexcept {
    if (id < 0 || static_cast<std::size_t>(id) >= keys.size())
        return std::nullopt;
    return keys[static_cast<std::size_t>(id)];
}

inline std::optional<std::string> canonicalVanillaKey(
    std::string_view key) {
    std::string candidate;
    if (key.find(':') == std::string_view::npos) {
        candidate = "minecraft:";
        candidate.append(key);
    } else {
        candidate.assign(key);
    }
    if (!isResourceIdentifier(candidate) ||
        !withinStringLength(candidate, kMaxPacketStringUtf16Units))
        return std::nullopt;
    return candidate;
}

template <std::size_t Size>
inline std::optional<std::int32_t> vanillaRegistryId(
    const std::array<std::string_view, Size>& keys,
    std::string_view key) {
    const auto canonical = canonicalVanillaKey(key);
    if (!canonical) return std::nullopt;
    return registryId(keys, *canonical);
}

inline constexpr std::optional<std::int32_t> dyeColorId(
    std::string_view name) noexcept {
    return registryId(kDyeColorNames, name);
}

inline constexpr std::optional<std::string_view> dyeColorName(
    std::int32_t id) noexcept {
    return registryKey(kDyeColorNames, id);
}

static_assert(registryId(kInstrumentRegistryKeys,
                         "minecraft:call_goat_horn") == 1);
static_assert(registryId(kJukeboxSongRegistryKeys,
                         "minecraft:creator_music_box") == 7);
static_assert(registryKey(kJukeboxSongRegistryKeys, 18) ==
              "minecraft:ward");
static_assert(registryId(kBannerPatternRegistryKeys,
                         "minecraft:half_horizontal_bottom") == 18);
static_assert(registryId(kBannerPatternRegistryKeys,
                         "minecraft:triangles_top") == 42);
static_assert(dyeColorId("light_blue") == 3 && dyeColorName(15) == "black");

inline bool isComponentDepthValid(std::size_t depth) noexcept {
    return depth <= kMaxComponentDepth;
}

inline bool isProtocolString(std::string_view value,
                             std::size_t maxUtf16Units =
                                 kMaxPacketStringUtf16Units) noexcept {
    return isResourceIdentifier(value) && withinStringLength(value, maxUtf16Units);
}

inline bool writeString(WriteBuffer& output, std::string_view value,
                        std::size_t maxUtf16Units =
                            kMaxPacketStringUtf16Units) {
    if (!withinStringLength(value, maxUtf16Units)) return false;
    output.string(value);
    return output.data.size() <= kMaxPayloadBytes;
}

inline std::optional<std::string> readString(
    ReadBuffer& input, std::size_t maxUtf16Units =
                           kMaxPacketStringUtf16Units) {
    try {
        const auto maxBytes = maximumUtf8Bytes(maxUtf16Units);
        auto result = input.string(maxBytes);
        if (!withinStringLength(result, maxUtf16Units)) return std::nullopt;
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline bool exactBoolean(const nbt::Value* value, bool fallback,
                         bool& result) noexcept {
    if (!value) {
        result = fallback;
        return true;
    }
    if (value->tag != nbt::Byte || (value->b != 0 && value->b != 1))
        return false;
    result = value->b != 0;
    return true;
}

inline std::optional<bool> readExactBoolean(ReadBuffer& input) {
    const auto value = input.u8();
    if (value > 1) return std::nullopt;
    return value != 0;
}

inline nbt::Value intListToNbt(const std::vector<std::int32_t>& values) {
    nbt::Value result = nbt::Value::makeList(nbt::Int, values.size());
    for (const auto value : values)
        result.list.push_back(nbt::Value::makeInt(value));
    return result;
}

inline std::optional<std::vector<std::int32_t>> readIntList(
    ReadBuffer& input, std::size_t maxCount) {
    try {
        const auto count = input.varint();
        if (count < 0 || static_cast<std::size_t>(count) > maxCount)
            return std::nullopt;
        std::vector<std::int32_t> values;
        values.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index = 0; index < count; ++index)
            values.push_back(input.i32());
        return values;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline bool validateNbtTree(const nbt::Value& value, std::size_t depth,
                            std::size_t& nodes,
                            std::size_t& approximateBytes) {
    if (depth > kMaxNbtDepth || ++nodes > kMaxNbtNodes ||
        !nbt::isValidTag(value.tag) || value.tag == nbt::End)
        return false;
    const auto addBytes = [&](std::size_t amount) {
        if (amount > kMaxPayloadBytes -
                         std::min(approximateBytes, kMaxPayloadBytes))
            return false;
        approximateBytes += amount;
        return true;
    };
    if (!addBytes(value.str.size()) ||
        !addBytes(value.byteArray.size()) ||
        value.intArray.size() > kMaxNbtNodes ||
        value.longArray.size() > kMaxNbtNodes)
        return false;
    if (value.tag == nbt::List) {
        if (value.list.size() > kMaxNbtNodes ||
            (value.list.empty() && !nbt::isValidTag(value.listElement)) ||
            (!value.list.empty() &&
             (!nbt::isValidTag(value.listElement) ||
              value.listElement == nbt::End)))
            return false;
        for (const auto& child : value.list) {
            if (child.tag != value.listElement ||
                !validateNbtTree(child, depth + 1, nodes, approximateBytes))
                return false;
        }
    } else if (value.tag == nbt::Compound) {
        if (value.comp.size() > kMaxNbtNodes) return false;
        for (const auto& [name, child] : value.comp) {
            if (!addBytes(name.size()) ||
                !validateNbtTree(child, depth + 1, nodes, approximateBytes))
                return false;
        }
    } else if (value.tag == nbt::IntArray) {
        if (!addBytes(value.intArray.size() * sizeof(std::int32_t)))
            return false;
    } else if (value.tag == nbt::LongArray) {
        if (!addBytes(value.longArray.size() * sizeof(std::int64_t)))
            return false;
    }
    return true;
}

inline bool validNbtTree(const nbt::Value& value) {
    std::size_t nodes = 0;
    std::size_t bytes = 0;
    return validateNbtTree(value, 0, nodes, bytes);
}

inline bool readNbtString(ReadBuffer& input, std::string& output) {
    const auto length = input.u16();
    const auto bytes = input.bytes(length);
    output.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

inline std::optional<nbt::Value> readNbtPayload(
    ReadBuffer& input, nbt::Tag tag, std::size_t depth,
    std::size_t& nodes) {
    if (depth > kMaxNbtDepth || ++nodes > kMaxNbtNodes ||
        !nbt::isValidTag(tag) || tag == nbt::End)
        return std::nullopt;
    nbt::Value value;
    value.tag = tag;
    switch (tag) {
    case nbt::Byte: value.b = input.i8(); break;
    case nbt::Short: value.s = input.i16(); break;
    case nbt::Int: value.i = input.i32(); break;
    case nbt::Long: value.l = input.i64(); break;
    case nbt::Float: value.f = input.f32(); break;
    case nbt::Double: value.d = input.f64(); break;
    case nbt::ByteArray: {
        const auto count = input.i32();
        if (count < 0 || static_cast<std::size_t>(count) > kMaxPayloadBytes)
            return std::nullopt;
        value.byteArray = input.bytes(static_cast<std::size_t>(count));
        break;
    }
    case nbt::String:
        if (!readNbtString(input, value.str)) return std::nullopt;
        break;
    case nbt::List: {
        const auto elementTag = static_cast<nbt::Tag>(input.u8());
        const auto count = input.i32();
        if (!nbt::isValidTag(elementTag) || count < 0 ||
            static_cast<std::size_t>(count) > kMaxNbtNodes ||
            (count != 0 && elementTag == nbt::End))
            return std::nullopt;
        value.listElement = elementTag;
        value.list.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index = 0; index < count; ++index) {
            auto child = readNbtPayload(input, elementTag, depth + 1, nodes);
            if (!child) return std::nullopt;
            value.list.push_back(std::move(*child));
        }
        break;
    }
    case nbt::Compound: {
        for (;;) {
            const auto childTag = static_cast<nbt::Tag>(input.u8());
            if (childTag == nbt::End) break;
            if (!nbt::isValidTag(childTag) ||
                value.comp.size() >= kMaxNbtNodes)
                return std::nullopt;
            std::string name;
            if (!readNbtString(input, name)) return std::nullopt;
            auto child = readNbtPayload(input, childTag, depth + 1, nodes);
            if (!child) return std::nullopt;
            value.comp.emplace_back(std::move(name), std::move(*child));
        }
        break;
    }
    case nbt::IntArray: {
        const auto count = input.i32();
        if (count < 0 || static_cast<std::size_t>(count) > kMaxNbtNodes)
            return std::nullopt;
        value.intArray.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index = 0; index < count; ++index)
            value.intArray.push_back(input.i32());
        break;
    }
    case nbt::LongArray: {
        const auto count = input.i32();
        if (count < 0 || static_cast<std::size_t>(count) > kMaxNbtNodes)
            return std::nullopt;
        value.longArray.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index = 0; index < count; ++index)
            value.longArray.push_back(input.i64());
        break;
    }
    case nbt::End: return std::nullopt;
    }
    return value;
}

inline std::optional<nbt::Value> readAnonymousCompound(ReadBuffer& input) {
    try {
        if (input.remaining() > kMaxPayloadBytes ||
            static_cast<nbt::Tag>(input.u8()) != nbt::Compound)
            return std::nullopt;
        std::size_t nodes = 0;
        auto value = readNbtPayload(input, nbt::Compound, 0, nodes);
        if (!value || !validNbtTree(*value)) return std::nullopt;
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline std::optional<std::vector<std::uint8_t>> writeAnonymousCompound(
    const nbt::Value& value) {
    if (value.tag != nbt::Compound || !validNbtTree(value))
        return std::nullopt;
    try {
        auto bytes = writeAnonymousNbt(value);
        if (bytes.size() > kMaxPayloadBytes) return std::nullopt;
        return bytes;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline std::optional<std::array<std::int32_t, 3>> blockPosition(
    const nbt::Value* value) {
    if (!value) return std::nullopt;
    std::array<std::int32_t, 3> result{};
    if (value->tag == nbt::IntArray && value->intArray.size() == 3) {
        std::copy(value->intArray.begin(), value->intArray.end(), result.begin());
        return result;
    }
    if (value->tag == nbt::List && value->list.size() == 3) {
        for (std::size_t i = 0; i < result.size(); ++i) {
            if (value->list[i].tag != nbt::Int) return std::nullopt;
            result[i] = value->list[i].i;
        }
        return result;
    }
    return std::nullopt;
}

inline nbt::Value packedBlockPosition(const std::array<std::int32_t, 3>& pos) {
    nbt::Value value;
    value.tag = nbt::IntArray;
    value.intArray.assign(pos.begin(), pos.end());
    return value;
}

inline std::int32_t signExtend(std::uint64_t value, unsigned bits) noexcept {
    const std::uint64_t mask = (std::uint64_t{1} << bits) - 1;
    const std::uint64_t sign = std::uint64_t{1} << (bits - 1);
    value &= mask;
    if ((value & sign) == 0) return static_cast<std::int32_t>(value);
    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(value) -
        static_cast<std::int64_t>(std::uint64_t{1} << bits));
}

inline std::array<std::int32_t, 3> unpackPosition(std::uint64_t packed) noexcept {
    return {signExtend(packed >> 38, 26), signExtend(packed, 12),
            signExtend(packed >> 12, 26)};
}

inline bool writeItemNameId(std::string_view name, WriteBuffer& output) {
    std::string qualified;
    if (name.find(':') == std::string_view::npos) {
        qualified = "minecraft:";
        qualified.append(name);
        name = qualified;
    }
    if (!isResourceIdentifier(name)) return false;
    const auto found = gen::itemIdByName().find(name);
    if (found == gen::itemIdByName().end() ||
        found->second > static_cast<std::uint32_t>(
                            std::numeric_limits<std::int32_t>::max()))
        return false;
    output.varint(static_cast<std::int32_t>(found->second));
    return true;
}

inline std::optional<std::string_view> itemName(std::int32_t id) {
    if (id < 0) return std::nullopt;
    const auto found = std::find_if(gen::kItems.begin(), gen::kItems.end(),
        [id](const auto& entry) {
            return entry.second == static_cast<std::uint32_t>(id);
        });
    if (found == gen::kItems.end()) return std::nullopt;
    return found->first;
}

inline std::optional<std::string_view> shapeName(std::int32_t id) {
    static constexpr std::array<std::string_view, 5> names{
        "small_ball", "large_ball", "star", "creeper", "burst"};
    if (id < 0 || static_cast<std::size_t>(id) >= names.size())
        return std::nullopt;
    return names[static_cast<std::size_t>(id)];
}

inline std::optional<std::int32_t> shapeId(std::string_view name) {
    static constexpr std::array<std::string_view, 5> names{
        "small_ball", "large_ball", "star", "creeper", "burst"};
    const auto found = std::find(names.begin(), names.end(), name);
    if (found == names.end()) return std::nullopt;
    return static_cast<std::int32_t>(std::distance(names.begin(), found));
}

inline std::optional<nbt::Value> decodeExplosion(ReadBuffer& input) {
    try {
        const auto shape = shapeName(input.varint());
        if (!shape) return std::nullopt;
        auto colors = readIntList(input, kMaxFireworkColors);
        auto fadeColors = readIntList(input, kMaxFireworkColors);
        if (!colors || !fadeColors) return std::nullopt;
        const bool trail = input.boolean();
        const bool twinkle = input.boolean();
        nbt::Value result = nbt::Value::makeCompound();
        result.set("shape", nbt::Value::makeString(std::string(*shape)));
        result.set("colors", intListToNbt(*colors));
        result.set("fade_colors", intListToNbt(*fadeColors));
        result.set("has_trail", nbt::Value::makeByte(trail ? 1 : 0));
        result.set("has_twinkle", nbt::Value::makeByte(twinkle ? 1 : 0));
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline bool encodeExplosion(const nbt::Value& saved, WriteBuffer& output) {
    if (saved.tag != nbt::Compound) return false;
    const auto* shapeValue = saved.get("shape");
    if (!shapeValue || shapeValue->tag != nbt::String) return false;
    const auto shape = shapeId(shapeValue->str);
    const auto* colors = saved.get("colors");
    const auto* fadeColors = saved.get("fade_colors");
    const auto validColors = [](const nbt::Value* list) {
        return !list || (list->tag == nbt::List &&
            list->list.size() <= kMaxFireworkColors &&
            std::all_of(list->list.begin(), list->list.end(),
                [](const nbt::Value& color) { return color.tag == nbt::Int; }));
    };
    if (!shape || !validColors(colors) || !validColors(fadeColors))
        return false;
    bool trail = false;
    bool twinkle = false;
    if (!exactBoolean(saved.get("has_trail"), false, trail) ||
        !exactBoolean(saved.get("has_twinkle"), false, twinkle))
        return false;
    // FireworkExplosionComponent.Type uses the exact 1.21.4 ids established
    // by the official server enum: SMALL_BALL=0 through BURST=4.
    output.varint(*shape);
    for (const auto* list : {colors, fadeColors}) {
        const std::size_t count = list ? list->list.size() : 0;
        output.varint(static_cast<std::int32_t>(count));
        if (list)
            for (const auto& color : list->list) output.i32(color.i);
    }
    output.boolean(trail);
    output.boolean(twinkle);
    return output.data.size() <= kMaxPayloadBytes;
}

inline bool writeProfileProperty(WriteBuffer& output,
                                 std::string_view name,
                                 std::string_view value,
                                 const std::optional<std::string>& signature) {
    if (!writeString(output, name) || !writeString(output, value)) return false;
    output.boolean(signature.has_value());
    if (signature && !writeString(output, *signature)) return false;
    return output.data.size() <= kMaxPayloadBytes;
}

struct ProfileProperty {
    std::string name;
    std::string value;
    std::optional<std::string> signature;
};

inline std::optional<std::vector<ProfileProperty>> profileProperties(
    const nbt::Value* saved) {
    std::vector<ProfileProperty> properties;
    if (!saved) return properties;
    if (saved->tag == nbt::List) {
        if (saved->list.size() > kMaxProfileProperties) return std::nullopt;
        properties.reserve(saved->list.size());
        for (const auto& entry : saved->list) {
            if (entry.tag != nbt::Compound) return std::nullopt;
            const auto* name = entry.get("name");
            const auto* value = entry.get("value");
            const auto* signature = entry.get("signature");
            if (!name || name->tag != nbt::String ||
                !value || value->tag != nbt::String ||
                (signature && signature->tag != nbt::String))
                return std::nullopt;
            ProfileProperty property{name->str, value->str, std::nullopt};
            if (signature) property.signature = signature->str;
            properties.push_back(std::move(property));
        }
        return properties;
    }
    if (saved->tag != nbt::Compound) return std::nullopt;
    for (const auto& [name, values] : saved->comp) {
        if (values.tag != nbt::List) return std::nullopt;
        for (const auto& value : values.list) {
            if (value.tag != nbt::String ||
                properties.size() >= kMaxProfileProperties)
                return std::nullopt;
            properties.push_back(ProfileProperty{name, value.str, std::nullopt});
        }
    }
    return properties;
}

inline nbt::Value profilePropertiesToNbt(
    const std::vector<ProfileProperty>& properties) {
    nbt::Value result = nbt::Value::makeList(nbt::Compound, properties.size());
    for (const auto& property : properties) {
        nbt::Value entry = nbt::Value::makeCompound();
        entry.set("name", nbt::Value::makeString(property.name));
        entry.set("value", nbt::Value::makeString(property.value));
        if (property.signature)
            entry.set("signature",
                      nbt::Value::makeString(*property.signature));
        result.list.push_back(std::move(entry));
    }
    return result;
}

inline bool encodeProfile(const nbt::Value& saved, WriteBuffer& output) {
    if (saved.tag != nbt::Compound) return false;
    const auto* name = saved.get("name");
    const auto* id = saved.get("id");
    if ((name && name->tag != nbt::String) ||
        (id && id->tag != nbt::IntArray && id->tag != nbt::List))
        return false;
    output.boolean(name != nullptr);
    if (name && !writeString(output, name->str, 16)) return false;

    output.boolean(id != nullptr);
    if (id) {
        std::array<std::int32_t, 4> words{};
        if (id->tag == nbt::IntArray) {
            if (id->intArray.size() != words.size()) return false;
            std::copy(id->intArray.begin(), id->intArray.end(), words.begin());
        } else {
            if (id->list.size() != words.size()) return false;
            for (std::size_t index = 0; index < words.size(); ++index) {
                if (id->list[index].tag != nbt::Int) return false;
                words[index] = id->list[index].i;
            }
        }
        for (const auto word : words) output.i32(word);
    }

    const auto properties = profileProperties(saved.get("properties"));
    if (!properties) return false;
    output.varint(static_cast<std::int32_t>(properties->size()));
    for (const auto& property : *properties) {
        if (!writeProfileProperty(output, property.name, property.value,
                                  property.signature))
            return false;
    }
    return output.data.size() <= kMaxPayloadBytes;
}

inline std::optional<nbt::Value> decodeProfile(ReadBuffer& input) {
    try {
        nbt::Value result = nbt::Value::makeCompound();
        if (input.boolean()) {
            auto name = readString(input, 16);
            if (!name) return std::nullopt;
            result.set("name", nbt::Value::makeString(std::move(*name)));
        }
        if (input.boolean()) {
            nbt::Value uuid;
            uuid.tag = nbt::IntArray;
            uuid.intArray.reserve(4);
            for (int index = 0; index < 4; ++index)
                uuid.intArray.push_back(input.i32());
            result.set("id", std::move(uuid));
        }
        const auto count = input.varint();
        if (count < 0 ||
            static_cast<std::size_t>(count) > kMaxProfileProperties)
            return std::nullopt;
        std::vector<ProfileProperty> properties;
        properties.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index = 0; index < count; ++index) {
            auto name = readString(input);
            auto value = readString(input);
            if (!name || !value) return std::nullopt;
            std::optional<std::string> signature;
            if (input.boolean()) {
                signature = readString(input);
                if (!signature) return std::nullopt;
            }
            properties.push_back(ProfileProperty{std::move(*name),
                std::move(*value), std::move(signature)});
        }
        if (!properties.empty())
            result.set("properties", profilePropertiesToNbt(properties));
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline bool parseBannerLayers(ReadBuffer& input) {
    const auto count = input.varint();
    if (count < 0 || static_cast<std::size_t>(count) > kMaxBannerLayers)
        return false;
    for (std::int32_t index = 0; index < count; ++index) {
        if (!registryKey(kBannerPatternRegistryKeys, input.varint()))
            return false;
        const auto color = input.varint();
        if (!dyeColorName(color)) return false;
    }
    return true;
}

inline bool consumePayload(ReadBuffer& input, std::uint32_t id,
                           std::size_t depth) {
    if (!isComponentDepthValid(depth) ||
        input.remaining() > kMaxPayloadBytes)
        return false;
    try {
        switch (id) {
        case 39: { // charged_projectiles: ItemStack list
            const auto count = input.varint();
            if (count < 0 || static_cast<std::size_t>(count) > kMaxEntries)
                return false;
            for (std::int32_t index = 0; index < count; ++index)
                (void)ItemStack::read(input);
            return input.remaining() == 0;
        }
        case 50: // RegistryEntry<Instrument> raw registry id.
            return registryKey(kInstrumentRegistryKeys, input.varint()).has_value() &&
                   input.remaining() == 0;
        case 52: { // RegistryPair<JukeboxSong>, then show_in_tooltip.
            const auto isEntry = readExactBoolean(input);
            if (!isEntry) return false;
            if (*isEntry) {
                if (!registryKey(kJukeboxSongRegistryKeys, input.varint()))
                    return false;
            } else {
                const auto key = readString(input);
                if (!key || !vanillaRegistryId(kJukeboxSongRegistryKeys, *key))
                    return false;
            }
            return readExactBoolean(input).has_value() &&
                   input.remaining() == 0;
        }
        case 54: { // LodestoneTrackerComponent(Optional<GlobalPos>, tracked)
            if (input.boolean()) {
                const auto dimension = readString(input);
                if (!dimension || !isResourceIdentifier(*dimension)) return false;
                (void)input.u64(); // packed BlockPos
            }
            (void)input.boolean();
            return input.remaining() == 0;
        }
        case 55: {
            auto decoded = decodeExplosion(input);
            return decoded.has_value() && input.remaining() == 0;
        }
        case 56: { // flight duration then vanilla-capped explosion list
            (void)input.varint();
            const auto count = input.varint();
            if (count < 0 ||
                static_cast<std::size_t>(count) > kMaxFireworkExplosions)
                return false;
            for (std::int32_t index = 0; index < count; ++index)
                if (!decodeExplosion(input)) return false;
            return input.remaining() == 0;
        }
        case 57:
            return decodeProfile(input).has_value() && input.remaining() == 0;
        case 59: // List<BannerPatternsComponent.Layer>.
            return parseBannerLayers(input) && input.remaining() == 0;
        case 61: // Sherds: fixed four item registry values; item ids are VarInts.
            for (int index = 0; index < 4; ++index)
                if (input.varint() < 0) return false;
            return input.remaining() == 0;
        case 63: { // BlockStateComponent's string-to-string map
            const auto count = input.varint();
            if (count < 0 || static_cast<std::size_t>(count) > kMaxEntries)
                return false;
            std::vector<std::string> keys;
            keys.reserve(static_cast<std::size_t>(count));
            for (std::int32_t index = 0; index < count; ++index) {
                auto key = readString(input);
                auto value = readString(input);
                if (!key || !value || key->empty() ||
                    std::find(keys.begin(), keys.end(), *key) != keys.end())
                    return false;
                keys.push_back(std::move(*key));
            }
            return input.remaining() == 0;
        }
        case 64: { // BeeData list: compound NBT + ticks + minimum ticks
            const auto count = input.varint();
            if (count < 0 || static_cast<std::size_t>(count) > kMaxEntries)
                return false;
            for (std::int32_t index = 0; index < count; ++index) {
                const auto entityData = readAnonymousCompound(input);
                if (!entityData) return false;
                (void)input.varint();
                (void)input.varint();
            }
            return input.remaining() == 0;
        }
        default: return false;
        }
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace detail

// Validate one component payload in-place. The caller supplies a ReadBuffer
// containing exactly this component's payload; success consumes it completely.
inline bool consume(ReadBuffer& input, std::uint32_t id,
                    std::size_t depth) {
    return detail::consumePayload(input, id, depth);
}

// Convert persisted vanilla item-component NBT to the protocol-769 payload.
// nullopt means malformed input or an intentionally unsupported registry map.
inline std::optional<std::vector<std::uint8_t>> fromNbt(
    std::uint32_t id, const nbt::Value& saved, std::size_t depth) {
    if (!detail::isComponentDepthValid(depth) ||
        !detail::validNbtTree(saved))
        return std::nullopt;
    try {
        WriteBuffer output;
        switch (id) {
        case 39: { // ChargedProjectilesComponent: List<ItemStack>
            if (saved.tag != nbt::List || saved.list.size() > kMaxEntries)
                return std::nullopt;
            output.varint(static_cast<std::int32_t>(saved.list.size()));
            for (const auto& entry : saved.list) {
                if (entry.tag != nbt::Compound) return std::nullopt;
                auto stack = ItemStack::fromNbt(entry, depth + 1);
                if (!stack || stack->empty()) return std::nullopt;
                stack->write(output);
                if (output.data.size() > kMaxPayloadBytes)
                    return std::nullopt;
            }
            break;
        }
        case 50: { // RegistryEntry<Instrument> is persisted as its registry key.
            if (saved.tag != nbt::String) return std::nullopt;
            const auto id = detail::vanillaRegistryId(
                detail::kInstrumentRegistryKeys, saved.str);
            if (!id) return std::nullopt;
            output.varint(*id);
            break;
        }
        case 52: { // RegistryPair<JukeboxSong> key form + tooltip default.
            if (saved.tag != nbt::Compound) return std::nullopt;
            const auto* song = saved.get("song");
            if (!song || song->tag != nbt::String) return std::nullopt;
            const auto id = detail::vanillaRegistryId(
                detail::kJukeboxSongRegistryKeys, song->str);
            if (!id) return std::nullopt;
            bool showTooltip = true;
            if (!detail::exactBoolean(saved.get("show_in_tooltip"), true,
                                      showTooltip))
                return std::nullopt;
            // RegistryPair's packet codec selects the RegistryEntry branch
            // with true, followed by the captured registry's raw VarInt id.
            output.boolean(true);
            output.varint(*id);
            output.boolean(showTooltip);
            break;
        }
        case 54: { // LodestoneTrackerComponent(Optional<GlobalPos>, tracked)
            if (saved.tag != nbt::Compound) return std::nullopt;
            const auto* target = saved.get("target");
            if (target && target->tag != nbt::Compound) return std::nullopt;
            bool tracked = true;
            if (!detail::exactBoolean(saved.get("tracked"), true, tracked))
                return std::nullopt;
            output.boolean(target != nullptr);
            if (target) {
                const auto* dimension = target->get("dimension");
                const auto pos = detail::blockPosition(target->get("pos"));
                if (!dimension || dimension->tag != nbt::String ||
                    !detail::isProtocolString(dimension->str) || !pos)
                    return std::nullopt;
                if (!detail::writeString(output, dimension->str))
                    return std::nullopt;
                output.position((*pos)[0], (*pos)[1], (*pos)[2]);
            }
            output.boolean(tracked);
            break;
        }
        case 55:
            if (!detail::encodeExplosion(saved, output)) return std::nullopt;
            break;
        case 56: { // FireworksComponent: flightDuration + max-256 explosions
            if (saved.tag != nbt::Compound) return std::nullopt;
            const auto* flight = saved.get("flight_duration");
            const auto* explosions = saved.get("explosions");
            if ((flight && flight->tag != nbt::Int) ||
                (explosions && explosions->tag != nbt::List) ||
                (explosions && explosions->list.size() >
                                   kMaxFireworkExplosions))
                return std::nullopt;
            output.varint(flight ? flight->i : 0);
            const auto explosionCount = explosions ? explosions->list.size() : 0;
            output.varint(static_cast<std::int32_t>(explosionCount));
            if (explosions) {
                for (const auto& explosion : explosions->list)
                    if (!detail::encodeExplosion(explosion, output))
                        return std::nullopt;
            }
            break;
        }
        case 57:
            if (saved.tag != nbt::Compound ||
                !detail::encodeProfile(saved, output))
                return std::nullopt;
            break;
        case 59: { // BannerPatternsComponent.CODEC is directly List<Layer>.
            if (saved.tag != nbt::List ||
                saved.list.size() > kMaxBannerLayers)
                return std::nullopt;
            output.varint(static_cast<std::int32_t>(saved.list.size()));
            for (const auto& layer : saved.list) {
                if (layer.tag != nbt::Compound) return std::nullopt;
                const auto* pattern = layer.get("pattern");
                const auto* color = layer.get("color");
                if (!pattern || pattern->tag != nbt::String ||
                    !color || color->tag != nbt::String)
                    return std::nullopt;
                const auto patternId = detail::vanillaRegistryId(
                    detail::kBannerPatternRegistryKeys, pattern->str);
                const auto colorId = detail::dyeColorId(color->str);
                if (!patternId || !colorId) return std::nullopt;
                output.varint(*patternId);
                output.varint(*colorId);
            }
            break;
        }
        case 61: { // Sherds: vanilla codec is an ordered four-item record.
            if (saved.tag != nbt::List || saved.list.size() > 4)
                return std::nullopt;
            const auto brick = gen::itemIdByName().find("minecraft:brick");
            if (brick == gen::itemIdByName().end()) return std::nullopt;
            for (std::size_t index = 0; index < 4; ++index) {
                if (index >= saved.list.size()) {
                    output.varint(static_cast<std::int32_t>(brick->second));
                    continue;
                }
                const auto& entry = saved.list[index];
                if (entry.tag != nbt::String ||
                    !detail::writeItemNameId(entry.str, output))
                    return std::nullopt;
            }
            break;
        }
        case 63: { // BlockStateComponent: Map<String,String>
            if (saved.tag != nbt::Compound || saved.comp.size() > kMaxEntries)
                return std::nullopt;
            output.varint(static_cast<std::int32_t>(saved.comp.size()));
            for (const auto& [key, value] : saved.comp) {
                if (key.empty() || value.tag != nbt::String ||
                    !detail::writeString(output, key) ||
                    !detail::writeString(output, value.str))
                    return std::nullopt;
            }
            break;
        }
        case 64: { // BeeData list with empty entity_data default.
            if (saved.tag != nbt::List || saved.list.size() > kMaxEntries)
                return std::nullopt;
            output.varint(static_cast<std::int32_t>(saved.list.size()));
            for (const auto& bee : saved.list) {
                if (bee.tag != nbt::Compound) return std::nullopt;
                const auto* entityData = bee.get("entity_data");
                const auto* ticks = bee.get("ticks_in_hive");
                const auto* minTicks = bee.get("min_ticks_in_hive");
                if ((entityData && entityData->tag != nbt::Compound) ||
                    !ticks || ticks->tag != nbt::Int ||
                    !minTicks || minTicks->tag != nbt::Int)
                    return std::nullopt;
                const nbt::Value empty = nbt::Value::makeCompound();
                const auto encoded = detail::writeAnonymousCompound(
                    entityData ? *entityData : empty);
                if (!encoded) return std::nullopt;
                output.raw(encoded->data(), encoded->size());
                output.varint(ticks->i);
                output.varint(minTicks->i);
                if (output.data.size() > kMaxPayloadBytes)
                    return std::nullopt;
            }
            break;
        }
        default: return std::nullopt;
        }
        if (output.data.size() > kMaxPayloadBytes) return std::nullopt;
        return std::move(output.data);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Convert a complete protocol-769 component payload to persisted component
// NBT. Registry-backed components without proven vanilla id mappings return
// nullopt and remain opaque at the caller boundary.
inline std::optional<nbt::Value> toNbt(
    std::uint32_t id, const std::vector<std::uint8_t>& payload,
    std::size_t depth) {
    if (!detail::isComponentDepthValid(depth) ||
        payload.size() > kMaxPayloadBytes)
        return std::nullopt;
    try {
        ReadBuffer input(payload);
        nbt::Value result;
        switch (id) {
        case 39: {
            const auto count = input.varint();
            if (count < 0 || static_cast<std::size_t>(count) > kMaxEntries)
                return std::nullopt;
            result = nbt::Value::makeList(nbt::Compound,
                                           static_cast<std::size_t>(count));
            for (std::int32_t index = 0; index < count; ++index) {
                auto stack = ItemStack::read(input);
                if (stack.empty()) return std::nullopt;
                result.list.push_back(stack.toNbt(-1, depth + 1));
            }
            break;
        }
        case 50: { // RegistryEntry<Instrument> raw registry id.
            const auto key = detail::registryKey(
                detail::kInstrumentRegistryKeys, input.varint());
            if (!key) return std::nullopt;
            result = nbt::Value::makeString(std::string(*key));
            break;
        }
        case 52: { // RegistryPair entry or key branch, then tooltip boolean.
            const auto isEntry = detail::readExactBoolean(input);
            if (!isEntry) return std::nullopt;
            std::optional<std::string_view> key;
            if (*isEntry) {
                key = detail::registryKey(detail::kJukeboxSongRegistryKeys,
                                          input.varint());
            } else {
                const auto rawKey = detail::readString(input);
                if (rawKey) {
                    const auto id = detail::vanillaRegistryId(
                        detail::kJukeboxSongRegistryKeys, *rawKey);
                    if (id)
                        key = detail::registryKey(
                            detail::kJukeboxSongRegistryKeys, *id);
                }
            }
            const auto showTooltip = detail::readExactBoolean(input);
            if (!key || !showTooltip) return std::nullopt;
            result = nbt::Value::makeCompound();
            result.set("song", nbt::Value::makeString(std::string(*key)));
            result.set("show_in_tooltip",
                       nbt::Value::makeByte(*showTooltip ? 1 : 0));
            break;
        }
        case 54: {
            result = nbt::Value::makeCompound();
            if (input.boolean()) {
                auto dimension = detail::readString(input);
                if (!dimension || !isResourceIdentifier(*dimension))
                    return std::nullopt;
                const auto pos = detail::unpackPosition(input.u64());
                nbt::Value target = nbt::Value::makeCompound();
                target.set("dimension",
                           nbt::Value::makeString(std::move(*dimension)));
                target.set("pos", detail::packedBlockPosition(pos));
                result.set("target", std::move(target));
            }
            result.set("tracked", nbt::Value::makeByte(
                                      input.boolean() ? 1 : 0));
            break;
        }
        case 55: {
            auto decoded = detail::decodeExplosion(input);
            if (!decoded) return std::nullopt;
            result = std::move(*decoded);
            break;
        }
        case 56: {
            const auto flight = input.varint();
            const auto count = input.varint();
            if (count < 0 ||
                static_cast<std::size_t>(count) >
                    kMaxFireworkExplosions)
                return std::nullopt;
            result = nbt::Value::makeCompound();
            result.set("flight_duration", nbt::Value::makeInt(flight));
            nbt::Value explosions = nbt::Value::makeList(
                nbt::Compound, static_cast<std::size_t>(count));
            for (std::int32_t index = 0; index < count; ++index) {
                auto explosion = detail::decodeExplosion(input);
                if (!explosion) return std::nullopt;
                explosions.list.push_back(std::move(*explosion));
            }
            result.set("explosions", std::move(explosions));
            break;
        }
        case 57: {
            auto decoded = detail::decodeProfile(input);
            if (!decoded) return std::nullopt;
            result = std::move(*decoded);
            break;
        }
        case 59: { // BannerPatternsComponent.CODEC is directly List<Layer>.
            const auto count = input.varint();
            if (count < 0 ||
                static_cast<std::size_t>(count) > kMaxBannerLayers)
                return std::nullopt;
            result = nbt::Value::makeList(
                nbt::Compound, static_cast<std::size_t>(count));
            for (std::int32_t index = 0; index < count; ++index) {
                const auto pattern = detail::registryKey(
                    detail::kBannerPatternRegistryKeys, input.varint());
                const auto color = detail::dyeColorName(input.varint());
                if (!pattern || !color) return std::nullopt;
                nbt::Value layer = nbt::Value::makeCompound();
                layer.set("pattern", nbt::Value::makeString(
                                         std::string(*pattern)));
                layer.set("color", nbt::Value::makeString(
                                       std::string(*color)));
                result.list.push_back(std::move(layer));
            }
            break;
        }
        case 61: {
            result = nbt::Value::makeList(nbt::String, 4);
            for (int index = 0; index < 4; ++index) {
                const auto name = detail::itemName(input.varint());
                if (!name) return std::nullopt;
                result.list.push_back(
                    nbt::Value::makeString(std::string(*name)));
            }
            break;
        }
        case 63: {
            const auto count = input.varint();
            if (count < 0 || static_cast<std::size_t>(count) > kMaxEntries)
                return std::nullopt;
            result = nbt::Value::makeCompound();
            for (std::int32_t index = 0; index < count; ++index) {
                auto key = detail::readString(input);
                auto value = detail::readString(input);
                if (!key || !value || key->empty() || result.get(*key))
                    return std::nullopt;
                result.set(std::move(*key),
                           nbt::Value::makeString(std::move(*value)));
            }
            break;
        }
        case 64: {
            const auto count = input.varint();
            if (count < 0 || static_cast<std::size_t>(count) > kMaxEntries)
                return std::nullopt;
            result = nbt::Value::makeList(nbt::Compound,
                                           static_cast<std::size_t>(count));
            for (std::int32_t index = 0; index < count; ++index) {
                auto entityData = detail::readAnonymousCompound(input);
                if (!entityData) return std::nullopt;
                const auto ticks = input.varint();
                const auto minTicks = input.varint();
                nbt::Value bee = nbt::Value::makeCompound();
                bee.set("entity_data", std::move(*entityData));
                bee.set("ticks_in_hive", nbt::Value::makeInt(ticks));
                bee.set("min_ticks_in_hive", nbt::Value::makeInt(minTicks));
                result.list.push_back(std::move(bee));
            }
            break;
        }
        default: return std::nullopt;
        }
        if (input.remaining() != 0 || !detail::validNbtTree(result))
            return std::nullopt;
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace cppfm::item_component_nbt::additional_other
