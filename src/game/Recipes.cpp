// Recipes implementation: built-in clean-room recipe table + JSON loader.
#include "Recipes.hpp"
#include "TagManager.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>

namespace cppfm {

namespace {

std::string normalizeResourceName(std::string name) {
    const bool isTag = !name.empty() && name.front() == '#';
    if (isTag) name.erase(name.begin());
    if (name.find(':') == std::string::npos)
        name.insert(0, "minecraft:");
    if (isTag) name.insert(name.begin(), '#');
    return name;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::string genericPathToUtf8(const std::filesystem::path& path) {
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::optional<std::int16_t> checkedRecipeStackCount(int count) noexcept {
    if (count <= 0 ||
        count > std::numeric_limits<std::int16_t>::max())
        return std::nullopt;
    return static_cast<std::int16_t>(count);
}

std::optional<int> parseRecipeStackCount(const json::Value& result) noexcept {
    const json::Value* value = result.find("count");
    if (!value) value = result.find("Count");
    if (!value) return 1;
    if (!value->isNum() || !std::isfinite(value->number) ||
        std::trunc(value->number) != value->number || value->number < 1.0 ||
        value->number >
            static_cast<double>(std::numeric_limits<std::int16_t>::max()))
        return std::nullopt;
    return static_cast<int>(value->number);
}

} // namespace

// ------------------------------------------------------------------ helpers

Ingredient RecipeManager::fromName(const std::string& n) const {
    Ingredient ing;
    if (n.rfind('#', 0) == 0) {
        auto it = tags_.find(n.substr(1));
        if (it != tags_.end()) ing.items = it->second;
        return ing;
    }
    const auto id = itemIdOr0(n);
    if (id) ing.items.insert(id);
    return ing;
}

Ingredient RecipeManager::makeIngredient(const json::Value& v) const {
    Ingredient ingredient;
    const auto addName = [&](std::string_view name, bool tag) {
        std::string normalized = normalizeResourceName(
            std::string(tag ? "#" : "") + std::string(name));
        const auto resolved = fromName(normalized);
        ingredient.items.insert(resolved.items.begin(), resolved.items.end());
    };

    if (v.isStr()) {
        const auto& name = v.asStr();
        const bool tag = !name.empty() && name.front() == '#';
        addName(tag ? std::string_view(name).substr(1) : name, tag);
    } else if (v.type == json::Value::Type::Arr) {
        for (const auto& alternative : v.arr) {
            const auto resolved = makeIngredient(alternative);
            ingredient.items.insert(resolved.items.begin(), resolved.items.end());
        }
    } else if (v.type == json::Value::Type::Obj) {
        const auto& item = v.at("item");
        const auto& tag = v.at("tag");
        const auto& items = v.at("items");
        if (item.isStr()) addName(item.asStr(), false);
        else if (tag.isStr()) addName(tag.asStr(), true);
        else if (items.type == json::Value::Type::Arr) {
            for (const auto& entry : items.arr) {
                if (!entry.isStr()) continue;
                const auto resolved = makeIngredient(entry);
                ingredient.items.insert(resolved.items.begin(),
                                        resolved.items.end());
            }
        }
    }
    return ingredient;
}

std::vector<std::string> Recipe::trimBlankRows(const std::vector<std::string>& rows) {
    size_t first = 0, last = rows.size();
    while (first < last && rows[first].find_first_not_of(' ') == std::string::npos) ++first;
    while (last > first && rows[last - 1].find_first_not_of(' ') == std::string::npos) --last;
    if (first >= last) return {};
    std::vector<std::string> out(rows.begin() + first, rows.begin() + last);
    size_t w = 0;
    for (auto& r : out) w = std::max(w, r.size());
    for (auto& r : out) if (r.size() < w) r += std::string(w - r.size(), ' ');
    return out;
}
void RecipeManager::syncTagsFrom(const TagManager& tm) {
    auto replacementTags = builtInTags_;
    for (const auto& [name, values] : tm.itemTags)
        replacementTags[name] = values;
    const auto planks = replacementTags.find("minecraft:planks");
    auto replacementPlanks = planks == replacementTags.end()
        ? std::unordered_set<std::uint32_t>{} : planks->second;
    tags_.swap(replacementTags);
    tagPlanks_.swap(replacementPlanks);
}
void RecipeManager::addShaped(const std::string& id, const std::string& outName,
                              int count, const std::vector<std::string>& rows,
                              const std::unordered_map<char, Ingredient>& keys) {
    const auto stackCount = checkedRecipeStackCount(count);
    if (!stackCount) return;
    auto norm = Recipe::trimBlankRows(rows);
    Recipe r;
    r.kind = Recipe::Kind::Shaped;
    r.id = id;
    r.height = static_cast<int>(norm.size());
    r.width = norm.empty() ? 0 : static_cast<int>(norm[0].size());
    if (norm.empty()) return;
    for (const auto& row : norm)
        for (char c : row) {
            if (c == ' ') { r.cells.push_back(Ingredient{}); continue; }
            auto it = keys.find(c);
            r.cells.push_back(it != keys.end() ? it->second : Ingredient{});
        }
    auto it = gen::itemIdByName().find(outName);
    if (it == gen::itemIdByName().end()) return;
    r.result = ItemStack::of(it->second, *stackCount);
    recipes_.push_back(std::move(r));
}

void RecipeManager::addShapeless(const std::string& id, const std::string& outName,
                                 int count, const std::vector<Ingredient>& inputs) {
    const auto stackCount = checkedRecipeStackCount(count);
    if (!stackCount) return;
    Recipe r;
    r.kind = Recipe::Kind::Shapeless;
    r.id = id;
    if (inputs.empty() || std::any_of(inputs.begin(), inputs.end(),
            [](const Ingredient& input) { return input.empty(); }))
        return;
    r.ingredients = inputs;
    auto it = gen::itemIdByName().find(outName);
    if (it == gen::itemIdByName().end()) return;
    r.result = ItemStack::of(it->second, *stackCount);
    recipes_.push_back(std::move(r));
}

void RecipeManager::addSmelting(const Ingredient& input,
                                const std::string& outName,
                                float xp, int ticks, Recipe::Kind kind) {
    const auto out = itemIdOr0(outName);
    if (input.empty() || !out) return;
    Recipe r;
    r.kind = kind;
    r.id = "minecraft:" + outName.substr(10);
    r.cookingTicks = ticks;
    r.experience = xp;
    r.cells.push_back(input);
    r.result = ItemStack::of(out, 1);
    recipes_.push_back(std::move(r));
}

void RecipeManager::addStonecutting(const Ingredient& input,
                                    const std::string& outName, int count) {
    const auto stackCount = checkedRecipeStackCount(count);
    if (!stackCount) return;
    const auto out = itemIdOr0(outName);
    if (input.empty() || !out) return;
    Recipe r;
    r.kind = Recipe::Kind::Stonecutting;
    r.id = "minecraft:" + outName.substr(10);
    r.cells.push_back(input);
    r.result = ItemStack::of(out, *stackCount);
    recipes_.push_back(std::move(r));
}

// ------------------------------------------------------------- built-in set

void RecipeManager::loadDefaults() {
    // tags (clean-room equivalents of the common vanilla item tags)
    auto fillTag = [&](const std::string& name,
                       std::initializer_list<const char*> items) {
        std::unordered_set<std::uint32_t> s;
        for (auto* n : items) {
            const auto id = itemIdOr0(n);
            if (id) s.insert(id);
        }
        builtInTags_["minecraft:" + name] = std::move(s);
    };
    fillTag("planks", {"minecraft:oak_planks", "minecraft:spruce_planks",
                       "minecraft:birch_planks", "minecraft:jungle_planks",
                       "minecraft:acacia_planks", "minecraft:dark_oak_planks",
                       "minecraft:mangrove_planks", "minecraft:cherry_planks",
                       "minecraft:pale_oak_planks", "minecraft:bamboo_planks"});
    fillTag("logs", {"minecraft:oak_log", "minecraft:spruce_log",
                     "minecraft:birch_log", "minecraft:jungle_log",
                     "minecraft:acacia_log", "minecraft:dark_oak_log"});
    fillTag("stone", {"minecraft:stone", "minecraft:granite", "minecraft:diorite",
                      "minecraft:andesite"});
    tags_ = builtInTags_;
    tagPlanks_ = tags_["minecraft:planks"];
    // §3). loadDirectory will populate recipes_; no built-in registration here.
}

// ------------------------------------------------------------------ json io

void RecipeManager::loadDirectory(const std::filesystem::path& dir) {
    datapack_detail::LooseDataBudget budget;
    loadDirectory(dir, budget);
}

void RecipeManager::loadDirectory(
    const std::filesystem::path& dir,
    datapack_detail::LooseDataBudget& budget) {
    RecipeManager staged = *this;
    staged.loadRecipeFiles(dir, {}, budget);
    swap(staged);
}

void RecipeManager::loadDatapackDirectory(
    const std::filesystem::path& dataRoot) {
    datapack_detail::LooseDataBudget budget;
    loadDatapackDirectory(dataRoot, budget);
}

void RecipeManager::loadDatapackDirectory(
    const std::filesystem::path& dataRoot,
    datapack_detail::LooseDataBudget& budget) {
    RecipeManager staged = *this;
    namespace fs = std::filesystem;
    std::error_code error;
    if (!fs::exists(dataRoot, error)) {
        if (error)
            throw fs::filesystem_error("inspect datapack recipe root",
                                       dataRoot, error);
        return;
    }
    if (!fs::is_directory(dataRoot, error) || error)
        throw fs::filesystem_error("inspect datapack recipe root", dataRoot,
            error ? error : std::make_error_code(std::errc::not_a_directory));

    std::vector<fs::path> namespaces;
    for (fs::directory_iterator it(dataRoot, error), end;
         !error && it != end; it.increment(error)) {
        budget.recordDirectoryEntry(it->path());
        std::error_code statusError;
        if (it->is_directory(statusError)) namespaces.push_back(it->path());
        else if (statusError)
            throw fs::filesystem_error("inspect datapack namespace",
                                       it->path(), statusError);
    }
    if (error)
        throw fs::filesystem_error("enumerate datapack namespaces", dataRoot,
                                   error);
    std::sort(namespaces.begin(), namespaces.end(),
        [](const fs::path& lhs, const fs::path& rhs) {
            return pathToUtf8(lhs.filename()) < pathToUtf8(rhs.filename());
        });

    for (const auto& nameSpace : namespaces)
        staged.loadRecipeFiles(nameSpace / "recipe",
                               pathToUtf8(nameSpace.filename()), budget);
    swap(staged);
}

void RecipeManager::loadRecipeFiles(const std::filesystem::path& dir,
                                    const std::string& nameSpace,
                                    datapack_detail::LooseDataBudget& budget) {
    namespace fs = std::filesystem;
    std::error_code error;
    if (!fs::exists(dir, error)) {
        if (error) throw fs::filesystem_error("inspect recipe directory", dir,
                                               error);
        return;
    }
    if (!fs::is_directory(dir, error) || error)
        throw fs::filesystem_error("inspect recipe directory", dir,
            error ? error : std::make_error_code(std::errc::not_a_directory));

    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(dir, error), end;
         !error && it != end; it.increment(error)) {
        budget.recordDirectoryEntry(it->path());
        std::error_code statusError;
        if (it->is_regular_file(statusError) && !statusError &&
            it->path().extension() == ".json") {
            budget.recordFile(it->path());
            files.push_back(it->path());
        } else if (statusError)
            throw fs::filesystem_error("inspect recipe file", it->path(),
                                       statusError);
    }
    if (error)
        throw fs::filesystem_error("enumerate recipe directory", dir, error);
    std::sort(files.begin(), files.end(), [](const fs::path& lhs,
                                             const fs::path& rhs) {
        return genericPathToUtf8(lhs) < genericPathToUtf8(rhs);
    });

    const auto parseIngredientList = [this](const json::Value& values) {
        std::vector<Ingredient> result;
        if (values.type != json::Value::Type::Arr) return result;
        result.reserve(values.arr.size());
        for (const auto& value : values.arr)
            result.push_back(makeIngredient(value));
        return result;
    };

    for (const auto& path : files) {
        const std::string pathText = pathToUtf8(path);
        std::string relative = genericPathToUtf8(path.lexically_relative(dir));
        if (relative.size() <= 5 ||
            relative.compare(relative.size() - 5, 5, ".json") != 0)
            continue;
        relative.resize(relative.size() - 5);
        const std::string recipeId = nameSpace.empty()
            ? normalizeResourceName(relative)
            : nameSpace + ":" + relative;
        std::erase_if(recipes_, [&recipeId](const Recipe& recipe) {
            return recipe.id == recipeId;
        });
        try {
            const std::string text = budget.readFile(path);

            const json::Value document = json::Value::parse(
                text, json::Value::kMaxTotalValues);
            std::string type = document.at("type").isStr()
                ? document.at("type").asStr() : std::string{};
            if (type.rfind("minecraft:", 0) == 0) type.erase(0, 10);
            const std::string group = document.at("group").isStr()
                ? document.at("group").asStr() : std::string{};
            const std::string category = document.at("category").isStr()
                ? document.at("category").asStr() : std::string{};

            int categoryId = 3; // crafting_misc
            if (type.starts_with("crafting_")) {
                if (category == "building") categoryId = 0;
                else if (category == "redstone") categoryId = 1;
                else if (category == "equipment") categoryId = 2;
            } else if (type == "smelting") {
                categoryId = category == "food" ? 4
                    : category == "blocks" ? 5 : 6;
            } else if (type == "blasting") {
                categoryId = category == "blocks" ? 7 : 8;
            } else if (type == "smoking") {
                categoryId = 9;
            } else if (type == "campfire_cooking") {
                categoryId = 12;
            } else if (type == "stonecutting") {
                categoryId = 10;
            } else if (type == "smithing_transform" || type == "smithing_trim") {
                categoryId = 11;
            }

            const json::Value result = document.at("result");
            std::string outputName;
            const auto outputCount = parseRecipeStackCount(result);
            if (result.isStr()) outputName = result.asStr();
            else if (result.type == json::Value::Type::Obj) {
                if (result.at("id").isStr())
                    outputName = result.at("id").asStr();
                else if (result.at("item").isStr())
                    outputName = result.at("item").asStr();
            }
            if (!outputCount) {
                std::fprintf(stderr,
                    "[cppfm] recipe %s skipped: result count must be an integer in 1..%d\n",
                    pathText.c_str(),
                    std::numeric_limits<std::int16_t>::max());
                continue;
            }
            if (!outputName.empty())
                outputName = normalizeResourceName(std::move(outputName));

            const bool special = type == "crafting_special" ||
                                 type == "crafting_decorated_pot";
            if (outputName.empty() && type != "smithing_trim" && !special)
                continue;

            const std::size_t before = recipes_.size();
            if (type == "crafting_shaped") {
                std::vector<std::string> rows;
                const auto& pattern = document.at("pattern");
                if (pattern.type == json::Value::Type::Arr) {
                    for (const auto& row : pattern.arr)
                        if (row.isStr()) rows.push_back(row.asStr());
                }
                rows = Recipe::trimBlankRows(rows);
                if (rows.empty()) continue;

                std::unordered_map<char, Ingredient> keys;
                const auto& keyObject = document.at("key");
                if (keyObject.type != json::Value::Type::Obj) continue;
                for (const auto& [symbol, definition] : keyObject.obj) {
                    if (symbol.size() != 1) continue;
                    keys.emplace(symbol.front(), makeIngredient(definition));
                }
                bool validPattern = true;
                for (const auto& row : rows)
                    for (const char symbol : row)
                        if (symbol != ' ' &&
                            (!keys.contains(symbol) || keys.at(symbol).empty()))
                            validPattern = false;
                if (!validPattern) continue;
                addShaped(recipeId, outputName, *outputCount, rows, keys);
            } else if (type == "crafting_shapeless") {
                const auto inputs = parseIngredientList(
                    document.at("ingredients"));
                addShapeless(recipeId, outputName, *outputCount, inputs);
            } else if (type == "crafting_transmute") {
                std::vector<Ingredient> inputs{
                    makeIngredient(document.at("input")),
                    makeIngredient(document.at("material"))};
                addShapeless(recipeId, outputName, *outputCount, inputs);
            } else if (type == "smelting" || type == "smoking" ||
                       type == "blasting" || type == "campfire_cooking") {
                const auto input = makeIngredient(document.at("ingredient"));
                const int cookingTicks = document.at("cookingtime").asInt(
                    document.at("cookingTime").asInt(200));
                if (cookingTicks <= 0 ||
                    cookingTicks > std::numeric_limits<std::int16_t>::max()) {
                    std::fprintf(stderr,
                        "[cppfm] recipe %s has invalid cookingtime %d\n",
                        pathText.c_str(), cookingTicks);
                    continue;
                }
                addSmelting(input, outputName,
                    static_cast<float>(document.at("experience").asFloat(0.f)),
                    cookingTicks,
                    Recipe::Kind::Smelting);
            } else if (type == "stonecutting") {
                addStonecutting(makeIngredient(document.at("ingredient")),
                                outputName, *outputCount);
            } else if (type == "smithing_transform" ||
                       type == "smithing_trim") {
                std::vector<Ingredient> inputs{
                    makeIngredient(document.at("template")),
                    makeIngredient(document.at("base")),
                    makeIngredient(document.at("addition"))};

                if (type == "smithing_transform") {
                    addShapeless(recipeId, outputName, *outputCount, inputs);
                    if (recipes_.size() > before) {
                        recipes_.back().kind = Recipe::Kind::Smithing;
                        recipes_.back().cells = std::move(inputs);
                    }
                } else {
                    Recipe trim;
                    trim.kind = Recipe::Kind::Smithing;
                    trim.id = recipeId;
                    trim.cells = std::move(inputs);
                    trim.smithingTrimResult = true;
                    recipes_.push_back(std::move(trim));
                }
            } else if (special) {
                Recipe recipe;
                recipe.kind = Recipe::Kind::Special;
                recipe.id = recipeId;
                recipes_.push_back(std::move(recipe));
            } else {
                std::fprintf(stderr,
                    "[cppfm] recipe %s has unsupported type '%s'\n",
                    pathText.c_str(), type.c_str());
                continue;
            }

            if (recipes_.size() > before) {
                auto& recipe = recipes_.back();
                recipe.id = recipeId;
                recipe.group = group;
                recipe.category = categoryId;
                if (recipe.kind == Recipe::Kind::Smelting) {
                    if (type == "blasting")
                        recipe.cookingStation =
                            Recipe::CookingStation::BlastFurnace;
                    else if (type == "smoking")
                        recipe.cookingStation =
                            Recipe::CookingStation::Smoker;
                    else if (type == "campfire_cooking")
                        recipe.cookingStation = Recipe::CookingStation::Campfire;
                }
            }
        } catch (const datapack_detail::LooseDataLimitExceeded&) {
            throw;
        } catch (const json::ResourceLimitExceeded&) {
            throw;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "[cppfm] recipe %s skipped: %s\n",
                         pathText.c_str(), error.what());
        }
    }
}

// --------------------------------------------------------------- lookups

const Recipe* RecipeManager::findCrafting(const std::vector<ItemStack>& grid,
                                          int gw, int gh) const {
    for (const auto& r : recipes_)
        if ((r.kind == Recipe::Kind::Shaped ||
             r.kind == Recipe::Kind::Shapeless) && r.matches(grid, gw, gh))
            return &r;
    return nullptr;
}
const Recipe* RecipeManager::findCooking(
    const std::uint32_t itemId,
    const Recipe::CookingStation station) const {
    for (const auto& r : recipes_)
        if (r.kind == Recipe::Kind::Smelting &&
            r.cookingStation == station && !r.cells.empty() &&
            r.cells.front().accepts(itemId))
            return &r;
    return nullptr;
}
const Recipe* RecipeManager::findStonecutting(std::uint32_t itemId) const {
    for (const auto& r : recipes_)
        if (r.kind == Recipe::Kind::Stonecutting && !r.cells.empty() &&
            r.cells.front().accepts(itemId))
            return &r;
    return nullptr;
}

} // namespace cppfm
