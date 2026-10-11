#pragma once

#include "MenuInteraction.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace cppfm {

struct CraftingRecipePlacement final {
    std::array<ItemStack, kPlayerInventorySlotCount> inventory;
    std::array<ItemStack, 9> grid;
    int batches = 0;
};

namespace recipe_placement_detail {

struct SupplyGroup final {
    ItemStack exemplar;
    int count = 0;
};

struct IngredientCell final {
    const Ingredient* ingredient = nullptr;
    std::size_t gridIndex = 0;
    std::vector<std::size_t> compatibleGroups;
};

inline bool assignIngredients(
    const std::vector<IngredientCell>& cells,
    const std::vector<SupplyGroup>& supplies, int batches,
    std::vector<std::size_t>& assignedGroups) {
    struct SupplySlot final { std::size_t group; };
    std::vector<SupplySlot> slots;
    std::vector<std::vector<std::size_t>> groupSlots(supplies.size());
    for (std::size_t group = 0; group < supplies.size(); ++group) {
        if (batches > maxStackFor(supplies[group].exemplar)) continue;
        const int cellCapacity = std::min(
            static_cast<int>(cells.size()), supplies[group].count / batches);
        for (int i = 0; i < cellCapacity; ++i) {
            groupSlots[group].push_back(slots.size());
            slots.push_back({group});
        }
    }

    std::vector<std::vector<std::size_t>> cellSlots(cells.size());
    for (std::size_t cell = 0; cell < cells.size(); ++cell) {
        for (const std::size_t group : cells[cell].compatibleGroups) {
            const auto& compatibleSlots = groupSlots[group];
            cellSlots[cell].insert(cellSlots[cell].end(),
                                   compatibleSlots.begin(),
                                   compatibleSlots.end());
        }
        if (cellSlots[cell].empty()) return false;
    }

    // Match the most constrained recipe cells first. The augmenting-path
    // matcher can still reassign earlier cells when alternatives overlap.
    std::vector<std::size_t> order(cells.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t lhs,
                                               std::size_t rhs) {
        return cellSlots[lhs].size() < cellSlots[rhs].size();
    });

    std::vector<int> slotOwners(slots.size(), -1);
    std::function<bool(std::size_t, std::vector<bool>&)> augment =
        [&](std::size_t cell, std::vector<bool>& seen) {
            for (const std::size_t slot : cellSlots[cell]) {
                if (seen[slot]) continue;
                seen[slot] = true;
                const int previous = slotOwners[slot];
                if (previous < 0 ||
                    augment(static_cast<std::size_t>(previous), seen)) {
                    slotOwners[slot] = static_cast<int>(cell);
                    return true;
                }
            }
            return false;
        };

    for (const std::size_t cell : order) {
        std::vector<bool> seen(slots.size(), false);
        if (!augment(cell, seen)) return false;
    }

    assignedGroups.assign(cells.size(), supplies.size());
    for (std::size_t slot = 0; slot < slotOwners.size(); ++slot) {
        const int cell = slotOwners[slot];
        if (cell >= 0)
            assignedGroups[static_cast<std::size_t>(cell)] = slots[slot].group;
    }
    return std::all_of(assignedGroups.begin(), assignedGroups.end(),
                       [&](std::size_t group) {
                           return group < supplies.size();
                       });
}

inline void consumeEquivalentStacks(
    std::array<ItemStack, kPlayerInventorySlotCount>& inventory,
    const ItemStack& exemplar, int amount) {
    for (int slot = 9; slot <= 44 && amount > 0; ++slot) {
        auto& stack = inventory[static_cast<std::size_t>(slot)];
        if (stack.empty() || !stack.sameItemData(exemplar)) continue;
        const int taken = std::min<int>(stack.count, amount);
        stack.count = static_cast<std::int16_t>(stack.count - taken);
        amount -= taken;
        if (stack.count <= 0) stack = ItemStack::air();
    }
}

} // namespace recipe_placement_detail

