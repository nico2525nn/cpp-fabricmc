// DatapackManager: lightweight wrapper over TagManager and LootTableEvaluator
// plus advancements/predicates/item_modifiers registries and function storage.
// Plan13 §10: supports /datapack list/enable/disable and tab completion for functions.
#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fstream>
#include <cstdio>
#include "TagManager.hpp"
#include "LootTables.hpp"
#include "Recipes.hpp"
#include "../core/Json.hpp"

namespace cppfm {

class DatapackManager {
public:
    TagManager tagManager;
    LootTableEvaluator lootTables;

    // Registries (plan13)
    std::unordered_map<std::string, std::string> advancements;   // id -> raw json
    std::unordered_map<std::string, std::string> predicates;     // id -> raw json
    std::unordered_map<std::string, std::string> itemModifiers;  // id -> raw json
    std::unordered_map<std::string, std::vector<std::string>> functions; // id -> lines

    // pack enable/disable state
    std::unordered_set<std::string> enabledPacks;
    std::unordered_set<std::string> availablePacks;

    DatapackManager() {
        // vanilla always available and enabled
        availablePacks.insert("vanilla");
        enabledPacks.insert("vanilla");
        availablePacks.insert("cppfm");
        enabledPacks.insert("cppfm");
    }

    // Load all datapack content. `assetsBase` is the base directory containing
    // tags/ and loot_tables/ (default "assets/data"). `worldDatapacks` is the
    // per-world datapack root (default "world/datapacks"); scanned if present.
    void loadAll(RecipeManager& recipes,
                 const std::string& assetsBase = "assets/data",
                 const std::string& worldDatapacks = "world/datapacks") {
        namespace fs = std::filesystem;
        std::error_code ec;
        // primary assets
        tagManager.loadDirectory(assetsBase + "/tags");
        if (tagManager.itemTags.empty()) tagManager.loadDirectory("assets/data/tags");
        tagManager.applyToRecipeTags(recipes.tags_);
        lootTables.loadDirectory(assetsBase + "/loot_tables");
        if (lootTables.size() == 0) lootTables.loadDirectory("assets/data/loot_tables");

        // scan assetsBase for advancements/predicates/item_modifiers/functions
        loadPackDirectory(assetsBase, "vanilla");
        // also scan assets/data directly if assetsBase was different
        if (assetsBase != "assets/data") loadPackDirectory("assets/data", "vanilla");

        // scan world/datapacks/*/data/** if present
        if (fs::exists(worldDatapacks, ec)) {
            for (auto& entry : fs::directory_iterator(worldDatapacks, ec)) {
                if (!entry.is_directory(ec)) continue;
                std::string packName = entry.path().filename().string();
                std::string base = entry.path().string() + "/data";
                if (!fs::exists(base, ec)) continue;
                availablePacks.insert(packName);
                // default enable newly discovered packs if not already disabled
                if (enabledPacks.find(packName) == enabledPacks.end()) {
                    // auto-enable unless explicitly disabled before; for simplicity enable
                    enabledPacks.insert(packName);
                }
                TagManager extra;
                extra.loadDirectory(base + "/tags");
                for (auto& [k,v] : extra.itemTags) for (auto id: v) tagManager.itemTags[k].insert(id);
                tagManager.applyToRecipeTags(recipes.tags_);
                lootTables.loadDirectory(base + "/loot_tables");
                loadPackDirectory(base, packName);
            }
        }
        // ensure enabled contains at least vanilla
        if (enabledPacks.empty()) enabledPacks.insert("vanilla");
    }

    void loadPackDirectory(const std::string& base, const std::string& packName) {
        namespace fs = std::filesystem;
        std::error_code ec;
        // advancements: <base>/<ns>/advancements/*.json
        // predicates: <base>/<ns>/predicates/*.json
        // item_modifiers: <base>/<ns>/item_modifiers/*.json
        // functions: <base>/<ns>/functions/*.mcfunction
        if (!fs::exists(base, ec)) return;
        for (auto& nsEntry : fs::directory_iterator(base, ec)) {
            if (!nsEntry.is_directory(ec)) continue;
            std::string ns = nsEntry.path().filename().string();
            // skip tags/loot_tables already handled
            if (ns == "tags" || ns == "loot_tables" || ns == "loot_table") continue;
            std::string advDir = nsEntry.path().string() + "/advancements";
            if (fs::exists(advDir, ec)) scanJsonDir(advDir, ns, advancements, packName);
            std::string advDir2 = nsEntry.path().string() + "/advancement";
            if (fs::exists(advDir2, ec)) scanJsonDir(advDir2, ns, advancements, packName);
            std::string predDir = nsEntry.path().string() + "/predicates";
            if (fs::exists(predDir, ec)) scanJsonDir(predDir, ns, predicates, packName);
            std::string predDir2 = nsEntry.path().string() + "/predicate";
            if (fs::exists(predDir2, ec)) scanJsonDir(predDir2, ns, predicates, packName);
            std::string modDir = nsEntry.path().string() + "/item_modifiers";
            if (fs::exists(modDir, ec)) scanJsonDir(modDir, ns, itemModifiers, packName);
            std::string modDir2 = nsEntry.path().string() + "/item_modifier";
            if (fs::exists(modDir2, ec)) scanJsonDir(modDir2, ns, itemModifiers, packName);
            std::string funcDir = nsEntry.path().string() + "/functions";
            if (fs::exists(funcDir, ec)) scanFunctionDir(funcDir, ns, packName);
            std::string funcDir2 = nsEntry.path().string() + "/function";
            if (fs::exists(funcDir2, ec)) scanFunctionDir(funcDir2, ns, packName);
        }
        // also support flat layout: base/functions etc without ns?
        // check base/advancements flat?
    }

