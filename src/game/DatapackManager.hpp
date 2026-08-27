// DatapackManager: lightweight wrapper over TagManager and LootTableEvaluator.
// Scans assets/data and world/datapacks for datapack JSON. For now it simply
// delegates to TagManager::loadDirectory and LootTableEvaluator::loadDirectory
// and applies tag data to RecipeManager. Full world/datapacks scanning can be
// added incrementally without changing GameServer call sites.
#pragma once
#include <filesystem>
#include <string>
#include "TagManager.hpp"
#include "LootTables.hpp"
#include "Recipes.hpp"

namespace cppfm {

class DatapackManager {
public:
    TagManager tagManager;
    LootTableEvaluator lootTables;

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
        // fallback for worktree layout where assetsBase may be "assets/data/tags"
        if (tagManager.itemTags.empty()) tagManager.loadDirectory("assets/data/tags");
        tagManager.applyToRecipeTags(recipes.tags_);
        lootTables.loadDirectory(assetsBase + "/loot_tables");
        if (lootTables.size() == 0) lootTables.loadDirectory("assets/data/loot_tables");

        // scan world/datapacks/*/data/** if present (stub: just ensure directory walk does not fail)
        if (fs::exists(worldDatapacks, ec)) {
            for (auto& entry : fs::directory_iterator(worldDatapacks, ec)) {
                if (!entry.is_directory(ec)) continue;
                std::string base = entry.path().string() + "/data";
                if (!fs::exists(base, ec)) continue;
                TagManager extra;
                extra.loadDirectory(base + "/tags");
                for (auto& [k,v] : extra.itemTags) for (auto id: v) tagManager.itemTags[k].insert(id);
                tagManager.applyToRecipeTags(recipes.tags_);
                lootTables.loadDirectory(base + "/loot_tables");
            }
        }
    }

    // Apply current tag state to a RecipeManager (idempotent)
    void applyTo(RecipeManager& recipes) const {
        tagManager.applyToRecipeTags(recipes.tags_);
    }
};

} // namespace cppfm
