// Recipes implementation: built-in clean-room recipe table + JSON loader.
#include "Recipes.hpp"
#include <algorithm>
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
    // NOTE: built-in shaped/shapeless/smelting/stonecutting and synthetic filler
    // are now JSON-driven via assets/data/recipes/*.json (plan32 §3).
    // loadDirectory will populate recipes_; no built-in registration here.
}

// ------------------------------------------------------------------ json io

void RecipeManager::loadDirectory(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file() || e.path().extension().string() != ".json") continue;
        files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (auto& path : files) {
        try {
            FILE* f = fopen(path.string().c_str(), "rb");
            if (!f) continue;
            std::string text;
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
            fclose(f);
            const json::Value v = json::Value::parse(text);
            std::string type =
                v.at("type").isStr()
                    ? v.at("type").asStr()
                    : std::string();
            // normalize "minecraft:" prefix per plan18 §8 (vanilla json uses it)
            if (type.rfind("minecraft:",0)==0) type = type.substr(10);
            std::string rid = path.stem().string();
            std::string group = v.at("group").isStr() ? v.at("group").asStr() : "";
            std::string categoryStr = v.at("category").isStr() ? v.at("category").asStr() : "";
            int categoryInt = 3;
            if (type == "smelting" || type == "smoking" || type == "blasting" || type == "campfire_cooking") categoryInt = 6;
            else if (type == "stonecutting") categoryInt = 10;
            else if (categoryStr == "blocks" || categoryStr == "building") categoryInt = 3;
            else if (categoryStr == "equipment") categoryInt = 3;
            else if (categoryStr == "misc") categoryInt = 3;
            else if (categoryStr == "food") categoryInt = 6;
            // result may be string, object {id,count}, or object with id/count; handle all
            const json::Value result = v.at("result");
            std::string outId;
            int outCount = 1;
            if (result.isStr()) outId = result.asStr();
            else if (!result.isNull()) {
                if (result.at("id").isStr()) outId = result.at("id").asStr();
                // some recipes store result as {item: ...} (smithing)
                if (outId.empty() && result.at("item").isStr()) outId = result.at("item").asStr();
                outCount = result.at("count").asInt(1);
                if (outCount==1) outCount = result.at("Count").asInt(1);
            }
            if (outId.empty()) continue;

            auto normalizeName = [](std::string s)->std::string{
                if (s.rfind("minecraft:",0)!=0 && s.rfind("#",0)!=0) {
                    if (s.find(':')==std::string::npos) s = "minecraft:"+s;
                }
                return s;
            };
            outId = normalizeName(outId);

            size_t before = recipes_.size();
            if (type == "crafting_shaped") {
                std::vector<std::string> rows;
                for (auto& rv : v.at("pattern").arr)
                    if (rv.isStr()) rows.push_back(rv.asStr());
                std::unordered_map<char, std::string> keys;
                for (auto& [k, def] : v.at("key").obj) {
                    if (def.isStr()) keys[k[0]] = normalizeName(def.asStr());
                    else {
                        const json::Value& it = def.at("item");
                        const json::Value& tg = def.at("tag");
                        if (it.isStr()) keys[k[0]] = normalizeName(it.asStr());
                        else if (tg.isStr()) {
                            std::string t = tg.asStr();
                            if (t.find(':')==std::string::npos) t = "minecraft:"+t;
                            keys[k[0]] = "#" + t;
                        }
                    }
                }
                addShaped(rid, outId, outCount, rows, keys);
            } else if (type == "crafting_shapeless") {
                std::vector<std::string> inputs;
                for (auto& iv : v.at("ingredients").arr) {
                    if (iv.isStr()) inputs.push_back(normalizeName(iv.asStr()));
                    else if (iv.at("item").isStr())
                        inputs.push_back(normalizeName(iv.at("item").asStr()));
                    else if (iv.at("tag").isStr()) {
                        std::string t = iv.at("tag").asStr();
                        if (t.find(':')==std::string::npos) t = "minecraft:"+t;
                        inputs.push_back("#" + t);
                    }
                }
                addShapeless(rid, outId, outCount, inputs);
            } else if (type == "smelting" || type == "smoking" ||
                       type == "blasting" || type == "campfire_cooking") {
                const json::Value ing = v.at("ingredient");
                std::string name;
                if (ing.isStr()) name = normalizeName(ing.asStr());
                else if (ing.type == json::Value::Type::Arr && !ing.arr.empty()) {
                    // ingredient may be array with one entry
                    const auto &first = ing.arr[0];
                    if (first.isStr()) name = normalizeName(first.asStr());
                    else if (first.at("item").isStr()) name = normalizeName(first.at("item").asStr());
                    else if (first.at("tag").isStr()) name = "#"+first.at("tag").asStr();
                } else if (ing.at("item").isStr()) name = normalizeName(ing.at("item").asStr());
                else if (ing.at("tag").isStr()) {
                    std::string t = ing.at("tag").asStr();
                    if (t.find(':')==std::string::npos) t = "minecraft:"+t;
                    name = "#"+t;
                }
                if (name.empty()) continue;
                addSmelting(name, outId,
                            static_cast<float>(
                                v.at("experience").asFloat(0.f)),
                            v.at("cookingtime").asInt(v.at("cookingTime").asInt(200)),
                            Recipe::Kind::Smelting);
            } else if (type == "stonecutting") {
                const json::Value ing = v.at("ingredient");
                std::string name;
                if (ing.isStr()) name = normalizeName(ing.asStr());
                else if (ing.at("item").isStr()) name = normalizeName(ing.at("item").asStr());
                else if (ing.at("tag").isStr()) {
                    std::string t = ing.at("tag").asStr();
                    if (t.find(':')==std::string::npos) t="minecraft:"+t;
                    name = "#"+t;
                }
                if (name.empty()) continue;
                addStonecutting(name, outId, outCount);
            } else if (type == "smithing_transform" || type == "smithing_trim") {
                std::vector<std::string> inputs;
                auto addIng = [&](const char* field){
                    const json::Value vv = v.at(field);
                    if (vv.isStr()) inputs.push_back(normalizeName(vv.asStr()));
                    else if (vv.at("item").isStr()) inputs.push_back(normalizeName(vv.at("item").asStr()));
                    else if (vv.at("tag").isStr()) inputs.push_back("#"+vv.at("tag").asStr());
                };
                addIng("template"); addIng("base"); addIng("addition");
                if (!inputs.empty()) {
                    // register as Smithing kind with 3 cells for future MenuType::Smithing
                    size_t pre = recipes_.size();
                    addShapeless(rid, outId, outCount, inputs);
                    if (recipes_.size() > pre) {
                        recipes_.back().kind = Recipe::Kind::Smithing;
                        recipes_.back().cells = recipes_.back().ingredients;
                        // keep ingredients for matching fallback as shapeless too
                    }
                }
            } else if (type == "crafting_special" || type == "crafting_transmute" || type == "crafting_decorated_pot") {
                // register as Special (matches false, book display only)
                Recipe r;
                r.kind = Recipe::Kind::Special;
                r.id = rid;
                r.group = group;
                r.category = categoryInt;
                auto it = gen::itemIdByName().find(outId.empty() ? "minecraft:firework_rocket" : outId);
                // for special without result, use firework_rocket as placeholder if outId missing
                std::string ridOut = outId.empty() ? std::string("minecraft:firework_rocket") : outId;
                auto it2 = gen::itemIdByName().find(ridOut);
                if (it2 != gen::itemIdByName().end()) r.result = ItemStack::of(it2->second, outCount);
                else if (it != gen::itemIdByName().end()) r.result = ItemStack::of(it->second, 1);
                recipes_.push_back(std::move(r));
            } else {
                // unknown type -> skip but keep size stable
                std::fprintf(stderr, "[cppfm] recipe %s unknown type '%s' skipped\n",
                             path.string().c_str(), type.c_str());
                continue;
            }
            if (recipes_.size() > before) {
                recipes_.back().id = "minecraft:" + rid;
                recipes_.back().group = group;
                recipes_.back().category = categoryInt;
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[cppfm] recipe %s skipped: %s\n",
                         path.string().c_str(), e.what());
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
