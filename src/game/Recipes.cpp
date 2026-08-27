// Recipes implementation: built-in clean-room recipe table + JSON loader.
#include "Recipes.hpp"
#include <filesystem>
#include <cstdio>

namespace cppfm {

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
    if (v.isStr()) return fromName(v.asStr());
    const json::Value empty;
    const json::Value& item = v.type == json::Value::Type::Obj ? v.at("item") : empty;
    const json::Value& tag = v.type == json::Value::Type::Obj ? v.at("tag") : empty;
    if (!item.isNull()) return fromName(item.asStr());
    if (!tag.isNull()) return fromName("#" + tag.asStr());
    return {};
}

void RecipeManager::addShaped(const std::string& id, const std::string& outName,
                              int count, const std::vector<std::string>& rows,
                              const std::unordered_map<char, std::string>& keys) {
    Recipe r;
    r.kind = Recipe::Kind::Shaped;
    r.id = id;
    r.height = static_cast<int>(rows.size());
    r.width = rows.empty() ? 0 : static_cast<int>(rows[0].size());
    for (const auto& row : rows)
        for (char c : row) {
            if (c == ' ') { r.cells.push_back(Ingredient{}); continue; }
            auto it = keys.find(c);
            r.cells.push_back(it != keys.end() ? fromName(it->second) : Ingredient{});
        }
    auto it = gen::itemIdByName().find(outName);
    if (it == gen::itemIdByName().end()) return;
    r.result = ItemStack::of(it->second, static_cast<std::int16_t>(count));
    recipes_.push_back(std::move(r));
}

void RecipeManager::addShapeless(const std::string& id, const std::string& outName,
                                 int count, const std::vector<std::string>& inputs) {
    Recipe r;
    r.kind = Recipe::Kind::Shapeless;
    r.id = id;
    for (auto& n : inputs) r.ingredients.push_back(fromName(n));
    auto it = gen::itemIdByName().find(outName);
    if (it == gen::itemIdByName().end()) return;
    r.result = ItemStack::of(it->second, static_cast<std::int16_t>(count));
    recipes_.push_back(std::move(r));
}

void RecipeManager::addSmelting(const std::string& inName, const std::string& outName,
                                float xp, int ticks, Recipe::Kind kind) {
    auto in = itemIdOr0(inName), out = itemIdOr0(outName);
    if (!in || !out) return;
    Recipe r;
    r.kind = kind;
    r.id = "minecraft:" + outName.substr(10);
    r.cookingTicks = ticks;
    r.experience = xp;
    Ingredient i; i.items.insert(in);
    r.cells.push_back(std::move(i));
    r.result = ItemStack::of(out, 1);
    recipes_.push_back(std::move(r));
}

