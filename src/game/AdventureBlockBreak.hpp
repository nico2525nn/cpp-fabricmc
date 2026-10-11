#pragma once

#include "BlockPredicate.hpp"
#include "Items.hpp"
#include "TagManager.hpp"
#include "../core/NBTValue.hpp"
#include "../generated/BlockRegistryIds.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cppfm {
namespace adventure_block_break_detail {

inline std::optional<std::int64_t> parseInteger(std::string_view value) {
    if (value.empty()) return std::nullopt;
    std::int64_t parsed = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size())
        return std::nullopt;
    return parsed;
}

inline std::string qualifyBlockTag(std::string name) {
    if (name.find(':') == std::string::npos)
        name.insert(0, "minecraft:");
    return name;
}

inline bool matchesProperties(ReadBuffer& input, std::uint16_t state) {
    const auto count = input.varint();
    if (count < 0 || static_cast<std::size_t>(count) >
                         item_component_nbt::kMaxCustomEffects)
        throw std::runtime_error("invalid block-state property count");
    const auto actual = gen::propsOf(state);
    bool matches = true;
    for (std::int32_t i = 0; i < count; ++i) {
        const auto name = input.string(32767);
        const bool exact = input.boolean();
        const auto found = std::find_if(actual.begin(), actual.end(),
            [&](const auto& property) { return property.first == name; });
        if (exact) {
            const auto value = input.string(32767);
            matches = matches && found != actual.end() &&
                      found->second == value;
        } else {
            const auto minimum = input.string(32767);
            const auto maximum = input.string(32767);
            if (found == actual.end()) {
                matches = false;
                continue;
            }
            const auto value = parseInteger(found->second);
            if (!value) {
                matches = false;
                continue;
            }
            if (!minimum.empty()) {
                const auto lower = parseInteger(minimum);
                if (!lower || *value < *lower) matches = false;
            }
            if (!maximum.empty()) {
                const auto upper = parseInteger(maximum);
                if (!upper || *value > *upper) matches = false;
            }
        }
    }
    return matches;
}

template <typename NbtSnapshotProvider>
inline bool matchesPredicate(ReadBuffer& input, std::uint16_t state,
                             const TagManager& tags,
                             NbtSnapshotProvider&& snapshotNbt) {
    const auto* definition = gen::blockByState(state);
    if (!definition) return false;

    bool matches = true;
    if (input.boolean()) {
        const auto holderSet = input.varint();
        matches = false;
        if (holderSet == 0) {
            const auto tagName = qualifyBlockTag(input.string(32767));
            const auto tag = tags.blockTags.find(tagName);
            matches = tag != tags.blockTags.end() &&
                tag->second.contains(definition->defaultState);
        } else if (holderSet > 0) {
            const auto count = holderSet - 1;
            if (count > static_cast<std::int32_t>(
                            item_component_nbt::kMaxCustomEffects))
                return false;
            const auto registryId = gen::blockRegistryId(definition->name);
            for (std::int32_t i = 0; i < count; ++i) {
                const auto candidate = input.varint();
                if (candidate < 0) return false;
                if (registryId && static_cast<std::uint32_t>(candidate) ==
                                      *registryId)
                    matches = true;
            }
        } else {
            return false;
        }
    }

    if (input.boolean()) {
        const bool propertiesMatch = matchesProperties(input, state);
        matches = matches && propertiesMatch;
    }

    if (input.boolean()) {
        const auto blockNbt = item_component_nbt::readAnonymousNbt(input);
        if (!blockNbt || blockNbt->tag != nbt::Compound) return false;
        const auto* actualNbt = matches ? snapshotNbt() : nullptr;
        matches = actualNbt &&
                  nbt::matchesSubset(*blockNbt, *actualNbt, true);
    }
    return matches;
}

} // namespace adventure_block_break_detail

template <typename NbtSnapshotProvider>
inline bool itemCanBreakBlockInAdventure(
    const ItemStack& item, std::uint16_t blockState,
    const TagManager& tags, NbtSnapshotProvider&& provideBlockEntityNbt) {
    if (item.empty()) return false;
    const auto component = std::find_if(item.components.begin(),
        item.components.end(), [](const auto& value) {
            return value.first == ItemStack::kCanBreakComponentId;
        });
    if (component == item.components.end()) return false;

    try {
        ReadBuffer input(component->second);
        const auto count = input.varint();
        if (count < 0 || static_cast<std::size_t>(count) >
                             item_component_nbt::kMaxCustomEffects)
            return false;
        bool allowed = false;
        bool blockEntityNbtLoaded = false;
        const nbt::Value* blockEntityNbt = nullptr;
        const auto snapshotNbtOnce = [&]() -> const nbt::Value* {
            if (!blockEntityNbtLoaded) {
                blockEntityNbt = provideBlockEntityNbt();
                blockEntityNbtLoaded = true;
            }
            return blockEntityNbt;
        };
        for (std::int32_t i = 0; i < count; ++i)
            allowed = adventure_block_break_detail::matchesPredicate(
                          input, blockState, tags, snapshotNbtOnce) || allowed;
        (void)input.boolean(); // showTooltip
        return input.remaining() == 0 && allowed;
    } catch (const std::exception&) {
        return false;
    }
}

inline bool itemCanBreakBlockInAdventure(const ItemStack& item,
                                         std::uint16_t blockState,
                                         const TagManager& tags) {
    return itemCanBreakBlockInAdventure(
        item, blockState, tags,
        []() -> const nbt::Value* { return nullptr; });
}

template <typename NbtSnapshotProvider>
inline bool canPlayerBreakBlockInAdventure(std::uint8_t gameMode,
                                            const ItemStack& held,
                                            std::uint16_t blockState,
                                            const TagManager& tags,
                                            NbtSnapshotProvider&&
                                                provideBlockEntityNbt) {
    return gameMode == 2 &&
        itemCanBreakBlockInAdventure(held, blockState, tags,
                                     std::forward<NbtSnapshotProvider>(
                                         provideBlockEntityNbt));
}

inline bool canPlayerBreakBlockInAdventure(std::uint8_t gameMode,
                                            const ItemStack& held,
                                            std::uint16_t blockState,
                                            const TagManager& tags) {
    return gameMode == 2 &&
        itemCanBreakBlockInAdventure(held, blockState, tags);
}

} // namespace cppfm
