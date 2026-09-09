#include "game/GameServer.hpp"
#include "game/MenuLogic.hpp"
#include "game/MenuInteraction.hpp"

#include <cstdio>
#include <string>

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
    menu.blockKey = posKey(4, 64, -2);
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
    check(io.dirtyCount == 1, "crafter marks its block entity as changed");

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
} // namespace

int main() {
    test_enchantment_menu();
    test_crafter_redstone();
    std::printf("MENU_LOGIC: %d PASS %d FAIL\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
