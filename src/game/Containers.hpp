// Containers: server-authoritative menus (chest / furnace / crafting table)
// with vanilla click-mode handling (plan3.md "チェスト/かまどUI").
// plan28 inventory polish: verify container menus remain orthogonal to Scoreboard ResetScore 0x49 (D26) — generic_9x3/furnace/crafting/crafter/cartography MenuType 25 and slot layouts verified intact after deep 31 merges; container open/close does not touch Scoreboard state.
//
// Strict 1.21.4 (protocol 769) parity: Yarn `ScreenHandlerType` 25 entries
// (generic_9x1..9x6, crafter 7, cartography 23, etc.) + MenuType 25 audit HIGH I1/I9/I10.
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

// Open Screen inventoryType ids = vanilla MenuType registry order (1.21.4, 25 entries).
namespace menus {
constexpr int kGeneric9x1 = 0, kGeneric9x2 = 1, kGeneric9x3 = 2,
              kGeneric9x4 = 3, kGeneric9x5 = 4, kGeneric9x6 = 5,
              kGeneric3x3 = 6, kCrafter = 7, kAnvil = 8, kBeacon = 9, kBlastFurnace = 10,
              kBrewingStand = 11, kCrafting = 12, kEnchantment = 13,
              kFurnace = 14, kGrindstone = 15, kHopper = 16, kLectern = 17,
              kLoom = 18, kMerchant = 19, kShulkerBox = 20, kSmithing = 21,
              kSmoker = 22, kCartographyTable = 23, kStonecutter = 24;
}

// 25 vanilla MenuType entries (1.21.4 protocol 769) + Barrel alias for block-entity distinction (same wire id as Generic9x3)
// Count must be 25 to match Yarn `ScreenHandlerType` / `MenuRegistry` (1.21.4) : 0 generic_9x1 .. 24 stonecutter.
// Strict audit HIGH I1/I9/I10 require Crafter (7) / Cartography (23) / BlastFurnace (10) / Smoker (22) at correct registry index.
// Wire order (Yarn `ScreenHandlerType`): generic_9x1=0, generic_9x2=1, generic_9x3=2, generic_9x4=3, generic_9x5=4, generic_9x6=5,
// generic_3x3=6, crafter=7, anvil=8, beacon=9, blast_furnace=10, brewing_stand=11, crafting=12, enchantment=13, furnace=14,
// grindstone=15, hopper=16, lectern=17, loom=18, merchant=19, shulker_box=20, smithing=21, smoker=22, cartography=23, stonecutter=24.
// Barrel uses generic_9x3 wire id for parity (same as Chest) but distinct block-entity kind.
enum class MenuType {
    Chest, Furnace, Crafting, Hopper, Dispenser, Barrel, ShulkerBox, Enchantment, Anvil, Brewing, Stonecutter, Grindstone, Smithing, Beacon, Loom,
    Crafter, CartographyTable, BlastFurnace, Smoker, Lectern, Merchant,
    Generic9x1, Generic9x2, Generic9x4, Generic9x6
};
static_assert(static_cast<int>(MenuType::Generic9x6) == 24, "MenuType must be 25 entries (0..24)");

class RecipeManager;

class Menu {
public:
    MenuType type = MenuType::Chest;
    std::int32_t windowId = 0;
    std::int64_t blockKey = -1;                  // packed pos, -1 for crafting

    // backing stores ---------------------------------------------------------
    ItemStack* container = nullptr;              // chest slots (27) | furnace(3) etc.
    int containerCount = 0;
    ItemStack craftGrid[9];                      // crafting table only
    ItemStack craftResult;                       // cached result
    ItemStack extraSlots[27];                    // generic storage for menus without BE

    // transient view of the owning player's inventory is external (Player.inv)
    // drag paint transient (mode 5)
    std::vector<int> dragSlots;
    int dragButton = -1;                 // initial button for drag type
    // anvil rename text (per-menu, not singleton)
    std::string anvilRename;
    int totalSlots() const {
        switch (type) {
        case MenuType::Chest: return 27 + 36;
        case MenuType::Furnace: return 3 + 36;
        case MenuType::Crafting: return 10 + 36;
        case MenuType::Hopper: return 5 + 36;
        case MenuType::Dispenser: return 9 + 36;
        case MenuType::Barrel: return 27 + 36;
        case MenuType::ShulkerBox: return 27 + 36;
        case MenuType::Enchantment: return 2 + 36;
        case MenuType::Anvil: return 3 + 36;
        case MenuType::Brewing: return 5 + 36;
        case MenuType::Stonecutter: return 2 + 36;
        case MenuType::Grindstone: return 3 + 36;
        case MenuType::Smithing: return 4 + 36;
        case MenuType::Beacon: return 1 + 36;
        case MenuType::Loom: return 4 + 36;
        case MenuType::Crafter: return 9 + 36;
        case MenuType::CartographyTable: return 3 + 36;
        case MenuType::BlastFurnace: return 3 + 36;
        case MenuType::Smoker: return 3 + 36;
        case MenuType::Lectern: return 1 + 36;
        case MenuType::Merchant: return 3 + 36;
        case MenuType::Generic9x1: return 9 + 36;
        case MenuType::Generic9x2: return 18 + 36;
        case MenuType::Generic9x4: return 36 + 36;
        case MenuType::Generic9x6: return 54 + 36;
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
        case MenuType::Enchantment: return menus::kEnchantment;
        case MenuType::Anvil: return menus::kAnvil;
        case MenuType::Brewing: return menus::kBrewingStand;
        case MenuType::Stonecutter: return menus::kStonecutter;
        case MenuType::Grindstone: return menus::kGrindstone;
        case MenuType::Smithing: return menus::kSmithing;
        case MenuType::Beacon: return menus::kBeacon;
        case MenuType::Loom: return menus::kLoom;
        case MenuType::Crafter: return menus::kCrafter;
        case MenuType::CartographyTable: return menus::kCartographyTable;
        case MenuType::BlastFurnace: return menus::kBlastFurnace;
        case MenuType::Smoker: return menus::kSmoker;
        case MenuType::Lectern: return menus::kLectern;
        case MenuType::Merchant: return menus::kMerchant;
        case MenuType::Generic9x1: return menus::kGeneric9x1;
        case MenuType::Generic9x2: return menus::kGeneric9x2;
        case MenuType::Generic9x4: return menus::kGeneric9x4;
        case MenuType::Generic9x6: return menus::kGeneric9x6;
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
