// Recipes: data-driven crafting/smelting recipes loaded from JSON
// (plan3.md "クラフトレシピのアーキテクチャ").
//
// Supported JSON shapes (authored clean-room under assets/data/recipes/):
//   { "type":"crafting_shaped", "pattern":["XX","XX"], "key":{"X":{"item":...}},
//     "result":{"id":..., "count":n} }
//   { "type":"crafting_shapeless", "ingredients":[{...},...], "result":{...} }
//   { "type":"smelting"|"smoking"|"blasting"|"campfire_cooking",
//     "ingredient":{...}, "result":{"id":...}, "cookingtime":t,
//     "experience":x }
//   { "type":"stonecutting", "ingredient":{...}, "result":{"id":...,"count":n} }
// Ingredients may reference tags ("tag": "minecraft:planks") resolved via a
// built-in tag table.
#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../core/Json.hpp"
#include "../generated/ItemIds.hpp"
#include "Items.hpp"

namespace cppfm {

struct Ingredient {
    std::unordered_set<std::uint32_t> items;   // accepted item ids (empty = any)

    bool empty() const { return items.empty(); }
    bool accepts(std::uint32_t itemId) const {
        return !items.empty() && items.count(itemId) != 0;
    }
    std::string describe() const {
        if (items.empty()) return "(none)";
        for (auto id : items) {
            for (auto& e : gen::kItems)
                if (e.second == id) return std::string(e.first);
            break;
        }
        return "?";
    }
};

class Recipe {
public:
    enum class Kind { Shaped, Shapeless, Smelting, Stonecutting, Smithing, Special };
    Kind kind;
    std::string id;                              // e.g. minecraft:oak_planks
    std::string group;                           // recipe group (wire varint, 0=none)
    int category = 3;                            // wire category: 3 crafting / 6 furnace / 10 stonecutter
    // shaped
    int width = 0, height = 0;
    std::vector<Ingredient> cells;               // width*height, row-major
    // shapeless
    std::vector<Ingredient> ingredients;
    // cooking
    int cookingTicks = 200;
    float experience = 0.f;

    ItemStack result;

    // Match a crafting grid (2x2 or 3x3) of stacks; returns true + result.
    bool matches(const std::vector<ItemStack>& grid, int gw, int gh) const {
        if (kind == Kind::Shaped) return matchShaped(grid, gw, gh);
        if (kind == Kind::Shapeless) return matchShapeless(grid);
        if (kind == Kind::Stonecutting) {
            int filled = 0; ItemStack only;
            for (auto& s : grid)
                if (!s.empty()) { ++filled; only = s; }
            if (filled != 1) return false;
            return !result.empty() && ingredientAccepts(cells.empty()
                        ? Ingredient{} : cells.front(), only.itemId);
        }
        return false;                            // smelting matched separately
    }

    static bool ingredientAccepts(const Ingredient& ing, std::uint32_t itemId) {
        return ing.accepts(itemId);
    }
    // plan37 B-03: trim blank rows helper
    static std::vector<std::string> trimBlankRows(const std::vector<std::string>& rows);

private:
    // Checks pattern placement at (ox,oy) with every grid cell outside the
    // pattern box required to be empty (vanilla shaped semantics).
    bool fitsVariant(const std::vector<ItemStack>& grid, int gw, int gh,
                     int ox, int oy, bool mirrored) const {
        for (int gy = 0; gy < gh; ++gy)
            for (int gx = 0; gx < gw; ++gx) {
                const auto& cell = grid[static_cast<std::size_t>(gy) *
                                        static_cast<std::size_t>(gw) + gx];
                const bool inside = gx >= ox && gx < ox + width &&
                                    gy >= oy && gy < oy + height;
                if (!inside) {
                    if (!cell.empty()) return false;
                    continue;
                }
                const int px = mirrored ? (width - 1 - (gx - ox)) : (gx - ox);
                const auto& ing = cells[static_cast<std::size_t>(gy - oy) * width + px];
                if (ing.empty()) { if (!cell.empty()) return false; }
                else if (cell.empty() || !ing.accepts(cell.itemId)) return false;
            }
        return true;
    }
    bool matchShaped(const std::vector<ItemStack>& grid, int gw, int gh) const {
        if (width > gw || height > gh) return false;
        // plan37 B-03: triple loop oy->ox->mirrored (vanilla ShapedRecipe#matches order)
        for (int oy = 0; oy <= gh - height; ++oy)
            for (int ox = 0; ox <= gw - width; ++ox)
                for (bool mirrored : {false, true})
                    if (fitsVariant(grid, gw, gh, ox, oy, mirrored)) return true;
        return false;
    }
    bool matchShapeless(const std::vector<ItemStack>& grid) const {
        std::vector<const ItemStack*> present;
        for (auto& s : grid) if (!s.empty()) present.push_back(&s);
        if (present.size() != ingredients.size()) return false;
        std::vector<bool> used(present.size(), false);
        for (auto& ing : ingredients) {
            bool found = false;
            for (std::size_t i = 0; i < present.size(); ++i) {
                if (used[i]) continue;
                if (ing.accepts(present[i]->itemId)) { used[i] = found = true; break; }
            }
            if (!found) return false;
        }
        return true;
    }
};

class RecipeManager {
public:
    void loadDefaults();                         // built-in table (see Recipes.cpp)
    void loadDirectory(const std::string& dir);  // assets/data/recipes/*.json

    // Find first crafting recipe matching the grid (gw/gh: 2 or 3).
    const Recipe* findCrafting(const std::vector<ItemStack>& grid, int gw, int gh) const;
    // Cooking recipes keyed by input item.
    const Recipe* findSmelting(std::uint32_t itemId) const;
    const Recipe* findStonecutting(std::uint32_t itemId) const;

    std::size_t size() const { return recipes_.size(); }
    const std::vector<Recipe>& all() const { return recipes_; }
    const std::unordered_set<std::uint32_t>& planksTag() const { return tagPlanks_; }
    // plan41 C-11: test helper — lookup by full id (minecraft:xxx)
    const Recipe* findById(const std::string& id) const {
        for (auto& r : recipes_) if (r.id == id) return &r;
        return nullptr;
    }

private:
    void addShaped(const std::string& id, const std::string& outName, int count,
                   const std::vector<std::string>& rows,
                   const std::unordered_map<char, std::string>& keys);
    void addShapeless(const std::string& id, const std::string& outName, int count,
                      const std::vector<std::string>& inputs);
    void addSmelting(const std::string& inName, const std::string& outName,
                     float xp, int ticks, Recipe::Kind kind);
    void addStonecutting(const std::string& inName, const std::string& outName, int count);

    Ingredient makeIngredient(const json::Value& v) const;
    Ingredient fromName(const std::string& n) const;

    std::vector<Recipe> recipes_;

protected:
    std::uint32_t itemIdOr0(const std::string& name) const {
        if (name.rfind('#', 0) == 0) return 0;       // tag marker handled elsewhere
        auto it = gen::itemIdByName().find(name);
        return it != gen::itemIdByName().end() ? it->second : 0;
    }

public:
    // plan37 B-03: tag sync with TagManager (double sync design)
    void syncTagsFrom(const class TagManager& tm);

    // Tag expansion used by both JSON loading and the built-in table.
    std::unordered_map<std::string, std::unordered_set<std::uint32_t>> tags_;
    std::unordered_set<std::uint32_t> tagPlanks_;
};

} // namespace cppfm
