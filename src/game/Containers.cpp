// Containers implementation.
#include "Containers.hpp"
#include "Recipes.hpp"

namespace cppfm {

ItemStack* Menu::slotAt(int slot, ItemStack* playerInv) {
    switch (type) {
    case MenuType::Hopper:
        if (slot >= 0 && slot < 5) return &container[slot];
        if (slot >= 5 && slot < 41) return &playerInv[slot - 5 + 9];
        return nullptr;
    case MenuType::Dispenser:
        if (slot >= 0 && slot < 9) return &container[slot];
        if (slot >= 9 && slot < 45) return &playerInv[slot - 9 + 9];
        return nullptr;
    case MenuType::Chest:
        if (slot >= 0 && slot < 27) return &container[slot];
        if (slot >= 27 && slot < 63) return &playerInv[slot - 27 + 9]; // main+hotbar
        return nullptr;
    case MenuType::Furnace:
        if (slot >= 0 && slot < 3) return &container[slot];
        if (slot >= 3 && slot < 39) return &playerInv[slot - 3 + 9];
        return nullptr;
    case MenuType::Crafting:
        if (slot == 0) return &craftResult;
        if (slot >= 1 && slot < 10) return &craftGrid[slot - 1];
        if (slot >= 10 && slot < 46) return &playerInv[slot - 10 + 9];
        return nullptr;
    }
    return nullptr;
}

const char* Menu::slotRegion(int slot) const {
    switch (type) {
    case MenuType::Hopper: return slot < 5 ? "container" : "player";
    case MenuType::Dispenser: return slot < 9 ? "container" : "player";
    case MenuType::Chest: return slot < 27 ? "container" : "player";
    case MenuType::Furnace: return slot < 3 ? "container" : "player";
    case MenuType::Crafting:
        if (slot == 0) return "result";
        if (slot < 10) return "craft";
        return "player";
    }
    return "?";
}

void Menu::refreshCraftResult(const RecipeManager& recipes) {
    if (type != MenuType::Crafting) return;
    // 2x2 grid when only rows 0-1/cols 0-1 used is handled naturally by the
    // matcher scanning the full 3x3.
    const Recipe* r = recipes.findCrafting(
        std::vector<ItemStack>(std::begin(craftGrid), std::end(craftGrid)), 3, 3);
    craftResult = r ? r->result : ItemStack::air();
}

bool Menu::consumeCraftIngredients() {
    for (auto& s : craftGrid)
        if (!s.empty()) {
            if (--s.count <= 0) s = ItemStack::air();
        }
    return true;
}

} // namespace cppfm
