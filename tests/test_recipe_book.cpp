#include "game/RecipeBook.hpp"

#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using cppfm::Ingredient;
using cppfm::Recipe;

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

void check(bool condition, const char* message) {
    if (!condition) fail(message);
}

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    std::uint32_t varint() {
        std::uint32_t result = 0;
        unsigned shift = 0;
        for (;;) {
            if (offset_ >= bytes_.size() || shift >= 35)
                fail("truncated or overlong varint");
            const auto byte = bytes_[offset_++];
            result |= static_cast<std::uint32_t>(byte & 0x7fU) << shift;
            if ((byte & 0x80U) == 0) return result;
            shift += 7;
        }
    }

    bool boolean() {
        if (offset_ >= bytes_.size()) fail("truncated boolean");
        return bytes_[offset_++] != 0;
    }

    std::uint8_t u8() {
        if (offset_ >= bytes_.size()) fail("truncated byte");
        return bytes_[offset_++];
    }

    float f32() {
        std::uint32_t bits = 0;
        for (int i = 0; i < 4; ++i)
            bits = (bits << 8) | u8();
        return std::bit_cast<float>(bits);
    }

    std::size_t remaining() const { return bytes_.size() - offset_; }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0;
};

std::uint32_t item(std::string_view name) {
    return cppfm::recipe_book::itemId(name);
}

void expectItemDisplay(Reader& reader, std::uint32_t expected) {
    check(reader.varint() == 2, "expected SlotDisplay.Item");
    check(reader.varint() == expected, "item display id mismatch");
}

void expectStackDisplay(Reader& reader, const Recipe& recipe) {
    check(reader.varint() == 3, "expected SlotDisplay.ItemStack");
    check(reader.varint() == static_cast<std::uint32_t>(recipe.result.count),
          "result count mismatch");
    check(reader.varint() == recipe.result.itemId, "result item mismatch");
    check(reader.varint() == 0, "unexpected result component count");
    check(reader.varint() == 0, "unexpected removed component count");
}

void expectStackDisplay(Reader& reader, const cppfm::ItemStack& result) {
    check(reader.varint() == 3, "expected SlotDisplay.ItemStack");
    check(reader.varint() == static_cast<std::uint32_t>(result.count),
          "result count mismatch");
    check(reader.varint() == result.itemId, "result item mismatch");
    check(reader.varint() == 0, "unexpected result component count");
    check(reader.varint() == 0, "unexpected removed component count");
}

void expectIngredient(Reader& reader, const Ingredient& ingredient) {
    if (ingredient.items.empty()) {
        check(reader.varint() == 0, "expected SlotDisplay.Empty");
        return;
    }
    if (ingredient.items.size() == 1) {
        expectItemDisplay(reader, *ingredient.items.begin());
        return;
    }
    check(reader.varint() == 7, "expected SlotDisplay.Composite");
    check(reader.varint() == ingredient.items.size(),
          "composite ingredient count mismatch");
    for (std::size_t i = 0; i < ingredient.items.size(); ++i) {
        check(reader.varint() == 2,
              "composite ingredient child is not an item display");
        (void)reader.varint();
    }
}

std::vector<Recipe> fixtures() {
    std::vector<Recipe> recipes;

    Recipe shaped;
    shaped.kind = Recipe::Kind::Shaped;
    shaped.width = 2;
    shaped.height = 1;
    Ingredient planks;
    planks.items.insert(item("minecraft:oak_planks"));
    planks.items.insert(item("minecraft:birch_planks"));
    shaped.cells = {planks, Ingredient{}};
    shaped.result = cppfm::ItemStack::of(item("minecraft:stick"), 4);
    shaped.category = 0;
    shaped.group = "wooden_sticks";
    recipes.push_back(shaped);

    Recipe special;
    special.kind = Recipe::Kind::Special;
    special.result = cppfm::ItemStack::of(item("minecraft:firework_rocket"));
    recipes.push_back(special);

    Recipe furnace;
    furnace.kind = Recipe::Kind::Smelting;
    Ingredient stone;
    stone.items.insert(item("minecraft:stone"));
    furnace.cells.push_back(stone);
    furnace.result = cppfm::ItemStack::of(item("minecraft:cooked_beef"));
    furnace.cookingStation = Recipe::CookingStation::Smoker;
    furnace.cookingTicks = 100;
    furnace.experience = 0.25f;
    furnace.category = 9;
    recipes.push_back(furnace);

    Recipe stonecutting;
    stonecutting.kind = Recipe::Kind::Stonecutting;
    stonecutting.cells.push_back(stone);
    stonecutting.result = cppfm::ItemStack::of(
        item("minecraft:stone_bricks"), 4);
    stonecutting.category = 10;
    recipes.push_back(stonecutting);

    Recipe smithing;
    smithing.kind = Recipe::Kind::Smithing;
    smithing.cells = {stone, stone, stone};
    smithing.result = cppfm::ItemStack::of(item("minecraft:netherite_sword"));
    smithing.category = 11;
    recipes.push_back(smithing);
    return recipes;
}