// Build a complete, transactional recipe-book placement. Old grid contents
// must fit back into the player's inventory before any new ingredient is
// selected. For makeAll, alternatives are solved as a capacity-constrained
// matching problem so a flexible ingredient cannot consume the only item
// accepted by another recipe cell.
inline std::optional<CraftingRecipePlacement> planCraftingRecipePlacement(
    const Recipe& recipe, bool playerInventory, bool makeAll,
    const std::array<ItemStack, kPlayerInventorySlotCount>& inventory,
    const std::array<ItemStack, 9>& currentGrid) {
    using namespace recipe_placement_detail;
    CraftingRecipePlacement plan{inventory, {}, 0};
    for (auto& stack : plan.grid) stack = ItemStack::air();
    for (const auto& stack : currentGrid) {
        if (!stack.empty() &&
            !insertCompletePlayerStack(
                plan.inventory, stack,
                static_cast<std::uint16_t>(stack.count), maxStackFor(stack)))
            return std::nullopt;
    }

    std::vector<IngredientCell> cells;
    if (recipe.kind == Recipe::Kind::Shaped) {
        const int gridWidth = playerInventory ? 2 : 3;
        const int gridHeight = playerInventory ? 2 : 3;
        if (recipe.width <= 0 || recipe.width > gridWidth ||
            recipe.height <= 0 || recipe.height > gridHeight ||
            recipe.cells.size() <
                static_cast<std::size_t>(recipe.width * recipe.height))
            return std::nullopt;
        for (int y = 0; y < recipe.height; ++y) {
            for (int x = 0; x < recipe.width; ++x) {
                const auto& ingredient = recipe.cells[
                    static_cast<std::size_t>(y * recipe.width + x)];
                if (!ingredient.empty())
                    cells.push_back({&ingredient,
                                     static_cast<std::size_t>(y * 3 + x),
                                     {}});
            }
        }
    } else if (recipe.kind == Recipe::Kind::Shapeless) {
        static constexpr std::array<std::size_t, 4> kPlayerGridSlots{
            0, 1, 3, 4};
        const std::size_t maximum = playerInventory ? 4 : 9;
        if (recipe.ingredients.empty() || recipe.ingredients.size() > maximum)
            return std::nullopt;
        for (std::size_t i = 0; i < recipe.ingredients.size(); ++i) {
            const std::size_t gridIndex = playerInventory
                ? kPlayerGridSlots[i] : i;
            cells.push_back({&recipe.ingredients[i], gridIndex, {}});
        }
    } else {
        return std::nullopt;
    }
    if (cells.empty()) return std::nullopt;

    std::vector<SupplyGroup> supplies;
    for (int slot = 9; slot <= 44; ++slot) {
        const auto& stack = plan.inventory[static_cast<std::size_t>(slot)];
        if (stack.empty()) continue;
        const auto existing = std::find_if(
            supplies.begin(), supplies.end(), [&](const SupplyGroup& group) {
                return group.exemplar.sameItemData(stack);
            });
        if (existing == supplies.end()) supplies.push_back({stack, stack.count});
        else existing->count += stack.count;
    }

    for (auto& cell : cells) {
        for (std::size_t group = 0; group < supplies.size(); ++group)
            if (cell.ingredient->accepts(supplies[group].exemplar.itemId))
                cell.compatibleGroups.push_back(group);
        if (cell.compatibleGroups.empty()) return std::nullopt;
    }

    const int firstBatch = makeAll ? 64 : 1;
    for (int batches = firstBatch; batches >= 1; --batches) {
        std::vector<std::size_t> assignedGroups;
        if (!assignIngredients(cells, supplies, batches, assignedGroups))
            continue;

        for (std::size_t cell = 0; cell < cells.size(); ++cell) {
            const auto& exemplar = supplies[assignedGroups[cell]].exemplar;
            consumeEquivalentStacks(plan.inventory, exemplar, batches);
            ItemStack placed = exemplar;
            placed.count = static_cast<std::int16_t>(batches);
            plan.grid[cells[cell].gridIndex] = std::move(placed);
        }
        plan.batches = batches;
        return plan;
    }
    return std::nullopt;
}

// Select one matching component-preserving stack for furnace and stonecutter
// recipe placement. makeAll fills one legal input stack, not an unbounded
// aggregate across incompatible item components.
inline std::optional<ItemStack> takeRecipeIngredientStack(
    std::array<ItemStack, kPlayerInventorySlotCount>& inventory,
    const Ingredient& ingredient, bool makeAll) {
    using namespace recipe_placement_detail;
    for (int slot = 9; slot <= 44; ++slot) {
        const auto& candidate = inventory[static_cast<std::size_t>(slot)];
        if (candidate.empty() || !ingredient.accepts(candidate.itemId)) continue;
        int available = 0;
        for (int other = 9; other <= 44; ++other) {
            const auto& stack = inventory[static_cast<std::size_t>(other)];
            if (!stack.empty() && stack.sameItemData(candidate))
                available += stack.count;
        }
        const int amount = makeAll
            ? std::min(available, maxStackFor(candidate)) : 1;
        ItemStack placed = candidate;
        placed.count = static_cast<std::int16_t>(amount);
        consumeEquivalentStacks(inventory, candidate, amount);
        return placed;
    }
    return std::nullopt;
}

} // namespace cppfm
