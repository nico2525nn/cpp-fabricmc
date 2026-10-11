#include "game/GameServer.hpp"
#include "game/MenuLogic.hpp"
#include "game/MenuInteraction.hpp"
#include "game/RecipePlacement.hpp"

#include <array>
#include <cstdio>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace cppfm;

namespace cppfm {
// MenuLogic uses this notification to keep the session's XP bar authoritative.
// The focused test has no network session, so provide the narrow test seam.
void GameServer::sendSetExperience(Player&) {}
}

namespace {
int passed = 0;
int failed = 0;

void check(bool condition, const char* message) {
    if (condition) {
        ++passed;
        std::printf("PASS %s\n", message);
    } else {
        ++failed;
        std::printf("FAIL %s\n", message);
    }
}

std::uint32_t id(const char* name) {
    const auto it = gen::itemIdByName().find(name);
    return it == gen::itemIdByName().end() ? 0 : it->second;
}

ItemStack componentStack(std::uint32_t itemId, std::int16_t count,
                         std::uint8_t marker) {
    ItemStack stack = ItemStack::of(itemId, count);
    stack.components.emplace_back(900, std::vector<std::uint8_t>{marker});
    return stack;
}

struct TestIo final : MenuIo {
    int dirtyCount = 0;
    void dropFromPlayer(Player&, const ItemStack&, bool) override {}
    void blockEntityChanged(std::int64_t) override { ++dirtyCount; }
    void itemCrafted(Player&, const ItemStack&) override {}
    void itemSmelted(Player&, const ItemStack&) override {}
};

void test_enchantment_menu() {
    Menu menu;
    menu.type = MenuType::Enchantment;
    menu.container = menu.extraSlots;
    menu.containerCount = 2;
    menu.blockKey = posKey(0, 64, 0);
    menu.extraSlots[0] = ItemStack::of(id("minecraft:diamond_sword"));
    menu.extraSlots[1] = ItemStack::of(id("minecraft:lapis_lazuli"), 3);

    Player player;
    player.entityId = 17;
    player.gamemode = 0;
    player.xp.level = 30;
    player.enchantmentSeed = 0x12345678;
    EnchantmentMenuLogic logic;
    const auto choices = logic.offers(menu, player, 15);

    bool threeChoices = true;
    for (int i = 0; i < 3; ++i) {
        threeChoices = threeChoices && choices[i].selectable();
        check(choices[i].lapisCost == i + 1,
              "enchantment offer lapis cost is 1/2/3");
        check(choices[i].levelCost >= 1 && choices[i].levelCost <= 30,
              "enchantment offer level cost is in the protocol range");
        check(choices[i].enchantmentId ==
                  ItemStack::enchantIdByName(choices[i].enchantment) &&
                  choices[i].enchantmentId >= 0,
              "enchantment offer exposes the protocol enchantment id");
        check(choices[i].enchantmentLevel >= 1,
              "enchantment offer exposes the selected enchantment level");
        check(choices[i].result.itemId == menu.extraSlots[0].itemId,
              "enchantment offer preserves the input item");
    }
    check(threeChoices,
          "enchantment exposes three selectable choices for a sword");

    menu.extraSlots[0] = ItemStack::of(id("minecraft:book"));
    const auto bookChoices = logic.offers(menu, player, 15);
    check(bookChoices[0].selectable() &&
              bookChoices[0].result.itemId == id("minecraft:enchanted_book"),
          "enchantment converts a plain book into an enchanted book result");
    menu.extraSlots[0] = ItemStack::of(id("minecraft:stick"));
    check(!logic.offers(menu, player, 15)[0].selectable(),
          "enchantment does not invent offers for unsupported items");
    menu.extraSlots[0] = ItemStack::of(id("minecraft:diamond_sword"));

    const auto expected = choices[1];
    const int oldLevel = player.xp.level;
    const auto oldSeed = player.enchantmentSeed;
    TestIo io;
    check(logic.onEnchantButton(menu, player, 1, io, 15),
          "enchantment accepts a valid choice");
    check(menu.extraSlots[0].components == expected.result.components,
          "enchantment applies the presented choice, not a second roll");
    check(menu.extraSlots[1].count == 1,
          "enchantment consumes the selected lapis amount");
    check(player.xp.level == oldLevel - expected.levelCost,
          "enchantment consumes the selected level cost");
    check(player.enchantmentSeed != oldSeed,
          "enchantment advances the table seed after a successful choice");
    check(io.dirtyCount == 1, "enchantment marks its menu as changed");

    const ItemStack before = menu.extraSlots[0];
    const int lapisBefore = menu.extraSlots[1].count;
    check(!logic.onEnchantButton(menu, player, 3, io, 15),
          "enchantment rejects an out-of-range button");
    check(menu.extraSlots[0].components == before.components &&
              menu.extraSlots[1].count == lapisBefore,
          "invalid enchantment button has no side effects");

    menu.extraSlots[1] = ItemStack::air();
    check(!logic.onEnchantButton(menu, player, 0, io, 15),
          "enchantment rejects a missing lapis input");

    Menu slotMenu;
    slotMenu.type = MenuType::Enchantment;
    slotMenu.container = slotMenu.extraSlots;
    slotMenu.containerCount = 2;
    Player slotPlayer;
    ItemStack cursor = ItemStack::of(id("minecraft:stick"), 2);
    RecipeManager recipes;
    check(!logic.onSlotClick(slotMenu, slotPlayer, 0, 0, 0, cursor, io,
                             recipes) && cursor.count == 2,
          "enchantment rejects an unsupported item in the item slot");
    cursor = ItemStack::of(id("minecraft:diamond_sword"), 3);
    check(logic.onSlotClick(slotMenu, slotPlayer, 0, 0, 0, cursor, io,
                            recipes) && slotMenu.extraSlots[0].count == 1 &&
              cursor.count == 2,
          "enchantment item slot accepts only one item");
    cursor = ItemStack::of(id("minecraft:stone"));
    check(!logic.onSlotClick(slotMenu, slotPlayer, 1, 0, 0, cursor, io,
                             recipes) && cursor.itemId == id("minecraft:stone"),
          "enchantment rejects a non-lapis item in the lapis slot");
    cursor = ItemStack::of(id("minecraft:lapis_lazuli"), 4);
    check(logic.onSlotClick(slotMenu, slotPlayer, 1, 0, 0, cursor, io,
                            recipes) && slotMenu.extraSlots[1].count == 4 &&
              cursor.empty(),
          "enchantment accepts lapis in the lapis slot");
}

void test_crafter_redstone() {
    RecipeManager recipes;
    recipes.loadDefaults();
    recipes.loadDirectory("assets/data/recipes");

    Menu menu;
    menu.type = MenuType::Crafter;
    menu.container = menu.extraSlots;
    menu.containerCount = 9;
    menu.blockKey = posKey(-1, -1, -1);
    menu.extraSlots[0] = ItemStack::of(id("minecraft:oak_log"));

    CrafterMenuLogic logic;
    TestIo io;
    ItemStack output = ItemStack::air();
    check(logic.craftOnRedstone(menu, recipes, output, io),
          "crafter performs one redstone craft for a matching recipe");
    check(output.itemId == id("minecraft:oak_planks") && output.count == 4,
          "crafter transfers the complete recipe output");
    check(menu.extraSlots[0].empty(),
          "crafter consumes one item from each occupied slot");
    check(menu.blockKey == -1 && io.dirtyCount == 1,
          "a valid packed block position equal to the former sentinel is still marked dirty");

    menu.extraSlots[0] = ItemStack::of(id("minecraft:oak_log"), 2);
    ItemStack blockedOutput = ItemStack::of(id("minecraft:oak_planks"), 61);
    check(!logic.craftOnRedstone(menu, recipes, blockedOutput, io),
          "crafter refuses a result transfer that cannot fit");
    check(menu.extraSlots[0].count == 2 && blockedOutput.count == 61,
          "failed crafter transfer does not consume inputs or mutate output");

    ItemStack mergeOutput = ItemStack::of(id("minecraft:oak_planks"), 60);
    check(logic.craftOnRedstone(menu, recipes, mergeOutput, io),
          "crafter merges a complete output into a compatible sink");
    check(mergeOutput.count == 64 && menu.extraSlots[0].count == 1,
          "crafter merge is atomic and leaves the expected input remainder");

    Menu invalid;
    invalid.type = MenuType::Chest;
    invalid.container = invalid.extraSlots;
    invalid.extraSlots[0] = ItemStack::of(id("minecraft:oak_log"));
    ItemStack untouched = ItemStack::air();
    check(!logic.craftOnRedstone(invalid, recipes, untouched, io),
          "crafter redstone API rejects a non-crafter menu");
    check(invalid.extraSlots[0].count == 1 && untouched.empty(),
          "non-crafter rejection has no side effects");
}

void test_right_click_split_preserves_item_data() {
    auto itemNbt = nbt::Value::makeCompound();
    itemNbt.set("id", nbt::Value::makeString("minecraft:diamond"));
    itemNbt.set("count", nbt::Value::makeInt(2));
    auto components = nbt::Value::makeCompound();
    components.set("example:opaque_component",
                   nbt::Value::makeString("keep through split"));
    itemNbt.set("components", std::move(components));
    const auto loaded = ItemStack::fromNbt(itemNbt);

    Menu menu;
    menu.type = MenuType::Brewing;
    menu.container = menu.extraSlots;
    menu.containerCount = 5;
    Player player;
    ItemStack cursor = loaded.value_or(ItemStack::air());
    BrewingMenuLogic logic;
    TestIo io;
    RecipeManager recipes;
    const bool changed = logic.onSlotClick(menu, player, 0, 1, 0, cursor,
                                           io, recipes);
    const auto& placed = menu.extraSlots[0];
    const auto saved = placed.empty() ? nbt::Value::makeCompound()
                                      : placed.toNbt(0);
    const auto* savedComponents = saved.get("components");
    const auto* savedOpaque = savedComponents
        ? savedComponents->get("example:opaque_component") : nullptr;
    check(loaded && changed && cursor.count == 1 && placed.count == 1 &&
              placed.sameItemData(cursor) && placed.persistedNbt &&
              savedOpaque && savedOpaque->tag == nbt::String &&
              savedOpaque->str == "keep through split",
          "right-click splitting copies known and opaque item data to both stacks");
}

void test_component_aware_menu_merging() {
    RecipeManager recipes;
    TestIo io;
    Player player;
    Menu menu;
    menu.type = MenuType::Chest;
    menu.container = menu.extraSlots;
    menu.containerCount = 2;

    ItemStack cursor = componentStack(id("minecraft:apple"), 2, 0x2a);
    menu.extraSlots[0] = componentStack(id("minecraft:apple"), 3, 0x2a);
    check(ClickLogic::apply(menu, player, recipes, 0, 0, 0, cursor, io) &&
              cursor.empty() && menu.extraSlots[0].count == 5 &&
              menu.extraSlots[0].components ==
                  std::vector<std::pair<std::uint32_t,
                                        std::vector<std::uint8_t>>>{
                      {900, {0x2a}}},
          "pickup merges stacks with identical non-empty components");

    cursor = componentStack(id("minecraft:apple"), 2, 0x2a);
    menu.extraSlots[1] = componentStack(id("minecraft:apple"), 4, 0x2b);
    check(ClickLogic::apply(menu, player, recipes, 1, 0, 0, cursor, io) &&
              menu.extraSlots[1].sameItemData(
                  componentStack(id("minecraft:apple"), 2, 0x2a)) &&
              cursor.sameItemData(
                  componentStack(id("minecraft:apple"), 4, 0x2b)),
          "pickup preserves rather than merges stacks with different components");

    Menu crafting;
    crafting.type = MenuType::Crafting;
    crafting.craftResult = componentStack(id("minecraft:apple"), 2, 0x2a);
    Player craftingPlayer;
    craftingPlayer.inv[9] = componentStack(id("minecraft:apple"), 63, 0x2b);
    ItemStack emptyCursor = ItemStack::air();
    check(ClickLogic::apply(crafting, craftingPlayer, recipes, 0, 0, 1,
                            emptyCursor, io) &&
              craftingPlayer.inv[9].count == 63 &&
              craftingPlayer.inv[9].components[0].second[0] == 0x2b &&
              craftingPlayer.inv[10].count == 2 &&
              craftingPlayer.inv[10].components[0].second[0] == 0x2a,
          "crafted stacks with different components occupy a separate inventory slot");
}

void test_quick_move_distributes_remainder() {
    Menu menu;
    menu.type = MenuType::Chest;
    menu.container = menu.extraSlots;
    menu.containerCount = 4;
    Player player;
    player.inv[9] = componentStack(id("minecraft:apple"), 8, 0x31);
    menu.extraSlots[0] = componentStack(id("minecraft:apple"), 62, 0x31);
    menu.extraSlots[1] = componentStack(id("minecraft:apple"), 63, 0x31);
    menu.extraSlots[2] = componentStack(id("minecraft:apple"), 17, 0x32);
    RecipeManager recipes;
    TestIo io;
    ItemStack cursor = ItemStack::air();

    check(ClickLogic::apply(menu, player, recipes, 27, 0, 1, cursor, io) &&
              player.inv[9].empty() && menu.extraSlots[0].count == 64 &&
              menu.extraSlots[1].count == 64 &&
              menu.extraSlots[2].count == 17 &&
              menu.extraSlots[2].components[0].second[0] == 0x32 &&
              menu.extraSlots[3].count == 5 &&
              menu.extraSlots[3].components[0].second[0] == 0x31,
          "quickMove fills multiple partial matches before an empty slot");
}

void test_pickup_all_visible_slots_only() {
    RecipeManager recipes;
    TestIo io;
    Player player;
    Menu crafting;
    crafting.type = MenuType::Crafting;
    crafting.craftGrid[0] = componentStack(id("minecraft:apple"), 1, 0x41);
    crafting.craftGrid[8] = componentStack(id("minecraft:apple"), 4, 0x42);
    crafting.craftResult = componentStack(id("minecraft:apple"), 5, 0x41);
    crafting.extraSlots[0] = componentStack(id("minecraft:apple"), 6, 0x41);
    player.inv[9] = componentStack(id("minecraft:apple"), 2, 0x41);
    player.inv[8] = componentStack(id("minecraft:apple"), 7, 0x41);
    player.inv[45] = componentStack(id("minecraft:apple"), 8, 0x41);
    ItemStack cursor = componentStack(id("minecraft:apple"), 61, 0x41);

    check(ClickLogic::apply(crafting, player, recipes, 1, 0, 6, cursor, io) &&
              cursor.count == 64 && crafting.craftGrid[0].empty() &&
              crafting.craftGrid[8].count == 4 &&
              crafting.craftResult.empty() &&
              crafting.extraSlots[0].count == 6 && player.inv[9].empty() &&
              player.inv[8].count == 7 && player.inv[45].count == 8,
          "pickupAll gathers craft-grid and visible player items only");

    Menu inventory;
    inventory.type = MenuType::Crafting;
    inventory.playerInventory = true;
    inventory.craftGrid[0] = componentStack(id("minecraft:apple"), 1, 0x51);
    inventory.craftGrid[4] = componentStack(id("minecraft:apple"), 1, 0x51);
    inventory.craftGrid[8] = componentStack(id("minecraft:apple"), 4, 0x51);
    inventory.craftResult = componentStack(id("minecraft:apple"), 5, 0x51);
    player.inv[5] = componentStack(id("minecraft:apple"), 1, 0x51);
    player.inv[45] = componentStack(id("minecraft:apple"), 2, 0x51);
    player.inv[4] = componentStack(id("minecraft:apple"), 6, 0x51);
    cursor = componentStack(id("minecraft:apple"), 59, 0x51);

    check(ClickLogic::apply(inventory, player, recipes, 1, 0, 6, cursor, io) &&
              cursor.count == 64 && inventory.craftGrid[0].empty() &&
              inventory.craftGrid[4].empty() &&
              inventory.craftGrid[8].count == 4 &&
              inventory.craftResult.empty() && player.inv[5].empty() &&
              player.inv[45].empty() && player.inv[4].count == 6,
          "player-inventory pickupAll uses visible grid/inventory mappings");
}

void test_anvil_rejects_full_cursor_without_charging_xp() {
    Menu menu;
    menu.type = MenuType::Anvil;
    menu.container = menu.extraSlots;
    menu.containerCount = 3;
    menu.extraSlots[0] = ItemStack::of(id("minecraft:apple"));
    menu.anvilRename = "renamed apple";

    Player player;
    player.xp.level = 3;
    ItemStack cursor = ItemStack::of(id("minecraft:apple"), 64);
    cursor.setCustomName("renamed apple");
    RecipeManager recipes;
    TestIo io;
    AnvilMenuLogic logic;
    logic.onContentChanged(menu, player);
    const ItemStack resultBefore = menu.extraSlots[2];

    const bool changed = logic.onSlotClick(menu, player, 2, 0, 0, cursor,
                                           io, recipes);
    check(!changed && player.xp.level == 3 &&
              menu.extraSlots[0].count == 1 &&
              menu.extraSlots[2].sameItemData(resultBefore) &&
              menu.extraSlots[2].count == resultBefore.count &&
              cursor.count == 64 && io.dirtyCount == 0,
          "anvil rejects a full result cursor without charging XP or consuming inputs");
}

void test_anvil_respects_non_stackable_input_limit() {
    Menu menu;
    menu.type = MenuType::Anvil;
    menu.container = menu.extraSlots;
    menu.containerCount = 3;
    menu.extraSlots[0] = ItemStack::of(id("minecraft:diamond_sword"));
    Player player;
    RecipeManager recipes;
    TestIo io;
    AnvilMenuLogic logic;

    for (const int button : {0, 1}) {
        ItemStack cursor = ItemStack::of(id("minecraft:diamond_sword"));
        const auto inputBefore = menu.extraSlots[0];
        check(!logic.onSlotClick(menu, player, 0, button, 0, cursor,
                                 io, recipes) &&
                  menu.extraSlots[0].sameItemData(inputBefore) &&
                  menu.extraSlots[0].count == 1 && cursor.count == 1,
              button == 0
                  ? "anvil left-click does not merge non-stackable inputs"
                  : "anvil right-click does not merge non-stackable inputs");
    }
}

void test_recipe_placement_make_all_and_alternatives() {
    const auto oak = id("minecraft:oak_planks");
    const auto birch = id("minecraft:birch_planks");
    Recipe recipe;
    recipe.kind = Recipe::Kind::Shaped;
    recipe.width = 2;
    recipe.height = 1;
    Ingredient flexible;
    flexible.items = {oak, birch};
    Ingredient oakOnly;
    oakOnly.items = {oak};
    recipe.cells = {flexible, oakOnly};

    std::array<ItemStack, kPlayerInventorySlotCount> inventory{};
    for (auto& stack : inventory) stack = ItemStack::air();
    inventory[9] = ItemStack::of(oak);
    inventory[10] = ItemStack::of(birch);
    std::array<ItemStack, 9> grid{};
    for (auto& stack : grid) stack = ItemStack::air();
    const auto placement = planCraftingRecipePlacement(
        recipe, false, false, inventory, grid);
    check(placement && placement->batches == 1 &&
              placement->grid[0].itemId == birch &&
              placement->grid[1].itemId == oak,
          "recipe ingredient alternatives are matched without starving a constrained cell");

    inventory[9] = ItemStack::of(oak, 2);
    inventory[10] = ItemStack::of(birch, 7);
    const auto alternativeMakeAll = planCraftingRecipePlacement(
        recipe, false, true, inventory, grid);
    check(alternativeMakeAll && alternativeMakeAll->batches == 2 &&
              alternativeMakeAll->grid[0].itemId == birch &&
              alternativeMakeAll->grid[0].count == 2 &&
              alternativeMakeAll->grid[1].itemId == oak &&
              alternativeMakeAll->grid[1].count == 2 &&
              alternativeMakeAll->inventory[10].count == 5,
          "makeAll finds the maximum feasible batch count across overlapping ingredient alternatives");

    recipe.cells = {oakOnly, oakOnly};
    inventory[9] = componentStack(oak, 4, 0x71);
    inventory[10] = componentStack(oak, 6, 0x71);
    const auto allPlacement = planCraftingRecipePlacement(
        recipe, false, true, inventory, grid);
    check(allPlacement && allPlacement->batches == 5 &&
              allPlacement->grid[0].count == 5 &&
              allPlacement->grid[1].count == 5 &&
              allPlacement->grid[0].sameItemData(allPlacement->grid[1]) &&
              allPlacement->inventory[9].empty() &&
              allPlacement->inventory[10].empty(),
          "makeAll fills each recipe cell to the maximum complete batch count and preserves components");
    const auto onePlacement = planCraftingRecipePlacement(
        recipe, false, false, inventory, grid);
    check(onePlacement && onePlacement->batches == 1 &&
              onePlacement->grid[0].count == 1 &&
              onePlacement->grid[1].count == 1 &&
              onePlacement->inventory[9].count == 2 &&
              onePlacement->inventory[10].count == 6,
          "single recipe-book placement remains exactly one batch");
}

void test_player_inventory_recipe_grid_mapping() {
    Recipe recipe;
    recipe.kind = Recipe::Kind::Shapeless;
    for (const char* item : {"minecraft:oak_planks", "minecraft:birch_planks",
                             "minecraft:stone", "minecraft:apple"}) {
        Ingredient ingredient;
        ingredient.items.insert(id(item));
        recipe.ingredients.push_back(std::move(ingredient));
    }
    std::array<ItemStack, kPlayerInventorySlotCount> inventory{};
    for (auto& stack : inventory) stack = ItemStack::air();
    for (std::size_t i = 0; i < recipe.ingredients.size(); ++i)
        inventory[9 + i] = ItemStack::of(
            *recipe.ingredients[i].items.begin());
    std::array<ItemStack, 9> grid{};
    for (auto& stack : grid) stack = ItemStack::air();

    const auto placement = planCraftingRecipePlacement(
        recipe, true, false, inventory, grid);
    check(placement && placement->grid[0].itemId == id("minecraft:oak_planks") &&
              placement->grid[1].itemId == id("minecraft:birch_planks") &&
              placement->grid[3].itemId == id("minecraft:stone") &&
              placement->grid[4].itemId == id("minecraft:apple") &&
              placement->grid[2].empty() && placement->grid[5].empty(),
          "player inventory recipe-book placement maps shapeless ingredients to the 2x2 grid");

    Recipe tooLarge;
    tooLarge.kind = Recipe::Kind::Shaped;
    tooLarge.width = 3;
    tooLarge.height = 1;
    tooLarge.cells.resize(3);
    check(!planCraftingRecipePlacement(tooLarge, true, false,
                                       inventory, grid),
          "player inventory recipe-book rejects recipes wider than its 2x2 grid");
}
} // namespace

int main() {
    test_enchantment_menu();
    test_crafter_redstone();
    test_right_click_split_preserves_item_data();
    test_component_aware_menu_merging();
    test_quick_move_distributes_remainder();
    test_pickup_all_visible_slots_only();
    test_anvil_rejects_full_cursor_without_charging_xp();
    test_anvil_respects_non_stackable_input_limit();
    test_recipe_placement_make_all_and_alternatives();
    test_player_inventory_recipe_grid_mapping();
    std::printf("MENU_LOGIC: %d PASS %d FAIL\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
