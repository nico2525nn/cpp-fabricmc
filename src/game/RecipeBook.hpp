#pragma once

#include "Recipes.hpp"
#include "../core/ByteBuffer.hpp"
#include "../generated/ItemIds.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cppfm::recipe_book {

struct NetworkEntry {
    std::int32_t displayId = 0;
    std::size_t recipeIndex = 0;
    // Zero is the recipe's primary display; additional displays use their
    // one-based position in Recipe::additionalDisplays.
    std::size_t displayIndex = 0;
    std::optional<std::int32_t> groupId;
};

inline bool hasDisplayData(Recipe::Kind kind, int width, int height,
                           const std::vector<Ingredient>& cells,
                           const std::vector<Ingredient>& ingredients,
                           const ItemStack& result,
                           bool smithingTrimResult) {
    if (result.empty() &&
        !(smithingTrimResult && kind == Recipe::Kind::Smithing))
        return false;
    switch (kind) {
    case Recipe::Kind::Shaped:
        return width > 0 && height > 0 &&
            cells.size() == static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height);
    case Recipe::Kind::Shapeless:
        return !ingredients.empty();
    case Recipe::Kind::Smelting:
    case Recipe::Kind::Stonecutting:
        return !cells.empty();
    case Recipe::Kind::Smithing:
        return cells.size() == 3;
    case Recipe::Kind::Special:
        // 1.21.4 has no "special" RecipeDisplay. Vanilla only sends a
        // display when the recipe can provide one; fake shapeless displays
        // teach the client an impossible recipe and corrupt the id mapping.
        return false;
    }
    return false;
}

inline bool hasDisplay(const Recipe& recipe) {
    if (hasDisplayData(recipe.kind, recipe.width, recipe.height,
                       recipe.cells, recipe.ingredients, recipe.result,
                       recipe.smithingTrimResult))
        return true;
    return std::any_of(recipe.additionalDisplays.begin(),
                       recipe.additionalDisplays.end(),
        [](const Recipe::BookDisplay& display) {
            return hasDisplayData(display.kind, display.width, display.height,
                                  display.cells, display.ingredients,
                                  display.result, display.smithingTrimResult);
        });
}

inline std::vector<NetworkEntry> buildNetworkEntries(
    const std::vector<Recipe>& recipes) {
    std::vector<NetworkEntry> entries;
    entries.reserve(recipes.size());
    std::unordered_map<std::string, std::int32_t> groupIds;
    std::int32_t nextGroupId = 0;
    for (std::size_t recipeIndex = 0; recipeIndex < recipes.size();
         ++recipeIndex) {
        const auto& recipe = recipes[recipeIndex];
        std::optional<std::int32_t> groupId;
        bool assignedGroup = false;
        const auto append = [&](std::size_t displayIndex, bool valid) {
            if (!valid) return;
            if (entries.size() >= static_cast<std::size_t>(
                    std::numeric_limits<std::int32_t>::max()))
                throw std::length_error("too many recipe-book displays");
            if (!assignedGroup && !recipe.group.empty()) {
                auto [it, inserted] = groupIds.try_emplace(recipe.group,
                                                           nextGroupId);
                if (inserted) ++nextGroupId;
                groupId = it->second;
                assignedGroup = true;
            }
            entries.push_back({static_cast<std::int32_t>(entries.size()),
                               recipeIndex, displayIndex, groupId});
        };
        append(0, hasDisplayData(recipe.kind, recipe.width, recipe.height,
                                 recipe.cells, recipe.ingredients, recipe.result,
                                 recipe.smithingTrimResult));
        for (std::size_t index = 0;
             index < recipe.additionalDisplays.size(); ++index) {
            const auto& display = recipe.additionalDisplays[index];
            append(index + 1, hasDisplayData(
                display.kind, display.width, display.height,
                display.cells, display.ingredients, display.result,
                display.smithingTrimResult));
        }
    }
    return entries;
}

inline std::optional<NetworkEntry> entryForDisplayId(
    const std::vector<Recipe>& recipes, std::int32_t displayId) {
    if (displayId < 0) return std::nullopt;
    const auto entries = buildNetworkEntries(recipes);
    if (static_cast<std::size_t>(displayId) >= entries.size())
        return std::nullopt;
    return entries[static_cast<std::size_t>(displayId)];
}

inline std::optional<std::size_t> recipeIndexForDisplayId(
    const std::vector<Recipe>& recipes, std::int32_t displayId) {
    const auto entry = entryForDisplayId(recipes, displayId);
    if (!entry) return std::nullopt;
    return entry->recipeIndex;
}