void RecipeManager::addStonecutting(const std::string& inName,
                                    const std::string& outName, int count) {
    auto in = itemIdOr0(inName), out = itemIdOr0(outName);
    if (!in || !out) return;
    Recipe r;
    r.kind = Recipe::Kind::Stonecutting;
    r.id = "minecraft:" + outName.substr(10);
    Ingredient i; i.items.insert(in);
    r.cells.push_back(i);
    r.result = ItemStack::of(out, static_cast<std::int16_t>(count));
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
        tags_["minecraft:" + name] = std::move(s);
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
    tagPlanks_ = tags_["minecraft:planks"];

    // ---- planks / sticks / basics -----------------------------------------
    for (auto id : tags_["minecraft:logs"]) {
        // log -> 4 planks of matching kind is data-driven in vanilla; we map
        // every log to oak planks family via per-log entries below instead.
        (void)id;
    }
    addShapeless("minecraft:oak_planks", "minecraft:oak_planks", 4,
                 {"#minecraft:logs"});
    addShaped("minecraft:stick", "minecraft:stick", 4,
              {"P", "P"}, {{'P', "#minecraft:planks"}});
    addShaped("minecraft:crafting_table", "minecraft:crafting_table", 1,
              {"PP", "PP"}, {{'P', "#minecraft:planks"}});
    addShaped("minecraft:furnace", "minecraft:furnace", 1,
              {"CCC", "C C", "CCC"}, {{'C', "minecraft:cobblestone"}});
    addShaped("minecraft:chest", "minecraft:chest", 1,
              {"PPP", "P P", "PPP"}, {{'P', "#minecraft:planks"}});
    addShaped("minecraft:torch", "minecraft:torch", 4,
              {"C", "S"}, {{'C', "minecraft:coal"}, {'S', "minecraft:stick"}});

    // ---- tools -------------------------------------------------------------
    const char* heads[][2] = {
        {"pickaxe", "minecraft:cobblestone"}, {"axe", "minecraft:cobblestone"},
        {"shovel", "minecraft:cobblestone"}, {"hoe", "minecraft:cobblestone"}};
    (void)heads;
    auto toolSet = [&](const char* kind, const char* material,
                       const char* outPrefix) {
        std::string m(material);
        std::string base = std::string(outPrefix);
        if (std::string(kind) == "wooden") m = "#minecraft:planks";
        if (std::string(kind) == "stone") m = "minecraft:cobblestone";
        if (std::string(kind) == "iron") m = "minecraft:iron_ingot";
        if (std::string(kind) == "golden") m = "minecraft:gold_ingot";
        if (std::string(kind) == "diamond") m = "minecraft:diamond";
        addShaped(base + kind + "_pickaxe", base + kind + "_pickaxe", 1,
                  {"MMM", " S ", " S "},
                  {{'M', m}, {'S', "minecraft:stick"}});
        addShaped(base + kind + "_axe", base + kind + "_axe", 1,
                  {"MM", "MS", " S"},
                  {{'M', m}, {'S', "minecraft:stick"}});
        addShaped(base + kind + "_shovel", base + kind + "_shovel", 1,
                  {"M", "S", "S"}, {{'M', m}, {'S', "minecraft:stick"}});
        addShaped(base + kind + "_sword", base + kind + "_sword", 1,
                  {"M", "M", "S"}, {{'M', m}, {'S', "minecraft:stick"}});
    };
    toolSet("wooden", "", "minecraft:");
    toolSet("stone", "", "minecraft:");
    toolSet("iron", "minecraft:iron_ingot", "minecraft:");
    toolSet("golden", "minecraft:gold_ingot", "minecraft:");
    toolSet("diamond", "minecraft:diamond", "minecraft:");

    // ---- combat / utility --------------------------------------------------
    addShaped("minecraft:shield", "minecraft:shield", 1,
              {"PIP", "PPP", " P "},
              {{'P', "#minecraft:planks"}, {'I', "minecraft:iron_ingot"}});
    addShapeless("minecraft:bread", "minecraft:bread", 1,
                 {"minecraft:wheat", "minecraft:wheat", "minecraft:wheat"});
    addSmelting("minecraft:sand", "minecraft:glass", 0.1f, 200,
                Recipe::Kind::Smelting);
    addSmelting("minecraft:iron_ore", "minecraft:iron_ingot", 0.7f, 200,
                Recipe::Kind::Smelting);
    addSmelting("minecraft:gold_ore", "minecraft:gold_ingot", 1.f, 200,
                Recipe::Kind::Smelting);
    addSmelting("minecraft:copper_ore", "minecraft:copper_ingot", 0.7f, 200,
                Recipe::Kind::Smelting);
    addSmelting("minecraft:ancient_debris", "minecraft:netherite_scrap", 2.f, 200,
                Recipe::Kind::Smelting);
    addSmelting("minecraft:raw_iron", "minecraft:iron_ingot", 0.7f, 200,
                Recipe::Kind::Smelting);
    addSmelting("minecraft:beef", "minecraft:cooked_beef", 0.35f, 200,
                Recipe::Kind::Smelting);
    addSmelting("minecraft:porkchop", "minecraft:cooked_porkchop", 0.35f, 200,
                Recipe::Kind::Smelting);
    addSmelting("minecraft:chicken", "minecraft:cooked_chicken", 0.35f, 200,
                Recipe::Kind::Smelting);
    addSmelting("minecraft:mutton", "minecraft:cooked_mutton", 0.35f, 200,
                Recipe::Kind::Smelting);
    addSmelting("minecraft:potato", "minecraft:baked_potato", 0.35f, 200,
                Recipe::Kind::Smelting);

    addShaped("minecraft:iron_block", "minecraft:iron_block", 1,
              {"III", "III", "III"}, {{'I', "minecraft:iron_ingot"}});
    addShapeless("minecraft:iron_ingot_from_block", "minecraft:iron_ingot", 9,
                 {"minecraft:iron_block"});
    addShaped("minecraft:gold_block", "minecraft:gold_block", 1,
              {"GGG", "GGG", "GGG"}, {{'G', "minecraft:gold_ingot"}});
    addShaped("minecraft:diamond_block", "minecraft:diamond_block", 1,
              {"DDD", "DDD", "DDD"}, {{'D', "minecraft:diamond"}});
    addShapeless("minecraft:diamond", "minecraft:diamond", 9,
                 {"minecraft:diamond_block"});

    // stonecutters (expanded for ghost recipe tests)
    addStonecutting("minecraft:stone", "minecraft:stone_bricks", 1);
    addStonecutting("minecraft:stone", "minecraft:stone_slab", 2);
    addStonecutting("minecraft:stone", "minecraft:stone_stairs", 1);
    addStonecutting("minecraft:cobblestone", "minecraft:stone", 1);
    addStonecutting("minecraft:cobblestone", "minecraft:cobblestone_slab", 2);
    addStonecutting("minecraft:cobblestone", "minecraft:cobblestone_stairs", 1);
    addStonecutting("minecraft:oak_planks", "minecraft:oak_slab", 2);
    addStonecutting("minecraft:oak_planks", "minecraft:oak_stairs", 1);
    addStonecutting("minecraft:quartz_block", "minecraft:quartz_slab", 2);
    addStonecutting("minecraft:quartz_block", "minecraft:quartz_stairs", 1);
}

