// MenuInteraction: vanilla click semantics for open menus. Pure logic —
// packet emission stays in the session layer so this is unit-testable.
#pragma once
#include "Containers.hpp"
#include "Recipes.hpp"

namespace cppfm {

class Player;

struct MenuIo {
    virtual ~MenuIo() = default;
    virtual void dropFromPlayer(Player& p, const ItemStack& stack, bool wholeStack) = 0;
    virtual void blockEntityChanged(std::int64_t key) = 0;
    virtual void itemCrafted(Player& p, const ItemStack& result) = 0;
    virtual void itemSmelted(Player& p, const ItemStack& result) = 0;
};

bool isTakeOnlySlot(const Menu& m, int slot);
bool isFuelItem(std::uint32_t itemId);
int furnaceFuelTicks(std::uint32_t itemId);
int maxStackFor(const ItemStack& s);             // 64 / 16 / 1 by item class
int maxStackForId(std::uint32_t itemId);         // id form (single truth for MenuLogic)

class ClickLogic {
public:
    // Applies one client click. Returns true when the menu contents changed
    static bool apply(Menu& m, Player& p, const RecipeManager& recipes,
                      int clickedSlot, int button, int mode,
                      ItemStack& cursor, MenuIo& io);

private:
    static bool pickupPlace(Menu& m, Player& p, int slot, int button,
                            ItemStack& cursor, MenuIo& io);
    static bool quickMove(Menu& m, Player& p, const RecipeManager& recipes,
                          int slot, ItemStack& cursor, MenuIo& io);
    static bool swapWithHotbar(Menu& m, Player& p, int slot, int button,
                               ItemStack& cursor, MenuIo& io);
    static bool throwSlot(Menu& m, Player& p, int slot, int button,
                          ItemStack& cursor, MenuIo& io);
    static bool pickupAll(Menu& m, Player& p, int slot, ItemStack& cursor,
                          MenuIo& io);

    static void craftTaken(Menu& m, const RecipeManager& recipes);
    static bool mergeInto(ItemStack& from, ItemStack& into, int amount = -1);
};

} // namespace cppfm
