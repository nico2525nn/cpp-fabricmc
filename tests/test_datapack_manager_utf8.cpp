#include "game/GameServer.hpp"
#include "core/Utf8.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", message);
}

std::string bytes(std::u8string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char8_t byte : value)
        result.push_back(static_cast<char>(byte));
    return result;
}

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory() {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path = std::filesystem::temp_directory_path() /
               ("cppfm-datapack-utf8-" + std::to_string(nonce));
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void testUtf8DatapackPaths() {
    namespace fs = std::filesystem;
    using cppfm::datapack_archive::pathFromUtf8;

    cppfm::DatapackManager literalPathProbe;
    literalPathProbe.loadPackDirectory("nonexistent-literal-path-probe");

    const std::string japaneseDirectory = bytes(u8"\u65e5\u672c\u8a9e");
    const std::string japaneseNamespace = bytes(u8"\u540d\u524d\u7a7a\u9593");
    const std::string japaneseFunction = bytes(u8"\u6311\u6226\u72b6\u614b");
    const auto unicodeComponent = pathFromUtf8(japaneseDirectory);
    check(cppfm::datapack_detail::pathToUtf8(unicodeComponent) ==
              japaneseDirectory,
          "path-to-UTF-8 conversion preserves a non-ASCII component");
    check(cppfm::datapack_detail::genericPathToUtf8(
              fs::path("left") / unicodeComponent / "right") ==
              "left/" + japaneseDirectory + "/right",
          "generic UTF-8 path uses portable separators");

    TemporaryDirectory temporary;
    const fs::path dataRoot = temporary.path / unicodeComponent;
    const fs::path namespaceRoot = dataRoot / pathFromUtf8(japaneseNamespace);
    const fs::path functionPath = namespaceRoot / "functions" /
        pathFromUtf8(japaneseFunction + ".mcfunction");
    const fs::path nestedFunctionPath = namespaceRoot / "functions" /
        pathFromUtf8(japaneseDirectory) /
        pathFromUtf8(japaneseFunction + ".mcfunction");
    const fs::path advancementPath = namespaceRoot / "advancements" /
        pathFromUtf8(japaneseFunction + ".json");

    std::error_code error;
    fs::create_directories(functionPath.parent_path(), error);
    check(!error, "create Unicode function directory");
    fs::create_directories(nestedFunctionPath.parent_path(), error);
    check(!error, "create nested Unicode function directory");
    fs::create_directories(advancementPath.parent_path(), error);
    check(!error, "create Unicode advancement directory");

    {
        std::ofstream file(functionPath, std::ios::binary);
        file << "say direct\n";
        check(static_cast<bool>(file), "write Unicode-named function");
    }
    {
        std::ofstream file(nestedFunctionPath, std::ios::binary);
        file << "say nested\n";
        check(static_cast<bool>(file), "write nested Unicode-named function");
    }
    {
        std::ofstream file(advancementPath, std::ios::binary);
        file << "{}";
        check(static_cast<bool>(file), "write Unicode-named advancement");
    }

    cppfm::DatapackManager manager;
    manager.loadPackDirectory(cppfm::datapack_detail::pathToUtf8(dataRoot));

    const auto directId = japaneseNamespace + ":" + japaneseFunction;
    const auto nestedId = japaneseNamespace + ":" + japaneseDirectory + "/" +
                          japaneseFunction;
    check(manager.functions.contains(directId),
          "loadPackDirectory indexes UTF-8 namespace and function names");
    check(manager.functions.contains(nestedId),
          "nested resource IDs use slash-separated UTF-8 paths");
    check(manager.advancements.contains(
              japaneseNamespace + ":" + japaneseFunction),
          "loadPackDirectory indexes UTF-8 advancement names");
}

void testUtf8DatapackTagsAndLootTables() {
    namespace fs = std::filesystem;
    const std::string unicodeDirectory = bytes(u8"\u65e5\u672c\u8a9e");
    TemporaryDirectory temporary;
    const fs::path packRoot = temporary.path /
        cppfm::utf8::pathFromUtf8(unicodeDirectory);
    const fs::path dataRoot = packRoot / "data";
    const std::string nameSpace = "compat";
    const fs::path tagPath = dataRoot / nameSpace / "tags" / "item" /
        cppfm::utf8::pathFromUtf8(unicodeDirectory) / "diamonds.json";
    const fs::path lootPath = dataRoot / nameSpace / "loot_table" /
        cppfm::utf8::pathFromUtf8(unicodeDirectory) / "diamond.json";
    const fs::path recipePath = dataRoot / nameSpace / "recipe" /
        cppfm::utf8::pathFromUtf8(unicodeDirectory + ".json");

    std::error_code error;
    fs::create_directories(tagPath.parent_path(), error);
    check(!error, "create non-ASCII datapack tag directory");
    fs::create_directories(lootPath.parent_path(), error);
    check(!error, "create non-ASCII datapack loot-table directory");
    fs::create_directories(recipePath.parent_path(), error);
    check(!error, "create non-ASCII datapack recipe directory");
    {
        std::ofstream file(tagPath, std::ios::binary);
        file << R"({"values":["minecraft:diamond"]})";
        check(static_cast<bool>(file), "write datapack tag JSON");
    }
    {
        std::ofstream file(lootPath, std::ios::binary);
        file << R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})";
        check(static_cast<bool>(file), "write datapack loot-table JSON");
    }
    {
        std::ofstream file(recipePath, std::ios::binary);
        file << R"({"type":"minecraft:crafting_shapeless","ingredients":[{"item":"minecraft:diamond"}],"result":{"id":"minecraft:diamond_block","count":1}})";
        check(static_cast<bool>(file), "write Unicode-named datapack recipe JSON");
    }

    const std::string retainedPackRecipeId = "continuity:keep";
    const auto retainedPackRecipePath = temporary.path / "secondary" / "data" /
        "continuity" / "recipe" / "keep.json";
    fs::create_directories(retainedPackRecipePath.parent_path(), error);
    check(!error, "create second datapack recipe directory");
    {
        std::ofstream file(retainedPackRecipePath, std::ios::binary);
        file << R"({"type":"minecraft:crafting_shapeless","ingredients":[{"item":"minecraft:emerald"}],"result":{"id":"minecraft:emerald_block","count":1}})";
        check(static_cast<bool>(file), "write second datapack recipe JSON");
    }

    cppfm::TagManager tags;
    tags.mergeDataPackDirectoriesPath({dataRoot});
    const auto* diamondTag = tags.getItemTag(
        nameSpace + ":" + unicodeDirectory + "/diamonds");
    const auto diamondId = cppfm::gen::itemIdByName().find(
        "minecraft:diamond");
    check(diamondTag != nullptr,
          "TagManager loads the tag through a native Unicode path");
    check(diamondTag != nullptr &&
              diamondId != cppfm::gen::itemIdByName().end() &&
              diamondTag->contains(diamondId->second),
          "loaded datapack tag resolves its item value");

    cppfm::TagManager standaloneTags;
    standaloneTags.loadDirectoryPath(dataRoot / nameSpace / "tags");
    const auto* standaloneDiamondTag = standaloneTags.getItemTag(
        "minecraft:" + unicodeDirectory + "/diamonds");
    check(standaloneDiamondTag != nullptr &&
              diamondId != cppfm::gen::itemIdByName().end() &&
              standaloneDiamondTag->contains(diamondId->second),
          "TagManager built-in-layout loading opens Unicode paths natively");

    cppfm::LootTableEvaluator lootTables;
    lootTables.loadDirectoryPath(dataRoot);
    const auto* table = lootTables.find(
        nameSpace + ":" + unicodeDirectory + "/diamond");
    check(table != nullptr,
          "LootTableEvaluator loads the table through a native Unicode path");
    check(table != nullptr && !table->pools.empty() &&
              !table->pools.front().entries.empty() &&
              table->pools.front().entries.front().name ==
                  "minecraft:diamond",
          "loaded Unicode-path loot table retains its item entry");

    cppfm::RecipeManager recipes;
    recipes.loadDefaults();
    cppfm::DatapackManager datapacks;
    const auto missingAssets = cppfm::utf8::pathToUtf8(
        temporary.path / "missing-assets");
    const auto worldDatapacks = cppfm::utf8::pathToUtf8(temporary.path);
    datapacks.loadAll(recipes, missingAssets, worldDatapacks);
    const auto managerTag = datapacks.tagManager.itemTags.find(
        nameSpace + ":" + unicodeDirectory + "/diamonds");
    const auto* managerTable = datapacks.lootTables.find(
        nameSpace + ":" + unicodeDirectory + "/diamond");
    check(datapacks.isAvailable(unicodeDirectory) &&
              datapacks.isEnabled(unicodeDirectory),
          "DatapackManager discovers a datapack under a Unicode folder name");
    check(managerTag != datapacks.tagManager.itemTags.end() &&
              diamondId != cppfm::gen::itemIdByName().end() &&
              managerTag->second.contains(diamondId->second),
          "DatapackManager merges tags through the Unicode native-path pipeline");
    check(managerTable != nullptr && !managerTable->pools.empty() &&
              !managerTable->pools.front().entries.empty() &&
              managerTable->pools.front().entries.front().name ==
                  "minecraft:diamond",
          "DatapackManager loads loot tables through the Unicode native-path pipeline");
    const auto recipeId = nameSpace + ":" + unicodeDirectory;
    check(recipes.findById(recipeId) != nullptr,
          "DatapackManager loads a Unicode-named 1.21.4 datapack recipe");
    check(datapacks.isAvailable("secondary") &&
              recipes.findById(retainedPackRecipeId) != nullptr,
          "DatapackManager loads a recipe from a second enabled datapack");
    check(datapacks.disablePack(unicodeDirectory) &&
              recipes.findById(recipeId) == nullptr &&
              recipes.findById(retainedPackRecipeId) != nullptr,
          "disabling a directory datapack removes only its own recipes");
    check(datapacks.enablePack(unicodeDirectory) &&
              recipes.findById(recipeId) != nullptr &&
              recipes.findById(retainedPackRecipeId) != nullptr,
          "re-enabling a directory datapack restores its recipes");
    datapacks.loadAll(recipes, missingAssets, worldDatapacks);
    check(recipes.findById(recipeId) != nullptr &&
              recipes.findById(retainedPackRecipeId) != nullptr,
          "datapack reload preserves recipes from all enabled packs");
}

