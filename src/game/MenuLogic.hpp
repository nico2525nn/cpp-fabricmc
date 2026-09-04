// MenuLogic — plan7 entity section Object-oriented GUI processing: per-menu-type logic for inventory containers. Each MenuLogic handles
// slot clicks for its container type (Anvil, Enchantment, Brewing, etc.) Replaces giant switch in MenuInteraction with polymorphic
// dispatch.
#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include "Containers.hpp"
#include "Items.hpp"

namespace cppfm {

class Player;
struct MenuIo;
class RecipeManager;

// Base interface for all menu logics
class MenuLogic {
public:
    virtual ~MenuLogic() = default;
    // Handle a slot click for this menu. Returns true if the menu state changed.
    // slotId is protocol slot index (0..totalSlots()-1). cursor is the player's CursorItem.
    virtual bool onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                             ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) = 0;
    // Called when container contents changed (for recalculating result slots)
    virtual void onContentChanged(Menu& menu, Player& player) { (void)menu; (void)player; }
    virtual const char* name() const = 0;
};

// Anvil: repair + rename logic via CostCalculator::anvilCost
class AnvilMenuLogic final : public MenuLogic {
public:
    bool onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                     ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) override;
    void onContentChanged(Menu& menu, Player& player) override;
    const char* name() const override { return "Anvil"; }
private:
    void recomputeResult(Menu& menu);
    std::string pendingRename_;
public:
    void setRenameText(const std::string& t) { pendingRename_ = t; }
    void setRenameForMenu(Menu& menu, const std::string& t) { menu.anvilRename = t; }
};

// Enchantment table: lapis + item -> enchant options, EnchantItem packet handling
class EnchantmentMenuLogic final : public MenuLogic {
public:
    bool onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                     ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) override;
    void onContentChanged(Menu& menu, Player& player) override;
    const char* name() const override { return "Enchantment"; }
    // Handle EnchantItem (0x0F) packet directly
    bool onEnchantButton(Menu& menu, Player& player, int buttonId, MenuIo& io);
    bool onEnchantButton(Menu& menu, Player& player, int buttonId, MenuIo& io, int bookshelves);
};

// Brewing stand: potion brewing logic (plan6)
class BrewingMenuLogic final : public MenuLogic {
public:
    bool onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                     ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) override;
    const char* name() const override { return "Brewing"; }
};

// Stonecutter: ghost recipe selection
class StonecutterMenuLogic final : public MenuLogic {
public:
    bool onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                     ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) override;
    const char* name() const override { return "Stonecutter"; }
};

// Crafter: 3x3 crafting grid stub (1.21 crafter block) — disabled slots handled as normal container
class CrafterMenuLogic final : public MenuLogic {
public:
    bool onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                     ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) override;
    const char* name() const override { return "Crafter"; }
};

// Cartography Table: map cloning / extension stub (3 slots: map, paper, result)
class CartographyMenuLogic final : public MenuLogic {
public:
    bool onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                     ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) override;
    void onContentChanged(Menu& menu, Player& player) override;
    const char* name() const override { return "CartographyTable"; }
private:
    void recomputeResult(Menu& menu);
};

// Grindstone, Smithing, Beacon, Loom etc. share generic logic
class GenericMenuLogic final : public MenuLogic {
public:
    explicit GenericMenuLogic(const char* n) : n_(n) {}
    bool onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                     ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) override;
    const char* name() const override { return n_; }
private: const char* n_;
};

// Factory / dispatcher
std::unique_ptr<MenuLogic> createMenuLogic(MenuType type);
MenuLogic* getMenuLogic(MenuType type); // singleton per type (thread-safe lazy)

} // namespace cppfm
