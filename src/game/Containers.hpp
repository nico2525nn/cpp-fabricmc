#pragma once
#include <array>
#include <cstdint>
#include <memory>
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

// 36-element literal repeated 4x in Commands.cpp give/clear).
inline constexpr std::array<int, 36> kMainInventoryOrder = {
    36, 37, 38, 39, 40, 41, 42, 43, 44,
    9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35};

// 25 vanilla MenuType entries (1.21.4 protocol 769) + Barrel alias for block-entity distinction (same wire id as Generic9x3)
// Count must be 25 to match Yarn `ScreenHandlerType` / `MenuRegistry` (1.21.4) : 0 generic_9x1 .. 24 stonecutter.
// Strict audit HIGH I1/I9/I10 require Crafter (7) / Cartography (23) / BlastFurnace (10) / Smoker (22) at correct registry index.
// Wire order (Yarn `ScreenHandlerType`): generic_9x1=0, generic_9x2=1, generic_9x3=2, generic_9x4=3, generic_9x5=4, generic_9x6=5,
// Barrel uses generic_9x3 wire id for parity (same as Chest) but distinct block-entity kind.
enum class MenuType {
    Chest, Furnace, Crafting, Hopper, Dispenser, Barrel, ShulkerBox, Enchantment, Anvil, Brewing, Stonecutter, Grindstone, Smithing, Beacon, Loom,
    Crafter, CartographyTable, BlastFurnace, Smoker, Lectern, Merchant,
    Generic9x1, Generic9x2, Generic9x4, Generic9x6
};
static_assert(static_cast<int>(MenuType::Generic9x6) == 24, "MenuType must be 25 entries (0..24)");

struct MenuLayout {
    int containerSlots;
    int screenTypeId;
};

inline constexpr std::array<MenuLayout, 25> kMenuLayouts = {{
    {27, menus::kGeneric9x3}, {3, menus::kFurnace}, {10, menus::kCrafting},
    {5, menus::kHopper}, {9, menus::kGeneric3x3}, {27, menus::kGeneric9x3},
    {27, menus::kShulkerBox}, {2, menus::kEnchantment}, {3, menus::kAnvil},
    {5, menus::kBrewingStand}, {2, menus::kStonecutter}, {3, menus::kGrindstone},
    {4, menus::kSmithing}, {1, menus::kBeacon}, {4, menus::kLoom},
    {9, menus::kCrafter}, {3, menus::kCartographyTable}, {3, menus::kBlastFurnace},
    {3, menus::kSmoker}, {1, menus::kLectern}, {3, menus::kMerchant},
    {9, menus::kGeneric9x1}, {18, menus::kGeneric9x2}, {36, menus::kGeneric9x4},
    {54, menus::kGeneric9x6}}};

inline constexpr MenuLayout menuLayoutFor(MenuType type) {
    const auto index = static_cast<std::size_t>(type);
    return index < kMenuLayouts.size() ? kMenuLayouts[index]
                                       : MenuLayout{27, menus::kGeneric9x3};
}

class RecipeManager;

class Menu {
public:
    MenuType type = MenuType::Chest;
    std::int32_t windowId = 0;
    std::int64_t blockKey = -1;                  // packed pos, -1 for crafting

    // backing stores ---------------------------------------------------------
    ItemStack* container = nullptr;              // chest slots (27) | furnace(3) etc.
    int containerCount = 0;
    // Keep the block entity alive for the entire lifetime of the menu.  The
    // store may unload/replace its map entry while a session is still
    // finishing a click or a packet refresh; the raw slot pointers below are
    // views into this owner.
    std::shared_ptr<BlockEntity> blockEntityOwner;
    // Crafter-only slot state.  A null pointer means that the menu has no
    // disabled-slot mask (all other container types).
    std::uint16_t* crafterDisabledSlots = nullptr;
    ItemStack craftGrid[9];                      // crafting table only
    ItemStack craftResult;                       // cached result
    ItemStack extraSlots[27];                    // generic storage for menus without BE

    // The player's own inventory screen is a 46-slot screen with a different
    // layout from an opened crafting table: slot 0 is the result, slots 1..4
    // are the 2x2 input, slots 5..8 are armor, slot 45 is the off-hand, and
    // slots 9..44 are the main inventory/hotbar.  Keep this as an explicit
    // adapter mode so normal menus retain their container+36 layout.
    bool playerInventory = false;

    // transient view of the owning player's inventory is external (Player.inv) drag paint transient (mode 5)
    std::vector<int> dragSlots;
    int dragButton = -1;                 // initial button for drag type
    // anvil rename text (per-menu, not singleton)
    std::string anvilRename;
    int totalSlots() const {
        return menuLayoutFor(type).containerSlots + 36;
    }
    int openScreenTypeId() const {
        return menuLayoutFor(type).screenTypeId;
    }

    // Map a protocol slot number to a mutable stack pointer (nullptr if none).
    ItemStack* slotAt(int slot, ItemStack* playerInv /*46*/);
    int craftGridIndex(int slot) const {
        if (playerInventory) {
            static constexpr int kInventoryCraftGrid[4] = {0, 1, 3, 4};
            return slot >= 1 && slot <= 4 ? kInventoryCraftGrid[slot - 1] : -1;
        }
        return type == MenuType::Crafting && slot >= 1 && slot <= 9
                   ? slot - 1 : -1;
    }

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
