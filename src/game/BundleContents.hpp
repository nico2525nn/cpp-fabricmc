// Typed access to the bundle_contents ItemStack component.
#pragma once

#include "Items.hpp"

namespace cppfm::bundle_contents {

inline constexpr std::uint32_t kComponentId = 40;

inline const std::vector<std::uint8_t>* payload(const ItemStack& bundle) {
    const auto component = std::find_if(
        bundle.components.begin(), bundle.components.end(),
        [](const auto& entry) { return entry.first == kComponentId; });
    return component == bundle.components.end() ? nullptr : &component->second;
}

inline std::optional<std::size_t> size(const ItemStack& bundle) {
    const auto* bytes = payload(bundle);
    if (!bytes ||
        !item_component_nbt::isValidProtocolComponentPayload(
            kComponentId, *bytes))
        return std::nullopt;
    try {
        ReadBuffer input(*bytes);
        const auto count = input.varint();
        if (count < 0 ||
            static_cast<std::size_t>(count) >
                item_component_nbt::kMaxBundleItems)
            return std::nullopt;
        return static_cast<std::size_t>(count);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline std::optional<std::vector<ItemStack>> decode(
    const ItemStack& bundle) {
    const auto* bytes = payload(bundle);
    if (!bytes ||
        !item_component_nbt::isValidProtocolComponentPayload(
            kComponentId, *bytes))
        return std::nullopt;
    try {
        ReadBuffer input(*bytes);
        const auto count = input.varint();
        if (count < 0 ||
            static_cast<std::size_t>(count) >
                item_component_nbt::kMaxBundleItems)
            return std::nullopt;
        std::vector<ItemStack> items;
        items.reserve(static_cast<std::size_t>(count));
        for (std::int32_t index = 0; index < count; ++index) {
            auto item = ItemStack::read(input);
            if (item.empty()) return std::nullopt;
            items.push_back(std::move(item));
        }
        if (input.remaining() != 0) return std::nullopt;
        return items;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline std::optional<std::vector<std::uint8_t>> encode(
    const std::vector<ItemStack>& items) {
    if (items.size() > item_component_nbt::kMaxBundleItems)
        return std::nullopt;
    try {
        WriteBuffer output;
        output.varint(static_cast<std::int32_t>(items.size()));
        for (const auto& item : items) {
            if (item.empty()) return std::nullopt;
            item.write(output);
            if (output.data.size() >
                ItemStack::kMaxComponentPayloadBytes)
                return std::nullopt;
        }
        return std::move(output.data);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Vanilla's selected index is transient; after removing the selected item it
// resets to -1. With no valid selection, BundleContentsComponent removes its
// first stack. Stage the new payload before changing the live stack so an
// allocation or validation failure cannot lose either item.
inline std::optional<ItemStack> popSelected(ItemStack& bundle) {
    if (bundle.empty() || bundle.name() != "minecraft:bundle")
        return std::nullopt;
    auto items = decode(bundle);
    if (!items || items->empty()) return std::nullopt;

    const auto selected = bundle.selectedBundleStackIndex;
    const std::size_t index = selected >= 0 &&
            static_cast<std::size_t>(selected) < items->size()
        ? static_cast<std::size_t>(selected) : 0;
    ItemStack extracted = (*items)[index];

    std::vector<ItemStack> remaining;
    remaining.reserve(items->size() - 1);
    for (std::size_t i = 0; i < items->size(); ++i)
        if (i != index) remaining.push_back((*items)[i]);

    auto nextPayload = encode(remaining);
    if (!nextPayload) return std::nullopt;
    std::optional<std::vector<std::uint8_t>> replacement;
    if (!remaining.empty()) replacement = std::move(*nextPayload);
    bundle.replaceComponentsForNbtEdit(
        {kComponentId}, kComponentId, std::move(replacement));
    bundle.selectedBundleStackIndex = -1;
    return extracted;
}

} // namespace cppfm::bundle_contents