void testNetworkIdMapping() {
    const auto recipes = fixtures();
    const auto entries = cppfm::recipe_book::buildNetworkEntries(recipes);
    check(entries.size() == 4, "special recipe must not have a display ID");
    check(entries[0].displayId == 0 && entries[0].recipeIndex == 0,
          "first recipe mapping mismatch");
    check(entries[1].displayId == 1 && entries[1].recipeIndex == 2,
          "special recipe must not create a gap in network IDs");
    check(cppfm::recipe_book::recipeIndexForDisplayId(recipes, 2) == 3,
          "stonecutter display ID must map to its source recipe");
    check(cppfm::recipe_book::displayIdForRecipeIndex(recipes, 3) == 2,
          "source stonecutter recipe must map to its display ID");
    check(!cppfm::recipe_book::displayIdForRecipeIndex(recipes, 1),
          "special recipe must have no recipe-book ID");
}

void testPacketDisplaysAndStations() {
    const auto recipes = fixtures();
    const auto packet = cppfm::recipe_book::makeAddPacketBody(
        recipes, nullptr, true);
    Reader reader(packet.data);
    check(reader.varint() == 4, "recipe-book entry count mismatch");

    check(reader.varint() == 0, "shaped display ID mismatch");
    check(reader.varint() == 1, "expected shaped display");
    check(reader.varint() == 2 && reader.varint() == 1,
          "shaped dimensions mismatch");
    check(reader.varint() == 2, "shaped cell count mismatch");
    expectIngredient(reader, recipes[0].cells[0]);
    expectIngredient(reader, recipes[0].cells[1]);
    expectStackDisplay(reader, recipes[0]);
    expectItemDisplay(reader, item("minecraft:crafting_table"));
    check(reader.boolean(), "named recipe group was lost");
    check(reader.varint() == 0, "group id mismatch");
    check(reader.varint() == 0, "crafting building-block category mismatch");
    check(!reader.boolean(), "unexpected crafting requirements");
    check(reader.u8() == 3, "recipe-book add flags mismatch");

    check(reader.varint() == 1, "furnace display ID mismatch");
    check(reader.varint() == 2, "expected furnace display");
    expectIngredient(reader, recipes[2].cells[0]);
    check(reader.varint() == 1, "furnace fuel must be AnyFuel");
    expectStackDisplay(reader, recipes[2]);
    expectItemDisplay(reader, item("minecraft:smoker"));
    check(reader.varint() == 100, "cooking duration mismatch");
    check(reader.f32() == 0.25f, "cooking experience mismatch");
    check(!reader.boolean(), "ungrouped recipe encoded as grouped");
    check(reader.varint() == 9, "smoker category mismatch");
    check(!reader.boolean(), "unexpected crafting requirements");
    check(reader.u8() == 3, "recipe-book add flags mismatch");

    check(reader.varint() == 2, "stonecutter display ID mismatch");
    check(reader.varint() == 3, "expected stonecutter display");
    expectIngredient(reader, recipes[3].cells[0]);
    expectStackDisplay(reader, recipes[3]);
    expectItemDisplay(reader, item("minecraft:stonecutter"));
    check(!reader.boolean(), "ungrouped recipe encoded as grouped");
    check(reader.varint() == 10, "stonecutter category mismatch");
    check(!reader.boolean(), "unexpected crafting requirements");
    check(reader.u8() == 3, "recipe-book add flags mismatch");

    check(reader.varint() == 3, "smithing display ID mismatch");
    check(reader.varint() == 4, "expected smithing display");
    for (const auto& ingredient : recipes[4].cells)
        expectIngredient(reader, ingredient);
    expectStackDisplay(reader, recipes[4]);
    expectItemDisplay(reader, item("minecraft:smithing_table"));
    check(!reader.boolean(), "ungrouped recipe encoded as grouped");
    check(reader.varint() == 11, "smithing category mismatch");
    check(!reader.boolean(), "unexpected crafting requirements");
    check(reader.u8() == 3, "recipe-book add flags mismatch");

    check(reader.boolean(), "replace flag mismatch");
    check(reader.remaining() == 0, "trailing bytes in recipe-book packet");
}

