#include "game/GameServer.hpp"
#include "core/Utf8.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

#ifndef _WIN32
#include <sys/stat.h>
#endif

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

void testLooseDatapackResourceBudgets() {
    namespace fs = std::filesystem;
    TemporaryDirectory temporary;
    std::error_code error;
    const auto jsonRoot = temporary.path / "data";
    fs::create_directories(jsonRoot, error);
    check(!error, "create loose-resource budget fixture directory");

    const auto write = [](const fs::path& path, std::string_view contents) {
        std::error_code createError;
        fs::create_directories(path.parent_path(), createError);
        if (createError) return false;
        std::ofstream file(path, std::ios::binary);
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        return static_cast<bool>(file);
    };

    const auto tooLarge = jsonRoot / "large.json";
    check(write(tooLarge, "{\"x\":12345}"),
          "write oversized loose JSON fixture");
    cppfm::DatapackManager perFileManager;
    cppfm::datapack_detail::LooseDataBudget perFileBudget(10, 8, 64);
    bool perFileRejected = false;
    try {
        perFileManager.scanJsonDirPath(jsonRoot, "test",
            perFileManager.advancements, perFileBudget);
    } catch (const std::runtime_error&) {
        perFileRejected = true;
    }
    check(perFileRejected,
          "loose JSON size is rejected before unbounded allocation");

#ifndef _WIN32
    const auto specialRoot = temporary.path / "special-files";
    fs::create_directories(specialRoot, error);
    check(!error, "create special-file budget fixture directory");
    const auto fifoPath = specialRoot / "blocked.json";
    const bool fifoCreated = ::mkfifo(fifoPath.c_str(), 0600) == 0;
    check(fifoCreated, "create FIFO resource fixture");
    bool fifoRejected = false;
    if (fifoCreated) {
        try {
            cppfm::datapack_detail::LooseDataBudget budget;
            (void)budget.readFile(fifoPath);
        } catch (const fs::filesystem_error&) {
            fifoRejected = true;
        }
    }
    check(fifoRejected,
          "loose resource reader rejects FIFOs without waiting for a writer");

    const auto regularTarget = specialRoot / "regular.json";
    check(write(regularTarget, "{}"), "write symlink target fixture");
    const auto symlinkPath = specialRoot / "linked.json";
    error.clear();
    fs::create_symlink(regularTarget.filename(), symlinkPath, error);
    const bool symlinkCreated = !error;
    check(symlinkCreated, "create symlink resource fixture");
    bool symlinkRejected = false;
    if (symlinkCreated) {
        try {
            cppfm::datapack_detail::LooseDataBudget budget;
            (void)budget.readFile(symlinkPath);
        } catch (const fs::filesystem_error&) {
            symlinkRejected = true;
        }
    }
    check(symlinkRejected,
          "loose resource reader refuses symlinked files");
#endif

    const auto aggregateRoot = temporary.path / "aggregate";
    check(write(aggregateRoot / "a.json", "{}") &&
              write(aggregateRoot / "b.json", "{}"),
          "write aggregate-budget JSON fixtures");
    cppfm::DatapackManager aggregateManager;
    cppfm::datapack_detail::LooseDataBudget aggregateBudget(10, 8, 3);
    bool aggregateRejected = false;
    std::unordered_map<std::string, std::string> aggregateOutput{
        {"preserved:existing", "keep"}};
    try {
        aggregateManager.scanJsonDirPath(aggregateRoot, "test",
            aggregateOutput, aggregateBudget);
    } catch (const std::runtime_error&) {
        aggregateRejected = true;
    }
    check(aggregateRejected,
          "loose JSON aggregate bytes share one bounded budget");
    check(aggregateOutput.size() == 1 &&
              aggregateOutput.at("preserved:existing") == "keep",
          "failed loose JSON scan publishes no partial resource updates");

    const auto countRoot = temporary.path / "count";
    check(write(countRoot / "a.json", "{}") &&
              write(countRoot / "b.json", "{}"),
          "write file-count-budget fixtures");
    cppfm::DatapackManager countManager;
    cppfm::datapack_detail::LooseDataBudget countBudget(1, 8, 64);
    bool countRejected = false;
    try {
        countManager.scanJsonDirPath(countRoot, "test",
            countManager.advancements, countBudget);
    } catch (const std::runtime_error&) {
        countRejected = true;
    }
    check(countRejected, "loose resource file count is bounded");

    const auto entriesRoot = temporary.path / "directory-entries";
    check(write(entriesRoot / "a.json", "{}") &&
              write(entriesRoot / "nested" / "b.json", "{}"),
          "write directory-entry budget fixtures");
    cppfm::DatapackManager entriesManager;
    cppfm::datapack_detail::LooseDataBudget entriesBudget(
        10, 8, 64, 100, 1);
    bool entriesRejected = false;
    try {
        entriesManager.scanJsonDirPath(entriesRoot, "test",
                                       entriesManager.advancements,
                                       entriesBudget);
    } catch (const cppfm::datapack_detail::LooseDataLimitExceeded&) {
        entriesRejected = true;
    }
    check(entriesRejected,
          "directory traversal is bounded before collecting file paths");
    check(entriesManager.advancements.empty(),
          "directory-entry limit failure publishes no partial scan results");

    const auto functionRoot = temporary.path / "functions";
    check(write(functionRoot / "large.mcfunction", "say bounded input\n"),
          "write oversized function fixture");
    cppfm::DatapackManager functionManager;
    cppfm::datapack_detail::LooseDataBudget functionBudget(10, 8, 64);
    bool functionRejected = false;
    try {
        functionManager.scanFunctionDirPath(functionRoot, "test",
                                            functionBudget);
    } catch (const std::runtime_error&) {
        functionRejected = true;
    }
    check(functionRejected,
          "mcfunction content is bounded before line-vector allocation");

    const auto linesRoot = temporary.path / "functions-lines";
    check(write(linesRoot / "many.mcfunction", "a\nb\nc\n"),
          "write oversized function-line fixture");
    cppfm::DatapackManager linesManager;
    cppfm::datapack_detail::LooseDataBudget linesBudget(10, 64, 64, 2);
    bool linesRejected = false;
    try {
        linesManager.scanFunctionDirPath(linesRoot, "test", linesBudget);
    } catch (const std::runtime_error&) {
        linesRejected = true;
    }
    check(linesRejected,
          "mcfunction line-vector overhead is bounded independently of bytes");

    const auto sharedRoot = temporary.path / "shared-readers";
    const auto tagsRoot = sharedRoot / "tags";
    const auto lootRoot = sharedRoot / "data";
    const auto recipesRoot = sharedRoot / "recipes";
    check(write(tagsRoot / "item" / "fixture.json", "{}") &&
              write(lootRoot / "minecraft" / "loot_table" / "fixture.json",
                    "{}") &&
              write(recipesRoot / "fixture.json", "{}"),
          "write files for shared tag, loot, and recipe budget");
    cppfm::datapack_detail::LooseDataBudget sharedBudget(3, 8, 5);
    cppfm::TagManager boundedTags;
    cppfm::LootTableEvaluator boundedLoot;
    cppfm::RecipeManager boundedRecipes;
    boundedRecipes.loadDefaults();
    bool sharedBudgetRejected = false;
    try {
        boundedTags.loadDirectoryPath(tagsRoot, sharedBudget);
        boundedLoot.loadDirectoryPath(lootRoot, sharedBudget);
        boundedRecipes.loadDirectory(recipesRoot, sharedBudget);
    } catch (const cppfm::datapack_detail::LooseDataLimitExceeded&) {
        sharedBudgetRejected = true;
    }
    check(sharedBudgetRejected,
          "tag, loot, and recipe readers consume one aggregate reload budget");

    const auto atomicTagsRoot = temporary.path / "atomic-tags" / "tags";
    check(write(atomicTagsRoot / "item" / "too_large.json",
                R"({"values":["minecraft:diamond"]})"),
          "write tag fixture that exceeds its byte budget");
    cppfm::TagManager atomicTags;
    atomicTags.itemTags["preserved:tag"].insert(77);
    cppfm::datapack_detail::LooseDataBudget atomicTagBudget(10, 8, 64);
    bool atomicTagRejected = false;
    try {
        atomicTags.loadDirectoryPath(atomicTagsRoot, atomicTagBudget);
    } catch (const cppfm::datapack_detail::LooseDataLimitExceeded&) {
        atomicTagRejected = true;
    }
    check(atomicTagRejected && atomicTags.itemTags.size() == 1 &&
              atomicTags.itemTags.at("preserved:tag").contains(77),
          "failed direct tag reload leaves the previously published state intact");

    const auto atomicRecipesRoot = temporary.path / "atomic-recipes";
    const std::string validRecipe =
        R"({"type":"minecraft:crafting_shapeless","ingredients":[{"item":"minecraft:diamond"}],"result":{"id":"minecraft:diamond_block","count":1}})";
    const std::string oversizedRecipe = validRecipe + std::string(8, ' ');
    check(write(atomicRecipesRoot / "a-added.json", validRecipe) &&
              write(atomicRecipesRoot / "z-too_large.json", oversizedRecipe),
          "write recipe fixtures for transactional loading");
    cppfm::RecipeManager atomicRecipes;
    atomicRecipes.loadDefaults();
    cppfm::datapack_detail::LooseDataBudget atomicRecipeBudget(
        10, validRecipe.size(), validRecipe.size() * 2);
    bool atomicRecipeRejected = false;
    try {
        atomicRecipes.loadDirectory(atomicRecipesRoot, atomicRecipeBudget);
    } catch (const cppfm::datapack_detail::LooseDataLimitExceeded&) {
        atomicRecipeRejected = true;
    }
    check(atomicRecipeRejected &&
              atomicRecipes.findById("minecraft:a-added") == nullptr,
          "failed direct recipe load does not publish earlier files from the same scan");

    const auto deepRecipesRoot = temporary.path / "deep-recipe-json";
    std::string tooDeepJson(cppfm::json::Value::kMaxDepth + 1, '[');
    tooDeepJson.append(cppfm::json::Value::kMaxDepth + 1, ']');
    check(write(deepRecipesRoot / "a-added.json", validRecipe) &&
              write(deepRecipesRoot / "z-too-deep.json", tooDeepJson),
          "write recipe JSON resource-depth fixtures");
    cppfm::RecipeManager depthLimitedRecipes;
    depthLimitedRecipes.loadDefaults();
    bool jsonResourceLimitPropagated = false;
    try {
        cppfm::datapack_detail::LooseDataBudget budget(
            10, tooDeepJson.size(), tooDeepJson.size() * 2);
        depthLimitedRecipes.loadDirectory(deepRecipesRoot, budget);
    } catch (const cppfm::json::ResourceLimitExceeded&) {
        jsonResourceLimitPropagated = true;
    }
    check(jsonResourceLimitPropagated &&
              depthLimitedRecipes.findById("minecraft:a-added") == nullptr,
          "JSON resource-limit failure aborts and rolls back direct recipe loading");

    const auto lootBaselineRoot = temporary.path / "loot-baseline" / "data" /
        "compat" / "loot_table";
    const auto atomicLootRoot = temporary.path / "atomic-loot" / "data" /
        "compat" / "loot_table";
    const std::string validLoot =
        R"({"pools":[{"rolls":1,"entries":[{"type":"minecraft:item","name":"minecraft:diamond"}]}]})";
    const std::string oversizedLoot = validLoot + std::string(8, ' ');
    check(write(lootBaselineRoot / "baseline.json", validLoot) &&
              write(atomicLootRoot / "a-added.json", validLoot) &&
              write(atomicLootRoot / "z-too_large.json", oversizedLoot),
          "write loot-table fixtures for transactional loading");
    cppfm::LootTableEvaluator atomicLoot;
    atomicLoot.loadDirectoryPath(lootBaselineRoot);
    cppfm::datapack_detail::LooseDataBudget atomicLootBudget(
        10, validLoot.size(), validLoot.size() * 2);
    bool atomicLootRejected = false;
    try {
        atomicLoot.loadDirectoryPath(atomicLootRoot, atomicLootBudget);
    } catch (const cppfm::datapack_detail::LooseDataLimitExceeded&) {
        atomicLootRejected = true;
    }
    check(atomicLootRejected && atomicLoot.find("compat:baseline") != nullptr &&
              atomicLoot.find("compat:a-added") == nullptr,
          "failed direct loot-table load preserves its prior generation");

    const auto referenceRoot = temporary.path / "tag-reference-chain" / "data";
    check(write(referenceRoot / "compat" / "tags" / "item" / "a.json",
                R"({"values":["#compat:b"]})") &&
              write(referenceRoot / "compat" / "tags" / "item" / "b.json",
                    R"({"values":["#compat:c"]})") &&
              write(referenceRoot / "compat" / "tags" / "item" / "c.json",
                    R"({"values":["minecraft:diamond"]})"),
          "write chained namespaced-tag reference fixtures");
    cppfm::TagManager resolvedTags;
    resolvedTags.mergeDataPackDirectoriesPath({referenceRoot});
    const auto diamondId = cppfm::gen::itemIdByName().at("minecraft:diamond");
    check(resolvedTags.itemTags.at("compat:a").contains(diamondId),
          "tag-reference worklist resolves a backward dependency chain");

    cppfm::TagManager cappedTags;
    cappedTags.itemTags["preserved:tag"].insert(77);
    cppfm::datapack_detail::LooseDataBudget tagEntryBudget(
        100, 4096, 8192, 1000, 1000, 2);
    bool tagExpansionRejected = false;
    try {
        cappedTags.mergeDataPackDirectoriesPath({referenceRoot},
                                                tagEntryBudget);
    } catch (const cppfm::datapack_detail::LooseDataLimitExceeded&) {
        tagExpansionRejected = true;
    }
    check(tagExpansionRejected && cappedTags.itemTags.size() == 1 &&
              cappedTags.itemTags.at("preserved:tag").contains(77),
          "tag-expansion cap fails transactionally before publishing partial references");

    cppfm::TagManager cappedReferences;
    cappedReferences.itemTags["preserved:tag"].insert(77);
    cppfm::datapack_detail::LooseDataBudget referenceBudget(
        100, 4096, 8192, 1000, 1000, 1000, 1, 1000);
    bool referenceLimitRejected = false;
    try {
        cappedReferences.mergeDataPackDirectoriesPath({referenceRoot},
                                                      referenceBudget);
    } catch (const cppfm::datapack_detail::LooseDataLimitExceeded&) {
        referenceLimitRejected = true;
    }
    check(referenceLimitRejected && cappedReferences.itemTags.size() == 1 &&
              cappedReferences.itemTags.at("preserved:tag").contains(77),
          "unique tag-reference edges are bounded transactionally");

    const auto duplicateReferenceRoot =
        temporary.path / "duplicate-tag-references" / "data";
    check(write(duplicateReferenceRoot / "compat" / "tags" / "item" /
                    "a.json",
                R"({"values":["#compat:c","#compat:c"]})") &&
              write(duplicateReferenceRoot / "compat" / "tags" / "item" /
                        "c.json",
                    R"({"values":["minecraft:diamond"]})"),
          "write duplicate-reference deduplication fixtures");
    cppfm::TagManager deduplicatedReferences;
    cppfm::datapack_detail::LooseDataBudget deduplicatedReferenceBudget(
        100, 4096, 8192, 1000, 1000, 1000, 1, 1);
    deduplicatedReferences.mergeDataPackDirectoriesPath(
        {duplicateReferenceRoot}, deduplicatedReferenceBudget);
    check(deduplicatedReferences.itemTags.at("compat:a").contains(diamondId),
          "duplicate references deduplicate without spending extra edge or resolution work");

    const auto workLimitRoot = temporary.path / "tag-work-limit" / "data";
    check(write(workLimitRoot / "compat" / "tags" / "item" / "a.json",
                R"({"values":["#compat:c"]})") &&
              write(workLimitRoot / "compat" / "tags" / "item" / "b.json",
                    R"({"values":["#compat:c"]})") &&
              write(workLimitRoot / "compat" / "tags" / "item" / "c.json",
                    R"({"values":["minecraft:diamond"]})"),
          "write tag-resolution work-limit fixtures");
    cppfm::TagManager workLimitedTags;
    workLimitedTags.itemTags["preserved:tag"].insert(77);
    cppfm::datapack_detail::LooseDataBudget workBudget(
        100, 4096, 8192, 1000, 1000, 1000, 10, 1);
    bool resolutionWorkRejected = false;
    try {
        workLimitedTags.mergeDataPackDirectoriesPath({workLimitRoot},
                                                     workBudget);
    } catch (const cppfm::datapack_detail::LooseDataLimitExceeded&) {
        resolutionWorkRejected = true;
    }
    check(resolutionWorkRejected && workLimitedTags.itemTags.size() == 1 &&
              workLimitedTags.itemTags.at("preserved:tag").contains(77),
          "tag-resolution work limit prevents fanout amplification transactionally");
}

} // namespace

int main() {
    testUtf8DatapackPaths();
    testUtf8DatapackTagsAndLootTables();
    testRecipeResultCountValidation();
    testLooseDatapackResourceBudgets();
    if (failures != 0) {
        std::fprintf(stderr, "%d datapack UTF-8 check(s) failed\n", failures);
        return 1;
    }
    std::puts("Datapack UTF-8 path checks passed");
    return 0;
}