void testRecipeResultCountValidation() {
    namespace fs = std::filesystem;
    TemporaryDirectory temporary;
    std::error_code error;
    fs::create_directories(temporary.path, error);
    check(!error, "create recipe-count fixture directory");

    const auto writeRecipe = [&](const std::string& id,
                                  const std::string& countJson) {
        std::ofstream file(temporary.path / (id + ".json"),
                           std::ios::binary);
        file << R"({"type":"minecraft:crafting_shapeless","ingredients":[{"item":"minecraft:diamond"}],"result":{"id":"minecraft:diamond_block")";
        if (!countJson.empty()) file << R"(,"count":)" << countJson;
        file << "}}";
        check(static_cast<bool>(file), "write recipe-count fixture");
    };

    writeRecipe("maximum", "32767");
    writeRecipe("overflow", "32768");
    writeRecipe("wraparound", "65537");
    writeRecipe("zero", "0");
    writeRecipe("negative", "-1");
    writeRecipe("fractional", "1.5");
    writeRecipe("string", "\"2\"");
    writeRecipe("default", "");

    cppfm::RecipeManager recipes;
    recipes.loadDefaults();
    recipes.loadDirectory(temporary.path);
    const auto* maximum = recipes.findById("minecraft:maximum");
    check(maximum && maximum->result.count == 32767,
          "largest representable recipe result count is preserved");
    const auto* defaultCount = recipes.findById("minecraft:default");
    check(defaultCount && defaultCount->result.count == 1,
          "missing recipe result count defaults to one");
    for (const auto* id : {"overflow", "wraparound", "zero", "negative",
                           "fractional", "string"}) {
        check(recipes.findById(std::string("minecraft:") + id) == nullptr,
              "invalid recipe result count is rejected before narrowing");
    }
}

} // namespace

int main() {
    testUtf8DatapackPaths();
    testUtf8DatapackTagsAndLootTables();
    testRecipeResultCountValidation();
    if (failures != 0) {
        std::fprintf(stderr, "%d datapack UTF-8 check(s) failed\n", failures);
        return 1;
    }
    std::puts("Datapack UTF-8 path checks passed");
    return 0;
}
