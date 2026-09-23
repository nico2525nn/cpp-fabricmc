// Containers implementation.
#include "Containers.hpp"
#include "Recipes.hpp"

namespace cppfm {

ItemStack* Menu::slotAt(int slot, ItemStack* playerInv) {
    if (playerInventory) {
        if (slot == 0) return &craftResult;
        const int gridIndex = craftGridIndex(slot);
        if (gridIndex >= 0) return &craftGrid[gridIndex];
        if (slot >= 5 && slot < 46 && playerInv) return &playerInv[slot];
        return nullptr;
    }
    // An opened crafting table has ten menu slots, but none are backed by a
    // block-entity container: result first, then the 3x3 grid. Click handling
    // and menu snapshots must resolve those slots through the same storage.
    if (type == MenuType::Crafting) {
        if (slot == 0) return &craftResult;
        const int gridIndex = craftGridIndex(slot);
        if (gridIndex >= 0) return &craftGrid[gridIndex];
    }
    const auto typeIndex = static_cast<std::size_t>(type);
    if (typeIndex >= kMenuLayouts.size()) return nullptr;
    const int containerSlots = kMenuLayouts[typeIndex].containerSlots;
    if (slot >= 0 && slot < containerSlots)
        return container ? &container[slot] : &extraSlots[slot];
    if (slot >= containerSlots && slot < containerSlots + 36)
        return &playerInv[slot - containerSlots + 9];
    return nullptr;
}

void Menu::refreshCraftResult(const RecipeManager& recipes) {
    if (type != MenuType::Crafting) return;
    // 2x2 grid when only rows 0-1/cols 0-1 used is handled naturally by the matcher scanning the full 3x3.
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