void testPartialAddAndRemoveUseNetworkIds() {
    const auto recipes = fixtures();
    const std::vector<int> selected{3, 1, 3};
    const auto add = cppfm::recipe_book::makeAddPacketBody(
        recipes, &selected, false);
    Reader addReader(add.data);
    check(addReader.varint() == 1,
          "partial add should omit special and duplicate recipes");
    check(addReader.varint() == 2,
          "partial add used source index instead of network display ID");

    const std::vector<int> removal{1, 3, 4};
    const auto remove = cppfm::recipe_book::makeRemovePacketBody(
        recipes, removal);
    Reader removeReader(remove.data);
    check(removeReader.varint() == 2,
          "remove should omit non-displayable special recipes");
    check(removeReader.varint() == 2 && removeReader.varint() == 3,
          "remove used source recipe indices instead of display IDs");
    check(removeReader.remaining() == 0, "trailing bytes in recipe removal");
}

void testMultipleDisplaysPerRecipe() {
    auto recipes = fixtures();
    Recipe::BookDisplay alternate;
    alternate.kind = Recipe::Kind::Shapeless;
    Ingredient coal;
    coal.items.insert(item("minecraft:coal"));
    alternate.ingredients.push_back(coal);
    alternate.result = cppfm::ItemStack::of(item("minecraft:gold_nugget"), 2);
    recipes.front().additionalDisplays.push_back(alternate);

    const auto entries = cppfm::recipe_book::buildNetworkEntries(recipes);
    check(entries.size() == 5, "both recipe displays must get network IDs");
    check(entries[0].recipeIndex == 0 && entries[0].displayIndex == 0 &&
              entries[0].displayId == 0,
          "primary recipe display mapping mismatch");
    check(entries[1].recipeIndex == 0 && entries[1].displayIndex == 1 &&
              entries[1].displayId == 1,
          "additional recipe display mapping mismatch");
    check(cppfm::recipe_book::recipeIndexForDisplayId(recipes, 0) == 0 &&
              cppfm::recipe_book::recipeIndexForDisplayId(recipes, 1) == 0,
          "both display IDs must resolve to the same source recipe");
    check(cppfm::recipe_book::displayIdForRecipeIndex(recipes, 0) == 0,
          "source recipe should resolve to its first display ID");
    const auto selectedAlternate = cppfm::recipe_book::recipeForDisplay(
        recipes.front(), entries[1].displayIndex);
    check(selectedAlternate.kind == Recipe::Kind::Shapeless &&
              selectedAlternate.ingredients.size() == 1 &&
              selectedAlternate.result.itemId == alternate.result.itemId &&
              selectedAlternate.additionalDisplays.empty(),
          "selected network display must drive recipe placement semantics");

    const auto packet = cppfm::recipe_book::makeAddPacketBody(
        recipes, nullptr, true);
    Reader reader(packet.data);
    check(reader.varint() == 5, "multi-display packet entry count mismatch");
    check(reader.varint() == 0 && reader.varint() == 1,
          "primary shaped display wire ID mismatch");
    check(reader.varint() == 2 && reader.varint() == 1 &&
              reader.varint() == 2,
          "primary shaped display dimensions mismatch");
    expectIngredient(reader, recipes[0].cells[0]);
    expectIngredient(reader, recipes[0].cells[1]);
    expectStackDisplay(reader, recipes[0]);
    expectItemDisplay(reader, item("minecraft:crafting_table"));
    check(reader.boolean() && reader.varint() == 0,
          "primary display group metadata mismatch");
    check(reader.varint() == 0 && !reader.boolean() && reader.u8() == 3,
          "primary display category or flags mismatch");

    check(reader.varint() == 1 && reader.varint() == 0,
          "alternate display ID/type mismatch");
    check(reader.varint() == 1, "alternate shapeless ingredient count mismatch");
    expectIngredient(reader, coal);
    expectStackDisplay(reader, alternate.result);
    expectItemDisplay(reader, item("minecraft:crafting_table"));
    check(reader.boolean() && reader.varint() == 0,
          "alternate display must share its recipe group");
    check(reader.varint() == 0 && !reader.boolean() && reader.u8() == 3,
          "alternate display category or flags mismatch");

    const auto remove = cppfm::recipe_book::makeRemovePacketBody(
        recipes, std::vector<int>{0});
    Reader removeReader(remove.data);
    check(removeReader.varint() == 2 && removeReader.varint() == 0 &&
              removeReader.varint() == 1 && removeReader.remaining() == 0,
          "removing a recipe must remove each of its display IDs");
}