inline Recipe recipeForDisplay(const Recipe& source,
                               std::size_t displayIndex) {
    Recipe display = source;
    display.additionalDisplays.clear();
    if (displayIndex == 0) return display;
    if (displayIndex > source.additionalDisplays.size())
        throw std::out_of_range("recipe-book display index out of range");
    const auto& variant = source.additionalDisplays[displayIndex - 1];
    display.kind = variant.kind;
    display.width = variant.width;
    display.height = variant.height;
    display.cells = variant.cells;
    display.ingredients = variant.ingredients;
    display.cookingTicks = variant.cookingTicks;
    display.experience = variant.experience;
    display.cookingStation = variant.cookingStation;
    display.result = variant.result;
    display.smithingTrimResult = variant.smithingTrimResult;
    return display;
}

inline std::optional<std::int32_t> displayIdForRecipeIndex(
    const std::vector<Recipe>& recipes, std::size_t recipeIndex) {
    const auto entries = buildNetworkEntries(recipes);
    const auto found = std::find_if(entries.begin(), entries.end(),
        [recipeIndex](const NetworkEntry& entry) {
            return entry.recipeIndex == recipeIndex;
        });
    if (found == entries.end()) return std::nullopt;
    return found->displayId;
}

inline std::uint32_t itemId(std::string_view name) {
    const auto found = gen::itemIdByName().find(std::string(name));
    return found == gen::itemIdByName().end() ? 0U : found->second;
}

inline void writeItemDisplay(WriteBuffer& out, std::uint32_t id) {
    if (id == 0) {
        out.varint(0); // SlotDisplay.Empty
        return;
    }
    out.varint(2); // SlotDisplay.Item
    out.varint(static_cast<std::int32_t>(id));
}

inline void writeIngredientDisplay(WriteBuffer& out,
                                   const Ingredient& ingredient) {
    if (ingredient.items.empty()) {
        out.varint(0); // SlotDisplay.Empty
        return;
    }
    std::vector<std::uint32_t> itemIds(ingredient.items.begin(),
                                       ingredient.items.end());
    std::sort(itemIds.begin(), itemIds.end());
    if (itemIds.size() == 1) {
        writeItemDisplay(out, itemIds.front());
        return;
    }
    out.varint(7); // SlotDisplay.Composite
    out.varint(static_cast<std::int32_t>(itemIds.size()));
    for (const auto id : itemIds) writeItemDisplay(out, id);
}

inline void writeResultDisplay(WriteBuffer& out, const ItemStack& result) {
    if (result.empty()) {
        out.varint(0); // SlotDisplay.Empty
        return;
    }
    out.varint(3); // SlotDisplay.ItemStack: preserve result count/components
    result.write(out);
}

inline std::uint32_t cookingStationItem(Recipe::CookingStation station) {
    switch (station) {
    case Recipe::CookingStation::Furnace:
        return itemId("minecraft:furnace");
    case Recipe::CookingStation::BlastFurnace:
        return itemId("minecraft:blast_furnace");
    case Recipe::CookingStation::Smoker:
        return itemId("minecraft:smoker");
    case Recipe::CookingStation::Campfire:
        return itemId("minecraft:campfire");
    }
    return 0;
}