// ------------------------------------------------------------------ json io

void RecipeManager::loadDirectory(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file() ||
            entry.path().extension().string() != ".json")
            continue;
        try {
            FILE* f = fopen(entry.path().string().c_str(), "rb");
            if (!f) continue;
            std::string text;
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
            fclose(f);
            const json::Value v = json::Value::parse(text);
            const std::string type =
                v.at("type").isStr()
                    ? v.at("type").asStr()
                    : std::string();
            std::string rid = entry.path().stem().string();
            const json::Value result = v.at("result");
            const std::string outId =
                result.at("id").isStr()
                    ? result.at("id").asStr()
                    : (result.isStr() ? result.asStr() : std::string());
            const int outCount =
                result.at("count").asInt(1);
            if (outId.empty()) continue;

            if (type == "crafting_shaped") {
                std::vector<std::string> rows;
                for (auto& rv : v.at("pattern").arr)
                    if (rv.isStr()) rows.push_back(rv.asStr());
                std::unordered_map<char, std::string> keys;
                for (auto& [k, def] : v.at("key").obj)
                    if (def.isStr()) keys[k[0]] = def.asStr();
                    else {
                        const json::Value& it = def.at("item");
                        const json::Value& tg = def.at("tag");
                        if (it.isStr()) keys[k[0]] = it.asStr();
                        else if (tg.isStr()) keys[k[0]] = "#" + tg.asStr();
                    }
                addShaped(rid, outId, outCount, rows, keys);
            } else if (type == "crafting_shapeless") {
                std::vector<std::string> inputs;
                for (auto& iv : v.at("ingredients").arr) {
                    if (iv.isStr()) inputs.push_back(iv.asStr());
                    else if (iv.at("item").isStr())
                        inputs.push_back(iv.at("item").asStr());
                    else if (iv.at("tag").isStr())
                        inputs.push_back("#" + iv.at("tag").asStr());
                }
                addShapeless(rid, outId, outCount, inputs);
            } else if (type == "smelting" || type == "smoking" ||
                       type == "blasting" || type == "campfire_cooking") {
                const json::Value ing = v.at("ingredient");
                const std::string name =
                    ing.isStr() ? ing.asStr() : ing.at("item").asStr();
                addSmelting(name, outId,
                            static_cast<float>(
                                v.at("experience").asFloat(0.f)),
                            v.at("cookingtime").asInt(200),
                            Recipe::Kind::Smelting);
            } else if (type == "stonecutting") {
                const json::Value ing = v.at("ingredient");
                const std::string name =
                    ing.isStr() ? ing.asStr() : ing.at("item").asStr();
                addStonecutting(name, outId, outCount);
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[cppfm] recipe %s skipped: %s\n",
                         entry.path().string().c_str(), e.what());
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
const Recipe* RecipeManager::findSmelting(std::uint32_t itemId) const {
    for (const auto& r : recipes_)
        if (r.kind == Recipe::Kind::Smelting && !r.cells.empty() &&
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
