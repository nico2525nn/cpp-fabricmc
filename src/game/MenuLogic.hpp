// slot clicks for its container type (Anvil, Enchantment, Brewing, etc.) Replaces giant switch in MenuInteraction with polymorphic
// dispatch.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <memory>
#include "Containers.hpp"
#include "Items.hpp"

namespace cppfm {

class Player;
struct MenuIo;
class RecipeManager;

// The three client-visible choices in an enchanting table. The result is
// built from the current input each time offers() is called; this avoids
// storing menu state in the per-type MenuLogic singleton.
struct EnchantmentOffer {
    int levelCost = 0;
    int lapisCost = 0;
    int enchantmentId = -1;
    int enchantmentLevel = -1;
    std::string enchantment;
    ItemStack result = ItemStack::air();

    bool selectable() const {
        return levelCost > 0 && !enchantment.empty() && !result.empty();
    }
};

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
public:
    // Rename text belongs to a Menu instance.  MenuLogic objects are shared
    // by all sessions, so retaining it here would leak one player's rename
    // into another player's anvil and would require cross-session locking.
    void setRenameText(const std::string&) {}
    void setRenameForMenu(Menu& menu, const std::string& t) { menu.anvilRename = t; }
};

// Enchantment table: lapis + item -> enchant options, EnchantItem packet handling
class EnchantmentMenuLogic final : public MenuLogic {
public:
    bool onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                     ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) override;
    void onContentChanged(Menu& menu, Player& player) override;
    const char* name() const override { return "Enchantment"; }
    // Rebuild the three choices from the current item, player seed and shelf
    // count. The existing model has no enchantment registry/enchantability
    // table, so unsupported item-specific vanilla rules remain explicit in
    // the returned choices rather than being silently guessed at click time.
    std::array<EnchantmentOffer, 3> offers(const Menu& menu, const Player& player,
                                           int bookshelves) const;
    // Handle EnchantItem (0x0F) packet directly
    bool onEnchantButton(Menu& menu, Player& player, int buttonId, MenuIo& io);
    bool onEnchantButton(Menu& menu, Player& player, int buttonId, MenuIo& io, int bookshelves);
};

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

// Crafter: 3x3 interaction and the atomic recipe step used by the server tick.
class CrafterMenuLogic final : public MenuLogic {
public:
    bool onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                     ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) override;
    const char* name() const override { return "Crafter"; }

    // Execute one redstone craft and transfer the complete result into
    // outputSink. The operation is all-or-nothing when the sink cannot hold
    // the result, so a failed transfer never consumes the crafter inputs.
    // The server tick owns disabled-slot state, adjacent-inventory routing,
    // and world item ejection around this atomic recipe operation.
    bool craftOnRedstone(Menu& menu, const RecipeManager& recipes,
                         ItemStack& outputSink, MenuIo& io) const;
};

// Cartography Table: partial map cloning/extension interaction (3 slots: map, paper, result).
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