template <typename Display>
inline void writeDisplayData(WriteBuffer& out, const Display& display) {
    const auto craftingTable = itemId("minecraft:crafting_table");
    switch (display.kind) {
    case Recipe::Kind::Shaped:
        out.varint(1); // RecipeDisplay.ShapedCrafting
        out.varint(display.width);
        out.varint(display.height);
        out.varint(static_cast<std::int32_t>(display.cells.size()));
        for (const auto& ingredient : display.cells)
            writeIngredientDisplay(out, ingredient);
        writeResultDisplay(out, display.result);
        writeItemDisplay(out, craftingTable);
        return;
    case Recipe::Kind::Shapeless:
        out.varint(0); // RecipeDisplay.ShapelessCrafting
        out.varint(static_cast<std::int32_t>(display.ingredients.size()));
        for (const auto& ingredient : display.ingredients)
            writeIngredientDisplay(out, ingredient);
        writeResultDisplay(out, display.result);
        writeItemDisplay(out, craftingTable);
        return;
    case Recipe::Kind::Smelting:
        out.varint(2); // RecipeDisplay.Furnace
        writeIngredientDisplay(out, display.cells.front());
        out.varint(1); // SlotDisplay.AnyFuel
        writeResultDisplay(out, display.result);
        writeItemDisplay(out, cookingStationItem(display.cookingStation));
        out.varint(display.cookingTicks);
        out.f32(display.experience);
        return;
    case Recipe::Kind::Stonecutting:
        out.varint(3); // RecipeDisplay.Stonecutter
        writeIngredientDisplay(out, display.cells.front());
        writeResultDisplay(out, display.result);
        writeItemDisplay(out, itemId("minecraft:stonecutter"));
        return;
    case Recipe::Kind::Smithing:
        out.varint(4); // RecipeDisplay.Smithing
        for (const auto& ingredient : display.cells)
            writeIngredientDisplay(out, ingredient);
        if (display.smithingTrimResult) {
            out.varint(5); // SlotDisplay.SmithingTrimSlotDisplay
            writeIngredientDisplay(out, display.cells[1]); // base
            writeIngredientDisplay(out, display.cells[2]); // material
            writeIngredientDisplay(out, display.cells[0]); // pattern/template
        } else {
            writeResultDisplay(out, display.result);
        }
        writeItemDisplay(out, itemId("minecraft:smithing_table"));
        return;
    case Recipe::Kind::Special:
        break;
    }
    throw std::invalid_argument("recipe has no network display");
}

inline void writeDisplay(WriteBuffer& out, const Recipe& recipe,
                         std::size_t displayIndex = 0) {
    if (displayIndex == 0) {
        writeDisplayData(out, recipe);
        return;
    }
    if (displayIndex > recipe.additionalDisplays.size())
        throw std::out_of_range("recipe-book display index out of range");
    writeDisplayData(out, recipe.additionalDisplays[displayIndex - 1]);
}

inline WriteBuffer makeAddPacketBody(const std::vector<Recipe>& recipes,
                                    const std::vector<int>* requestedIndices,
                                    bool replace) {
    const auto entries = buildNetworkEntries(recipes);
    std::unordered_set<std::size_t> requested;
    if (requestedIndices) {
        requested.reserve(requestedIndices->size());
        for (const int index : *requestedIndices)
            if (index >= 0) requested.insert(static_cast<std::size_t>(index));
    }
    const auto selectedCount = requestedIndices
        ? static_cast<std::size_t>(std::count_if(
              entries.begin(), entries.end(), [&](const NetworkEntry& entry) {
                  return requested.contains(entry.recipeIndex);
              }))
        : entries.size();

    WriteBuffer out;
    out.varint(static_cast<std::int32_t>(selectedCount));
    for (const auto& entry : entries) {
        if (requestedIndices && !requested.contains(entry.recipeIndex))
            continue;
        const auto& recipe = recipes[entry.recipeIndex];
        out.varint(entry.displayId);
        writeDisplay(out, recipe, entry.displayIndex);
        out.boolean(entry.groupId.has_value()); // optional group ID
        if (entry.groupId) out.varint(*entry.groupId);
        out.varint(recipe.category); // RecipeBookCategory registry ID
        out.boolean(false); // no crafting-requirements ingredient set
        out.u8(0x03); // notification | highlight
    }
    out.boolean(replace);
    return out;
}

inline WriteBuffer makeRemovePacketBody(const std::vector<Recipe>& recipes,
                                        const std::vector<int>& recipeIndices) {
    const auto entries = buildNetworkEntries(recipes);
    std::unordered_set<std::size_t> requested;
    requested.reserve(recipeIndices.size());
    for (const int index : recipeIndices)
        if (index >= 0) requested.insert(static_cast<std::size_t>(index));

    WriteBuffer out;
    const auto count = static_cast<std::size_t>(std::count_if(
        entries.begin(), entries.end(), [&](const NetworkEntry& entry) {
            return requested.contains(entry.recipeIndex);
        }));
    out.varint(static_cast<std::int32_t>(count));
    for (const auto& entry : entries)
        if (requested.contains(entry.recipeIndex)) out.varint(entry.displayId);
    return out;
}

inline bool selectsVisibleRecipe(const std::vector<Recipe>& recipes,
                                 const std::vector<int>& recipeIndices) {
    for (const int index : recipeIndices) {
        if (index >= 0 && static_cast<std::size_t>(index) < recipes.size() &&
            hasDisplay(recipes[static_cast<std::size_t>(index)]))
            return true;
    }
    return false;
}

} // namespace cppfm::recipe_book
