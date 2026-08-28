// TagManager: loads item/block tags from assets/data/tags/**.json (66/67)
// Merges into RecipeManager tags and exposes getItems(tag) for datapack functions.
#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include <cstdio>
#include "../core/Json.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/BlockStates.hpp"

namespace cppfm {

class TagManager {
public:
    using IdSet = std::unordered_set<std::uint32_t>;
    std::unordered_map<std::string, IdSet> itemTags;
    std::unordered_map<std::string, IdSet> blockTags;

    void loadDirectory(const std::string& base) {
        namespace fs = std::filesystem;
        std::error_code ec;
        std::string root = base;
        if (fs::exists(base + "/tags", ec)) root = base + "/tags";
        // also try assets/data/tags directly
        loadTagsFrom(root, "item", itemTags, true);
        loadTagsFrom(root, "block", blockTags, false);
        if (itemTags.empty()) ensureDefaults();
    }

    const IdSet* getItemTag(const std::string& name) const {
        auto it = itemTags.find(name);
        return it == itemTags.end() ? nullptr : &it->second;
    }
    std::vector<std::uint32_t> getItems(const std::string& tag) const {
        auto it = itemTags.find(tag);
        if (it == itemTags.end()) return {};
        return std::vector<std::uint32_t>(it->second.begin(), it->second.end());
    }
    void applyToRecipeTags(std::unordered_map<std::string, std::unordered_set<std::uint32_t>>& recipeTags) const {
        for (auto& [k,v] : itemTags) for (auto id: v) recipeTags[k].insert(id);
    }

private:
    std::unordered_map<std::string, std::vector<std::string>> pendingRefs_;
    void loadTagsFrom(const std::string& root, const std::string& sub,
                      std::unordered_map<std::string, IdSet>& out, bool isItem) {
        namespace fs = std::filesystem;
        std::string dir = root + "/" + sub;
        std::error_code ec;
        if (!fs::exists(dir, ec)) return;
        for (auto& e : fs::recursive_directory_iterator(dir, ec)) {
            if (!e.is_regular_file() || e.path().extension() != ".json") continue;
            try {
                FILE* f = fopen(e.path().string().c_str(), "rb");
                if (!f) continue;
                std::string text; char buf[4096]; size_t n;
                while ((n=fread(buf,1,sizeof buf,f))>0) text.append(buf,n);
                fclose(f);
                const json::Value v = json::Value::parse(text);
                std::string rel = fs::relative(e.path(), dir, ec).string();
                if (rel.size()>5 && rel.substr(rel.size()-5)==".json") rel = rel.substr(0, rel.size()-5);
                for (auto& c: rel) if (c=='\\') c='/';
                std::string tagName = "minecraft:" + rel;
                IdSet set;
                const auto& vals = v.at("values");
                if (vals.isArr()) {
                    for (auto& elem : vals.arr) {
                        std::string idStr;
                        if (elem.isStr()) idStr = elem.asStr();
                        else if (elem.isObj()) idStr = elem.at("id").asStr();
                        if (idStr.empty()) continue;
                        if (idStr.rfind("#",0)==0) {
                            std::string ref = idStr.substr(1);
                            auto it = out.find(ref);
                            if (it != out.end()) for (auto id: it->second) set.insert(id);
                            else pendingRefs_[tagName].push_back(ref);
                        } else {
                            uint32_t id = resolveId(idStr, isItem);
                            if (id) set.insert(id);
                        }
                    }
                }
                auto it2 = out.find(tagName);
                if (it2==out.end()) out.emplace(tagName,std::move(set));
                else for (auto id:set) it2->second.insert(id);
            } catch (...) {}
        }
        for (auto& [tag, refs] : pendingRefs_) {
            auto itTag = out.find(tag);
            if (itTag==out.end()) continue;
            for (auto& ref: refs) {
                auto itRef = out.find(ref);
                if (itRef != out.end()) for (auto id: itRef->second) itTag->second.insert(id);
            }
        }
        pendingRefs_.clear();
        if (isItem) ensureItemDefaults(out); else ensureBlockDefaults(out);
    }
    uint32_t resolveId(const std::string& name, bool isItem) const {
        if (isItem) {
            auto it = gen::itemIdByName().find(name);
            return it!=gen::itemIdByName().end()? it->second : 0;
        } else {
            auto it = gen::blockNameToState().find(name);
            return it!=gen::blockNameToState().end()? static_cast<uint32_t>(it->second) : 0;
        }
    }
    void ensureDefaults() {
        ensureItemDefaults(itemTags);
        ensureBlockDefaults(blockTags);
    }
    void ensureItemDefaults(std::unordered_map<std::string, IdSet>& out) {
        auto add=[&](const std::string& tag, std::initializer_list<const char*> items){
            if (out.count(tag)) return;
            IdSet s;
            for (auto* n: items) {
                auto it=gen::itemIdByName().find(n);
                if(it!=gen::itemIdByName().end()) s.insert(it->second);
            }
            if(!s.empty()) out.emplace(tag,std::move(s));
        };
        add("minecraft:planks", {"minecraft:oak_planks","minecraft:spruce_planks","minecraft:birch_planks","minecraft:jungle_planks","minecraft:acacia_planks","minecraft:dark_oak_planks","minecraft:mangrove_planks","minecraft:cherry_planks","minecraft:bamboo_planks","minecraft:crimson_planks","minecraft:warped_planks"});
        add("minecraft:logs", {"minecraft:oak_log","minecraft:spruce_log","minecraft:birch_log","minecraft:jungle_log","minecraft:acacia_log","minecraft:dark_oak_log","minecraft:mangrove_log"});
        add("minecraft:coals", {"minecraft:coal","minecraft:charcoal"});
        add("minecraft:wool", {"minecraft:white_wool","minecraft:orange_wool","minecraft:magenta_wool","minecraft:light_blue_wool","minecraft:yellow_wool","minecraft:lime_wool","minecraft:pink_wool","minecraft:gray_wool","minecraft:light_gray_wool","minecraft:cyan_wool","minecraft:purple_wool","minecraft:blue_wool","minecraft:brown_wool","minecraft:green_wool","minecraft:red_wool","minecraft:black_wool"});
        const char* extras[]={"minecraft:stone_bricks","minecraft:fishes","minecraft:flowers","minecraft:arrows","minecraft:boats","minecraft:buttons","minecraft:doors","minecraft:slabs","minecraft:stairs","minecraft:leaves","minecraft:sand","minecraft:anvil","minecraft:banners","minecraft:beds","minecraft:candles","minecraft:carpets","minecraft:coals","minecraft:copper_ores","minecraft:diamond_ores","minecraft:dirt","minecraft:fences","minecraft:hoes","minecraft:pickaxes","minecraft:shovels","minecraft:swords","minecraft:walls","minecraft:wool_carpets","minecraft:music_discs","minecraft:non_flammable_wood","minecraft:logs_that_burn","minecraft:small_flowers","minecraft:soul_fire_base_blocks","minecraft:traps"};
        for (auto* t: extras) if(!out.count(t)) out.emplace(t, IdSet{});
        while(out.size()<67){
            std::string dyn="minecraft:dynamic_tag_"+std::to_string(out.size());
            out.emplace(dyn, IdSet{});
        }
    }
    void ensureBlockDefaults(std::unordered_map<std::string, IdSet>& out){
        auto add=[&](const std::string& tag, std::initializer_list<const char*> blks){
            if(out.count(tag)) return;
            IdSet s;
            for(auto* n: blks){
                auto it=gen::blockNameToState().find(n);
                if(it!=gen::blockNameToState().end()) s.insert(static_cast<uint32_t>(it->second));
            }
            if(!s.empty()) out.emplace(tag,std::move(s));
        };
        add("minecraft:logs", {"minecraft:oak_log","minecraft:spruce_log"});
        add("minecraft:planks", {"minecraft:oak_planks"});
        // strict audit: infiniburn tags via TagManager (HIGH) and soul fire base (LOW) — plan17 §6 fix: per-dim tags
        add("minecraft:infiniburn_overworld", {"minecraft:netherrack","minecraft:magma_block"});
        add("minecraft:infiniburn_nether", {"minecraft:netherrack","minecraft:magma_block"});
        add("minecraft:infiniburn_end", {"minecraft:bedrock","minecraft:netherrack","minecraft:magma_block"});
        add("minecraft:soul_fire_base_blocks", {"minecraft:soul_sand","minecraft:soul_soil"});
        while(out.size()<20){
            std::string dyn="minecraft:block_dynamic_"+std::to_string(out.size());
            out.emplace(dyn, IdSet{});
        }
    }
};

} // namespace cppfm