    void scanJsonDir(const std::string& dir, const std::string& ns,
                     std::unordered_map<std::string, std::string>& out,
                     const std::string& packName) {
        namespace fs = std::filesystem;
        std::error_code ec;
        for (auto& e : fs::recursive_directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec) || e.path().extension() != ".json") continue;
            try {
                std::string rel = fs::relative(e.path(), dir, ec).string();
                if (rel.size()>5 && rel.substr(rel.size()-5)==".json") rel = rel.substr(0, rel.size()-5);
                for (auto& c: rel) if (c=='\\') c='/';
                std::string id = ns + ":" + rel;
                // read file
                std::ifstream f(e.path());
                std::string txt((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                out[id] = txt;
                (void)packName;
            } catch (...) {}
        }
    }

    void scanFunctionDir(const std::string& dir, const std::string& ns,
                         const std::string& packName) {
        namespace fs = std::filesystem;
        std::error_code ec;
        for (auto& e : fs::recursive_directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec) || e.path().extension() != ".mcfunction") continue;
            try {
                std::string rel = fs::relative(e.path(), dir, ec).string();
                if (rel.size()>11 && rel.substr(rel.size()-11)==".mcfunction") rel = rel.substr(0, rel.size()-11);
                for (auto& c: rel) if (c=='\\') c='/';
                std::string id = ns + ":" + rel;
                std::ifstream f(e.path());
                std::vector<std::string> lines;
                std::string line;
                while (std::getline(f,line)) {
                    // trim?
                    lines.push_back(line);
                }
                functions[id] = std::move(lines);
                (void)packName;
            } catch (...) {}
        }
    }

    // Also load a single function file on demand if not yet loaded (for tab completion fallback)
    std::vector<std::string> getFunctionIds() const {
        std::vector<std::string> out;
        out.reserve(functions.size());
        for (auto& kv: functions) out.push_back(kv.first);
        // ensure at least vanilla tick
        if (out.empty()) {
            out.push_back("minecraft:tick");
            out.push_back("minecraft:load");
        }
        return out;
    }

    const std::vector<std::string>* getFunction(const std::string& id) const {
        auto it = functions.find(id);
        if (it != functions.end()) return &it->second;
        // try with minecraft: prefix
        if (id.find(':')==std::string::npos) {
            auto it2 = functions.find("minecraft:"+id);
            if (it2 != functions.end()) return &it2->second;
        }
        return nullptr;
    }

    // Apply current tag state to a RecipeManager (idempotent)
    void applyTo(RecipeManager& recipes) const {
        tagManager.applyToRecipeTags(recipes.tags_);
    }

    // Pack management (plan13)
    std::vector<std::string> listAvailable() const {
        std::vector<std::string> v(availablePacks.begin(), availablePacks.end());
        std::sort(v.begin(), v.end());
        return v;
    }
    std::vector<std::string> listEnabled() const {
        std::vector<std::string> v(enabledPacks.begin(), enabledPacks.end());
        std::sort(v.begin(), v.end());
        return v;
    }
    bool isEnabled(const std::string& name) const {
        return enabledPacks.find(name) != enabledPacks.end();
    }
    bool isAvailable(const std::string& name) const {
        return availablePacks.find(name) != availablePacks.end();
    }
    bool enablePack(const std::string& name) {
        if (!isAvailable(name)) {
            // allow enabling unknown as file pack (create)
            availablePacks.insert(name);
        }
        auto [it, inserted] = enabledPacks.insert(name);
        return inserted;
    }
    bool disablePack(const std::string& name) {
        if (enabledPacks.find(name) == enabledPacks.end()) return false;
        if (name == "vanilla") return false; // cannot disable vanilla
        enabledPacks.erase(name);
        return true;
    }

    size_t advancementCount() const { return advancements.size(); }
    size_t predicateCount() const { return predicates.size(); }
    size_t itemModifierCount() const { return itemModifiers.size(); }

    // Evaluate predicate (stub: checks if predicate exists, returns true if no conditions)
    bool testPredicate(const std::string& id) const {
        auto it = predicates.find(id);
        if (it == predicates.end()) {
            if (id.find(':')==std::string::npos) {
                auto it2 = predicates.find("minecraft:"+id);
                if (it2 != predicates.end()) return true;
            }
            return false;
        }
        // For now, if JSON contains "condition": check simple? stub returns true
        return true;
    }

    // Apply item modifier (stub: returns true)
    bool applyItemModifier(const std::string& id, class ItemStack& stack) const {
        (void)id; (void)stack;
        return isAvailable(id) || predicates.find(id)!=predicates.end() || itemModifiers.find(id)!=itemModifiers.end();
    }
};

} // namespace cppfm
