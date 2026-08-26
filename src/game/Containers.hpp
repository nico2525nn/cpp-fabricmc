// Containers: server-authoritative menus (chest / furnace / crafting table)
// with vanilla click-mode handling (plan3.md "チェスト/かまどUI").
//
// Menu slot layout follows the vanilla protocol tables:
//   generic_9x3 : 0..26 container rows, 27..53 main inv, 54..62 hotbar
//   furnace     : 0 input, 1 fuel, 2 output, 3..29 main inv, 30..38 hotbar
//   crafting    : 0 result, 1..9 grid, 10..36 main inv, 37..45 hotbar
#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include "BlockEntities.hpp"
#include "Items.hpp"
#include "../generated/BlockStates.hpp"

namespace cppfm {

// Open Screen inventoryType ids = vanilla MenuType registry order (1.21.x).
namespace menus {
constexpr int kGeneric9x1 = 0, kGeneric9x2 = 1, kGeneric9x3 = 2,
              kGeneric9x4 = 3, kGeneric9x5 = 4, kGeneric9x6 = 5,
              kGeneric3x3 = 6, kAnvil = 8, kBeacon = 9, kBlastFurnace = 10,
              kBrewingStand = 11, kCrafting = 12, kEnchantment = 13,
              kFurnace = 14, kGrindstone = 15, kHopper = 16, kLectern = 17,
              kLoom = 18, kMerchant = 19, kShulkerBox = 20, kSmithing = 21,
              kSmoker = 22, kCartographyTable = 23, kStonecutter = 24;
}

enum class MenuType { Chest, Furnace, Crafting, Hopper, Dispenser };

class RecipeManager;

class Menu {
public:
    MenuType type = MenuType::Chest;
    std::int32_t windowId = 0;
    std::int64_t blockKey = -1;                  // packed pos, -1 for crafting

    // backing stores ---------------------------------------------------------
    ItemStack* container = nullptr;              // chest slots (27) | furnace(3)
    int containerCount = 0;
    ItemStack craftGrid[9];                      // crafting table only
    ItemStack craftResult;                       // cached result

    // transient view of the owning player's inventory is external (Player.inv)

    int containerCount_ = 0;             // hopper 5 / dispenser 9
    int totalSlots() const {
        switch (type) {
        case MenuType::Chest: return 27 + 36;
        case MenuType::Furnace: return 3 + 36;
        case MenuType::Crafting: return 10 + 36;
        case MenuType::Hopper: return 5 + 36;
        case MenuType::Dispenser: return 9 + 36;
        case MenuType::Barrel: return 27 + 36;
        case MenuType::ShulkerBox: return 27 + 36;
        }
        return 63;
    }
    int openScreenTypeId() const {
        switch (type) {
        case MenuType::Chest: return menus::kGeneric9x3;
        case MenuType::Furnace: return menus::kFurnace;
        case MenuType::Crafting: return menus::kCrafting;
        case MenuType::Hopper: return menus::kHopper;
        case MenuType::Dispenser: return menus::kGeneric3x3;
        case MenuType::Barrel: return menus::kGeneric9x3;
        case MenuType::ShulkerBox: return menus::kShulkerBox;
        }
        return menus::kGeneric9x3;
    }

    // Map a protocol slot number to a mutable stack pointer (nullptr if none).
    ItemStack* slotAt(int slot, ItemStack* playerInv /*46*/);
    const char* slotRegion(int slot) const;

    // Crafting helpers
    void refreshCraftResult(const RecipeManager& recipes);
    bool consumeCraftIngredients();              // decrement grid once

    // Furnace helpers
    FurnaceData* furnace() {
        if (!blockEntity || blockEntity->kind != BlockEntity::Kind::Furnace)
            return nullptr;
        return &blockEntity->furnace;
    }
    BlockEntity* blockEntity = nullptr;
};

} // namespace cppfm