void testTransmuteAndTrimRecipeLoading() {
    namespace fs = std::filesystem;
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path directory = fs::temp_directory_path() /
        ("cppfm_recipe_loader_" + unique);
    std::error_code error;
    if (!fs::create_directory(directory, error) || error) {
        check(false, "create temporary recipe directory");
        return;
    }
    struct RemoveDirectory {
        fs::path path;
        ~RemoveDirectory() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup{directory};

    {
        std::ofstream output(directory / "transmute.json");
        output << R"({"type":"minecraft:crafting_transmute","input":{"item":"minecraft:stone"},"material":[{"item":"minecraft:coal"},{"item":"minecraft:diamond"}],"result":{"id":"minecraft:diamond_block"}})";
    }
    {
        std::ofstream output(directory / "trim.json");
        output << R"({"type":"minecraft:smithing_trim","template":{"item":"minecraft:coast_armor_trim_smithing_template"},"base":{"item":"minecraft:iron_chestplate"},"addition":{"item":"minecraft:amethyst_shard"}})";
    }

    cppfm::RecipeManager manager;
    manager.loadDefaults();
    manager.loadDirectory(directory);
    const auto* transmute = manager.findById("minecraft:transmute");
    const auto* trim = manager.findById("minecraft:trim");
    check(transmute && transmute->kind == Recipe::Kind::Shapeless &&
              transmute->ingredients.size() == 2,
          "crafting_transmute input/material must be retained as a normal recipe");
    if (transmute) {
        check(transmute->ingredients[1].accepts(item("minecraft:coal")) &&
                  transmute->ingredients[1].accepts(item("minecraft:diamond")),
              "transmute ingredient alternatives must be unioned");
        std::vector<cppfm::ItemStack> grid(9);
        grid[0] = cppfm::ItemStack::of(item("minecraft:stone"));
        grid[1] = cppfm::ItemStack::of(item("minecraft:diamond"));
        check(transmute->matches(grid, 3, 3),
              "transmute recipe should match its input and material");
    }
    check(trim && trim->kind == Recipe::Kind::Smithing && trim->result.empty() &&
              trim->smithingTrimResult && trim->cells.size() == 3,
          "smithing_trim without a fixed result must be retained");
    if (trim) {
        check(cppfm::recipe_book::hasDisplay(*trim),
              "smithing trim should expose a recipe-book display");
        cppfm::WriteBuffer display;
        cppfm::recipe_book::writeDisplay(display, *trim);
        Reader reader(display.data);
        check(reader.varint() == 4, "trim must use SmithingRecipeDisplay");
        for (const auto& ingredient : trim->cells)
            expectIngredient(reader, ingredient);
        check(reader.varint() == 5,
              "trim result must use SmithingTrimSlotDisplay");
        expectIngredient(reader, trim->cells[1]);
        expectIngredient(reader, trim->cells[2]);
        expectIngredient(reader, trim->cells[0]);
        expectItemDisplay(reader, item("minecraft:smithing_table"));
        check(reader.remaining() == 0,
              "trailing bytes in smithing trim display");
    }
}

} // namespace

int main() {
    testNetworkIdMapping();
    testPacketDisplaysAndStations();
    testPartialAddAndRemoveUseNetworkIds();
    testMultipleDisplaysPerRecipe();
    testTransmuteAndTrimRecipeLoading();
    std::puts("PASS recipe-book displays, categories, stations, and network IDs");
    return 0;
}
