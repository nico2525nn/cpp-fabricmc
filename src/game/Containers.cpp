// Containers implementation.
#include "Containers.hpp"
#include "Recipes.hpp"

namespace cppfm {

ItemStack* Menu::slotAt(int slot, ItemStack* playerInv) {
    switch (type) {
    case MenuType::Hopper:
        if (slot >= 0 && slot < 5) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 5 && slot < 41) return &playerInv[slot - 5 + 9];
        return nullptr;
    case MenuType::Dispenser:
        if (slot >= 0 && slot < 9) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 9 && slot < 45) return &playerInv[slot - 9 + 9];
        return nullptr;
    case MenuType::Chest:
    case MenuType::Barrel:
    case MenuType::ShulkerBox:
        if (slot >= 0 && slot < 27) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 27 && slot < 63) return &playerInv[slot - 27 + 9]; // main+hotbar
        return nullptr;
    case MenuType::Furnace:
        if (slot >= 0 && slot < 3) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 3 && slot < 39) return &playerInv[slot - 3 + 9];
        return nullptr;
    case MenuType::Crafting:
        if (slot == 0) return &craftResult;
        if (slot >= 1 && slot < 10) return &craftGrid[slot - 1];
        if (slot >= 10 && slot < 46) return &playerInv[slot - 10 + 9];
        return nullptr;
    case MenuType::Enchantment:
        if (slot >= 0 && slot < 2) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 2 && slot < 38) return &playerInv[slot - 2 + 9];
        return nullptr;
    case MenuType::Anvil:
        if (slot >= 0 && slot < 3) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 3 && slot < 39) return &playerInv[slot - 3 + 9];
        return nullptr;
    case MenuType::Brewing:
        if (slot >= 0 && slot < 5) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 5 && slot < 41) return &playerInv[slot - 5 + 9];
        return nullptr;
    case MenuType::Stonecutter:
        if (slot >= 0 && slot < 2) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 2 && slot < 38) return &playerInv[slot - 2 + 9];
        return nullptr;
    case MenuType::Grindstone:
        if (slot >= 0 && slot < 3) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 3 && slot < 39) return &playerInv[slot - 3 + 9];
        return nullptr;
    case MenuType::Smithing:
        if (slot >= 0 && slot < 4) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 4 && slot < 40) return &playerInv[slot - 4 + 9];
        return nullptr;
    case MenuType::Beacon:
        if (slot >= 0 && slot < 1) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 1 && slot < 37) return &playerInv[slot - 1 + 9];
        return nullptr;
    case MenuType::Loom:
        if (slot >= 0 && slot < 4) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 4 && slot < 40) return &playerInv[slot - 4 + 9];
        return nullptr;
    case MenuType::Crafter:
        if (slot >= 0 && slot < 9) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 9 && slot < 45) return &playerInv[slot - 9 + 9];
        return nullptr;
    case MenuType::CartographyTable:
        if (slot >= 0 && slot < 3) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 3 && slot < 39) return &playerInv[slot - 3 + 9];
        return nullptr;
    case MenuType::BlastFurnace:
    case MenuType::Smoker:
        if (slot >= 0 && slot < 3) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 3 && slot < 39) return &playerInv[slot - 3 + 9];
        return nullptr;
    case MenuType::Lectern:
        if (slot >= 0 && slot < 1) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 1 && slot < 37) return &playerInv[slot - 1 + 9];
        return nullptr;
    case MenuType::Merchant:
        if (slot >= 0 && slot < 3) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 3 && slot < 39) return &playerInv[slot - 3 + 9];
        return nullptr;
    case MenuType::Generic9x1:
        if (slot >= 0 && slot < 9) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 9 && slot < 45) return &playerInv[slot - 9 + 9];
        return nullptr;
    case MenuType::Generic9x2:
        if (slot >= 0 && slot < 18) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 18 && slot < 54) return &playerInv[slot - 18 + 9];
        return nullptr;
    case MenuType::Generic9x4:
        if (slot >= 0 && slot < 36) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 36 && slot < 72) return &playerInv[slot - 36 + 9];
        return nullptr;
    case MenuType::Generic9x6:
        if (slot >= 0 && slot < 54) return container ? &container[slot] : &extraSlots[slot];
        if (slot >= 54 && slot < 90) return &playerInv[slot - 54 + 9];
        return nullptr;
    }
    return nullptr;
}

const char* Menu::slotRegion(int slot) const {
    switch (type) {
    case MenuType::Hopper: return slot < 5 ? "container" : "player";
    case MenuType::Dispenser: return slot < 9 ? "container" : "player";
    case MenuType::Chest:
    case MenuType::Barrel:
    case MenuType::ShulkerBox: return slot < 27 ? "container" : "player";
    case MenuType::Furnace: return slot < 3 ? "container" : "player";
    case MenuType::Crafting:
        if (slot == 0) return "result";
        if (slot < 10) return "craft";
        return "player";
    case MenuType::Enchantment: return slot < 2 ? "container" : "player";
    case MenuType::Anvil: return slot < 3 ? "container" : "player";
    case MenuType::Brewing: return slot < 5 ? "container" : "player";
    case MenuType::Stonecutter: return slot < 2 ? "container" : "player";
    case MenuType::Grindstone: return slot < 3 ? "container" : "player";
    case MenuType::Smithing: return slot < 4 ? "container" : "player";
    case MenuType::Beacon: return slot < 1 ? "container" : "player";
    case MenuType::Loom: return slot < 4 ? "container" : "player";
    case MenuType::Crafter: return slot < 9 ? "container" : "player";
    case MenuType::CartographyTable: return slot < 3 ? "container" : "player";
    case MenuType::BlastFurnace: return slot < 3 ? "container" : "player";
    case MenuType::Smoker: return slot < 3 ? "container" : "player";
    case MenuType::Lectern: return slot < 1 ? "container" : "player";
    case MenuType::Merchant: return slot < 3 ? "container" : "player";
    case MenuType::Generic9x1: return slot < 9 ? "container" : "player";
    case MenuType::Generic9x2: return slot < 18 ? "container" : "player";
    case MenuType::Generic9x4: return slot < 36 ? "container" : "player";
    case MenuType::Generic9x6: return slot < 54 ? "container" : "player";
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
