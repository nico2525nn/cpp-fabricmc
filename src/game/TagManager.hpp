// TagManager: loads item/block tags from assets/data/tags/**.json and supplies
// a small semantic fallback set when a checkout has no vanilla tag resources.
// Merges into RecipeManager tags and exposes getItems(tag) for datapack functions.
#pragma once
#include <algorithm>
#include <deque>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include <cstdio>
#include "../core/Json.hpp"
#include "../core/Utf8.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/BlockStates.hpp"
#include "MobEffects.hpp"
#include "DatapackResourceBudget.hpp"

namespace cppfm {

class TagManager {
public:
    using IdSet = std::unordered_set<std::uint32_t>;
    using NamespacedTagReferences =
        std::unordered_map<std::string,
            std::unordered_map<std::string, bool>>;

    std::unordered_map<std::string, IdSet> itemTags;
    std::unordered_map<std::string, IdSet> blockTags;
    std::unordered_map<std::string, IdSet> statusEffectTags;
    std::unordered_map<std::string, std::unordered_set<std::string>> biomeTags;

    void loadDirectory(const std::string& base) {
        loadDirectoryPath(std::filesystem::path(base));
    }

    // Filesystem-native entry point for callers that already hold a path.
    // Unlike the legacy string overload, this never round-trips through the
    // process locale or Windows active code page.
    void loadDirectoryPath(const std::filesystem::path& base) {
        datapack_detail::LooseDataBudget budget;
        loadDirectoryPath(base, budget);
    }

    void loadDirectoryPath(const std::filesystem::path& base,
                           datapack_detail::LooseDataBudget& budget) {
        TagManager staged;
        staged.loadDirectoryPathContents(base, budget);
        using std::swap;
        swap(itemTags, staged.itemTags);
        swap(blockTags, staged.blockTags);
        swap(statusEffectTags, staged.statusEffectTags);
        swap(biomeTags, staged.biomeTags);
        swap(pendingRefs_, staged.pendingRefs_);
    }

    // Merge the registry tags supported by this server from a Java 1.21 data
    // pack root (`data/<namespace>/tags/...`). This is distinct from the
    // built-in assets layout (`tags/item/...`) handled by loadDirectory().
    void mergeDataPackDirectory(const std::string& dataRoot) {
        mergeDataPackDirectories({dataRoot});
    }

    void mergeDataPackDirectories(const std::vector<std::string>& dataRoots) {
        std::vector<std::filesystem::path> nativeRoots;
        nativeRoots.reserve(dataRoots.size());
        for (const auto& root : dataRoots)
            nativeRoots.emplace_back(root);
        mergeDataPackDirectoriesPath(nativeRoots);
    }

    // Filesystem-native API for UTF-8-safe datapack roots. Suggested manager
    // integration: mergeDataPackDirectoriesPath(vector<filesystem::path>).
    void mergeDataPackDirectoriesPath(
        const std::vector<std::filesystem::path>& dataRoots) {
        datapack_detail::LooseDataBudget budget;
        mergeDataPackDirectoriesPath(dataRoots, budget);
    }

    void mergeDataPackDirectoriesPath(
        const std::vector<std::filesystem::path>& dataRoots,
        datapack_detail::LooseDataBudget& budget) {
        namespace fs = std::filesystem;
        auto mergedItemTags = itemTags;
        auto mergedBlockTags = blockTags;
        auto mergedStatusEffectTags = statusEffectTags;
        auto mergedBiomeTags = biomeTags;
        if (budget.resolvedTagEntries == 0) {
            recordExistingTagEntries(mergedItemTags, budget);
            recordExistingTagEntries(mergedBlockTags, budget);
            recordExistingTagEntries(mergedStatusEffectTags, budget);
            recordExistingTagEntries(mergedBiomeTags, budget);
        }
        NamespacedTagReferences itemReferences;
        NamespacedTagReferences blockReferences;
        NamespacedTagReferences statusEffectReferences;
        NamespacedTagReferences biomeReferences;
        for (const auto& dataRoot : dataRoots) {
            std::error_code error;
            if (!fs::exists(dataRoot, error)) {
                if (error)
                    throw fs::filesystem_error("inspect datapack data root",
                                               dataRoot, error);
                continue;
            }
            if (!fs::is_directory(dataRoot, error) || error)
                throw fs::filesystem_error("inspect datapack data root",
                    dataRoot, error ? error : std::make_error_code(
                        std::errc::not_a_directory));
            std::vector<fs::directory_entry> namespaces;
            fs::directory_iterator it(dataRoot, error), end;
            if (error)
                throw fs::filesystem_error("scan datapack data root",
                                           dataRoot, error);
            while (it != end) {
                budget.recordDirectoryEntry(it->path());
                std::error_code statusError;
                if (it->is_directory(statusError) && !statusError)
                    namespaces.push_back(*it);
                it.increment(error);
                if (error)
                    throw fs::filesystem_error("scan datapack data root",
                                               dataRoot, error);
            }
            std::sort(namespaces.begin(), namespaces.end(),
                [](const fs::directory_entry& lhs,
                   const fs::directory_entry& rhs) {
                    return utf8::pathToUtf8(lhs.path().filename()) <
                           utf8::pathToUtf8(rhs.path().filename());
                });
            for (const auto& nameSpace : namespaces) {
                const auto tags = nameSpace.path() / "tags";
                const auto name = utf8::pathToUtf8(
                    nameSpace.path().filename());
                mergeNamespacedTagsFrom(tags / "item", name, mergedItemTags,
                    itemReferences, budget, [this](const std::string& id) {
                        const auto resolved = resolveId(id, true);
                        return !resolved
                            ? std::vector<std::uint32_t>{}
                            : std::vector<std::uint32_t>{*resolved};
                    });
                mergeNamespacedTagsFrom(tags / "block", name, mergedBlockTags,
                    blockReferences, budget, [this](const std::string& id) {
                        const auto resolved = resolveId(id, false);
                        return !resolved
                            ? std::vector<std::uint32_t>{}
                            : std::vector<std::uint32_t>{*resolved};
                    });
                mergeNamespacedTagsFrom(
                    tags / "mob_effect", name, mergedStatusEffectTags,
                    statusEffectReferences, budget, [this](const std::string& id) {
                        const auto resolved = resolveId(id, false, true);
                        return !resolved
                            ? std::vector<std::uint32_t>{}
                            : std::vector<std::uint32_t>{*resolved};
                    });
                mergeNamespacedTagsFrom(
                    tags / "status_effect", name, mergedStatusEffectTags,
                    statusEffectReferences, budget, [this](const std::string& id) {
                        const auto resolved = resolveId(id, false, true);
                        return !resolved
                            ? std::vector<std::uint32_t>{}
                            : std::vector<std::uint32_t>{*resolved};
                    });
                mergeNamespacedTagsFrom(
                    tags / "worldgen" / "biome", name, mergedBiomeTags,
                    biomeReferences, budget, [](const std::string& id) {
                        return std::vector<std::string>{id};
                    });
            }
        }
        resolveNamespacedTagReferences(mergedItemTags, itemReferences, budget);
        resolveNamespacedTagReferences(mergedBlockTags, blockReferences,
                                       budget);
        resolveNamespacedTagReferences(mergedStatusEffectTags,
                                       statusEffectReferences, budget);
        resolveNamespacedTagReferences(mergedBiomeTags, biomeReferences,
                                       budget);
        itemTags.swap(mergedItemTags);
        blockTags.swap(mergedBlockTags);
        statusEffectTags.swap(mergedStatusEffectTags);
        biomeTags.swap(mergedBiomeTags);
    }

    const IdSet* getItemTag(const std::string& name) const {
        auto it = itemTags.find(name);
        return it == itemTags.end() ? nullptr : &it->second;
    }
    const IdSet* getStatusEffectTag(const std::string& name) const {
        const auto found = statusEffectTags.find(name);
        return found == statusEffectTags.end() ? nullptr : &found->second;
    }
    std::vector<std::uint32_t> getItems(const std::string& tag) const {
        auto it = itemTags.find(tag);
        if (it == itemTags.end()) return {};
        return std::vector<std::uint32_t>(it->second.begin(), it->second.end());
    }
    void applyToRecipeTags(std::unordered_map<std::string, std::unordered_set<std::uint32_t>>& recipeTags) const {
        for (auto& [k,v] : itemTags) for (auto id: v) recipeTags[k].insert(id);
    }
    bool isBlockInTag(const std::string& tag, const std::string& blockName) const {
        auto it = blockTags.find(tag);
        if(it==blockTags.end()) return false;
        auto nit = gen::blockNameToState().find(blockName);
        if(nit==gen::blockNameToState().end()) return false;
        return it->second.count(static_cast<std::uint32_t>(nit->second))>0;
    }

private:
    std::unordered_map<std::string, std::vector<std::string>> pendingRefs_;

    void loadDirectoryPathContents(
        const std::filesystem::path& base,
        datapack_detail::LooseDataBudget& budget) {
        namespace fs = std::filesystem;
        std::error_code ec;
        itemTags.clear();
        blockTags.clear();
        statusEffectTags.clear();
        biomeTags.clear();
        pendingRefs_.clear();
        fs::path root = base;
        if (fs::exists(base / "tags", ec)) root = base / "tags";
        if (ec)
            throw fs::filesystem_error("inspect datapack tags", base / "tags",
                                       ec);
        loadTagsFrom(root, "item", itemTags, true, budget);
        loadTagsFrom(root, "block", blockTags, false, budget);
        loadTagsFrom(root, "mob_effect", statusEffectTags, false, budget,
                     true);
        loadTagsFrom(root, "status_effect", statusEffectTags, false, budget,
                     true);
        loadBiomeTags(root, budget);
        if (itemTags.empty()) ensureDefaults();
        if (biomeTags.empty()) ensureBiomeDefaults();
    }

    template<class TagMap>
    static void recordExistingTagEntries(
        const TagMap& tags, datapack_detail::LooseDataBudget& budget) {
        for (const auto& [name, values] : tags) {
            (void)name;
            budget.recordResolvedTagEntries(values.size());
        }
    }

    static std::string canonicalResourceId(std::string id) {
        if (id.find(':') == std::string::npos)
            id = "minecraft:" + id;
        return id;
    }

    template<class TagValue, class ResolveValue>
    void mergeNamespacedTagsFrom(
        const std::filesystem::path& directory, const std::string& nameSpace,
        std::unordered_map<std::string, std::unordered_set<TagValue>>& output,
        NamespacedTagReferences& references,
        datapack_detail::LooseDataBudget& budget,
        ResolveValue resolveValue) {
        namespace fs = std::filesystem;
        std::error_code error;
        if (!fs::exists(directory, error)) {
            if (error)
                throw fs::filesystem_error("inspect datapack tag directory",
                                           directory, error);
            return;
        }
        if (!fs::is_directory(directory, error) || error)
            throw fs::filesystem_error("inspect datapack tag directory",
                                       directory,
                                       error ? error : std::make_error_code(
                                           std::errc::not_a_directory));

        std::vector<fs::path> files;
        fs::recursive_directory_iterator it(directory, error), end;
        if (error)
            throw fs::filesystem_error("scan datapack tag directory",
                                       directory, error);
        while (it != end) {
            budget.recordDirectoryEntry(it->path());
            std::error_code statusError;
            if (it->is_regular_file(statusError) && !statusError &&
                it->path().extension() == ".json") {
                budget.recordFile(it->path());
                files.push_back(it->path());
            }
            it.increment(error);
            if (error)
                throw fs::filesystem_error("scan datapack tag directory",
                                           directory, error);
        }
        std::sort(files.begin(), files.end());

        for (const auto& filePath : files) {
            const std::string text = budget.readFile(filePath);

            try {
                const auto value = json::Value::parse(
                    text, json::Value::kMaxTotalValues);
                if (!value.isObj())
                    throw std::invalid_argument("tag root must be an object");
                std::error_code relativeError;
                auto relative = utf8::genericPathToUtf8(fs::relative(
                    filePath, directory, relativeError));
                if (relativeError)
                    throw fs::filesystem_error("resolve datapack tag path",
                        filePath, directory, relativeError);
                if (relative.size() <= 5 ||
                    relative.substr(relative.size() - 5) != ".json")
                    throw std::invalid_argument("tag filename must end in .json");
                relative.resize(relative.size() - 5);
                const auto tagName = nameSpace + ":" + relative;
                const auto* replaceValue = value.find("replace");
                if (replaceValue && !replaceValue->isBool())
                    throw std::invalid_argument("replace must be a boolean");
                if (replaceValue && replaceValue->boolean) {
                    output[tagName].clear();
                    references.erase(tagName);
                }

                auto& tagValues = output[tagName];
                const auto* values = value.find("values");
                if (!values || !values->isArr())
                    throw std::invalid_argument("values must be an array");
                for (const auto& entry : values->arr) {
                    std::string id;
                    bool required = true;
                    if (entry.isStr()) {
                        id = entry.asStr();
                    } else if (entry.isObj()) {
                        const auto* idValue = entry.find("id");
                        if (!idValue || !idValue->isStr())
                            throw std::invalid_argument(
                                "tag entry object requires a string id");
                        id = idValue->asStr();
                        if (const auto* requiredValue = entry.find("required")) {
                            if (!requiredValue->isBool())
                                throw std::invalid_argument(
                                    "tag entry required must be a boolean");
                            required = requiredValue->boolean;
                        }
                    } else {
                        throw std::invalid_argument(
                            "tag entry must be a string or object");
                    }
                    if (id.empty())
                        throw std::invalid_argument("tag entry id is empty");
                    if (id.front() == '#') {
                        const auto target =
                            canonicalResourceId(id.substr(1));
                        auto& sourceReferences = references[tagName];
                        const auto existing = sourceReferences.find(target);
                        if (existing == sourceReferences.end()) {
                            budget.recordTagReference(filePath);
                            sourceReferences.emplace(target, required);
                        } else {
                            existing->second = existing->second || required;
                        }
                        continue;
                    }
                    auto resolvedValues = resolveValue(
                        canonicalResourceId(std::move(id)));
                    if (resolvedValues.empty() && required)
                        throw std::invalid_argument(
                            "required tag value is not present in the registry");
                    for (const auto& resolved : resolvedValues)
                        if (tagValues.insert(resolved).second)
                            budget.recordResolvedTagEntry(filePath);
                }
            } catch (const datapack_detail::LooseDataLimitExceeded&) {
                throw;
            } catch (const json::ResourceLimitExceeded&) {
                throw;
            } catch (const std::exception& exception) {
                throw std::runtime_error("invalid datapack tag " +
                    utf8::pathToUtf8(filePath) + ": " + exception.what());
            }
        }
    }

    template<class TagValue>
    static void resolveNamespacedTagReferences(
        std::unordered_map<std::string, std::unordered_set<TagValue>>& tags,
        const NamespacedTagReferences& references,
        datapack_detail::LooseDataBudget& budget) {
        struct DependentTag {
            const std::string* source;
            bool required;
        };
        std::unordered_map<std::string, std::vector<DependentTag>> dependents;
        std::size_t referenceCount = 0;
        for (const auto& [source, targets] : references) {
            (void)source;
            referenceCount += targets.size();
        }
        dependents.reserve(referenceCount);
        for (const auto& [source, targets] : references)
            for (const auto& [target, required] : targets)
                dependents[target].push_back({&source, required});

        std::deque<std::pair<std::string, TagValue>> pendingValues;
        for (const auto& [tagName, values] : tags)
            for (const auto& value : values)
                pendingValues.emplace_back(tagName, value);

        while (!pendingValues.empty()) {
            auto [targetName, value] = std::move(pendingValues.front());
            pendingValues.pop_front();
            const auto targetDependents = dependents.find(targetName);
            if (targetDependents == dependents.end()) continue;
            for (const auto& dependent : targetDependents->second) {
                budget.recordTagResolutionWork();
                const auto source = tags.find(*dependent.source);
                if (source == tags.end()) continue;
                if (source->second.insert(value).second) {
                    budget.recordResolvedTagEntry();
                    pendingValues.emplace_back(*dependent.source, value);
                }
            }
        }
        for (const auto& [source, targets] : references)
            for (const auto& [target, required] : targets)
                if (required && !tags.contains(target))
                    throw std::runtime_error("required tag reference " +
                        source + " -> " + target + " does not exist");
    }

    void loadTagsFrom(const std::filesystem::path& root,
                      const std::string& sub,
                      std::unordered_map<std::string, IdSet>& out, bool isItem,
                      datapack_detail::LooseDataBudget& budget,
                      bool isStatusEffect = false) {
        namespace fs = std::filesystem;
        const fs::path dir = root / sub;
        std::error_code ec;
        if (!fs::exists(dir, ec)) return;
        fs::recursive_directory_iterator iterator(dir, ec), end;
        if (ec)
            throw fs::filesystem_error("scan datapack tag directory", dir, ec);
        while (iterator != end) {
            auto& e = *iterator;
            budget.recordDirectoryEntry(e.path());
            if (!e.is_regular_file() || e.path().extension() != ".json") {
                iterator.increment(ec);
                if (ec)
                    throw fs::filesystem_error(
                        "scan datapack tag directory", dir, ec);
                continue;
            }
            budget.recordFile(e.path());
            try {
                const std::string text = budget.readFile(e.path());
                const json::Value v = json::Value::parse(
                    text, json::Value::kMaxTotalValues);
                std::string rel = utf8::genericPathToUtf8(
                    fs::relative(e.path(), dir, ec));
                if (rel.size()>5 && rel.substr(rel.size()-5)==".json") rel = rel.substr(0, rel.size()-5);
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
                            const auto id = resolveId(idStr, isItem,
                                                      isStatusEffect);
                            if (id) set.insert(*id);
                        }
                    }
                }
                auto it2 = out.find(tagName);
                if (it2 == out.end()) it2 = out.emplace(tagName, IdSet{}).first;
                for (const auto id : set)
                    if (it2->second.insert(id).second)
                        budget.recordResolvedTagEntry(e.path());
            } catch (const datapack_detail::LooseDataLimitExceeded&) {
                throw;
            } catch (const json::ResourceLimitExceeded&) {
                throw;
            } catch (const std::exception& ex) {
                const auto displayPath = utf8::pathToUtf8(e.path());
                std::fprintf(stderr, "[TagManager] ignoring malformed tag %s: %s\n",
                             displayPath.c_str(), ex.what());
            } catch (...) {
                const auto displayPath = utf8::pathToUtf8(e.path());
                std::fprintf(stderr, "[TagManager] ignoring malformed tag %s\n",
                             displayPath.c_str());
            }
            iterator.increment(ec);
            if (ec)
                throw fs::filesystem_error("scan datapack tag directory", dir,
                                           ec);
        }
        for (auto& [tag, refs] : pendingRefs_) {
            auto itTag = out.find(tag);
            if (itTag==out.end()) continue;
            for (auto& ref: refs) {
                auto itRef = out.find(ref);
                if (itRef != out.end()) for (auto id: itRef->second)
                    if (itTag->second.insert(id).second)
                        budget.recordResolvedTagEntry();
            }
        }
        pendingRefs_.clear();
        if (isItem) ensureItemDefaults(out);
        else if (!isStatusEffect) ensureBlockDefaults(out);
    }
    std::optional<std::uint32_t> resolveId(const std::string& name,
                                           bool isItem,
                                           bool isStatusEffect = false) const {
        if (isItem) {
            auto it = gen::itemIdByName().find(name);
            if (it != gen::itemIdByName().end()) return it->second;
        } else if (isStatusEffect) {
            const auto it = effects::byName().find(name);
            if (it != effects::byName().end()) return it->second;
        } else {
            auto it = gen::blockNameToState().find(name);
            if (it != gen::blockNameToState().end())
                return static_cast<std::uint32_t>(it->second);
        }
        return std::nullopt;
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
        add("minecraft:infiniburn_overworld", {"minecraft:netherrack","minecraft:magma_block"});
        add("minecraft:infiniburn_nether", {"minecraft:netherrack","minecraft:magma_block"});
        add("minecraft:infiniburn_end", {"minecraft:bedrock","minecraft:netherrack","minecraft:magma_block"});
        add("minecraft:soul_fire_base_blocks", {"minecraft:soul_sand","minecraft:soul_soil"});
    }
    void loadBiomeTags(const std::filesystem::path& root,
                       datapack_detail::LooseDataBudget& budget) {
        namespace fs = std::filesystem;
        std::error_code ec;
        // Try root + "/worldgen/biome" (assets/data/tags/worldgen/biome) and also root + "/biome"
        const std::vector<fs::path> dirs = {
            root / "worldgen" / "biome", root / "biome"};
        for(auto& dir: dirs){
            if(!fs::exists(dir, ec)) continue;
            fs::recursive_directory_iterator iterator(dir, ec), end;
            if (ec)
                throw fs::filesystem_error("scan datapack biome tags", dir,
                                           ec);
            while (iterator != end) {
                auto& e = *iterator;
                budget.recordDirectoryEntry(e.path());
                if(!e.is_regular_file() || e.path().extension() != ".json") {
                    iterator.increment(ec);
                    if (ec)
                        throw fs::filesystem_error(
                            "scan datapack biome tags", dir, ec);
                    continue;
                }
                budget.recordFile(e.path());
                try{
                    const std::string text = budget.readFile(e.path());
                    const json::Value v = json::Value::parse(
                        text, json::Value::kMaxTotalValues);
                    std::string rel = utf8::genericPathToUtf8(
                        fs::relative(e.path(), dir, ec));
                    if(rel.size()>5 && rel.substr(rel.size()-5)==".json") rel = rel.substr(0, rel.size()-5);
                    std::string tagName = "minecraft:" + rel;
                    std::unordered_set<std::string> set;
                    const auto& vals = v.at("values");
                    if(vals.isArr()){
                        for(auto& elem: vals.arr){
                            std::string idStr;
                            if(elem.isStr()) idStr = elem.asStr();
                            else if(elem.isObj()) idStr = elem.at("id").asStr();
                            if(!idStr.empty()){
                                if(idStr[0]=='#'){
                                    std::string ref = idStr.substr(1);
                                    auto it = biomeTags.find(ref);
                                    if(it != biomeTags.end()) for(auto& id: it->second) set.insert(id);
                                } else {
                                    set.insert(idStr);
                                    // also store without prefix for lenient match
                                    if(idStr.find(':')!=std::string::npos) set.insert(idStr.substr(idStr.find(':')+1));
                                }
                            }
                        }
                    }
                    auto it2 = biomeTags.find(tagName);
                    if(it2 == biomeTags.end())
                        it2 = biomeTags.emplace(tagName,
                                                std::unordered_set<std::string>{})
                                  .first;
                    for (const auto& id : set)
                        if (it2->second.insert(id).second)
                            budget.recordResolvedTagEntry(e.path());
                } catch (const datapack_detail::LooseDataLimitExceeded&) {
                    throw;
                } catch (const json::ResourceLimitExceeded&) {
                    throw;
                } catch (const std::exception& ex) {
                    const auto displayPath = utf8::pathToUtf8(e.path());
                    std::fprintf(stderr, "[TagManager] ignoring malformed biome tag %s: %s\n",
                                 displayPath.c_str(), ex.what());
                } catch (...) {
                    const auto displayPath = utf8::pathToUtf8(e.path());
                    std::fprintf(stderr, "[TagManager] ignoring malformed biome tag %s\n",
                                 displayPath.c_str());
                }
                iterator.increment(ec);
                if (ec)
                    throw fs::filesystem_error("scan datapack biome tags",
                                               dir, ec);
            }
        }
    }
    void ensureBiomeDefaults(){
        auto add=[&](const std::string& tag, std::initializer_list<const char*> biomes){
            if(biomeTags.count(tag)) return;
            std::unordered_set<std::string> s;
            for(auto* n: biomes) s.insert(n);
            if(!s.empty()) biomeTags.emplace(tag, std::move(s));
        };
        add("minecraft:is_overworld", {"minecraft:plains","minecraft:desert","minecraft:forest","minecraft:mountains","minecraft:swamp","minecraft:taiga","minecraft:savanna","minecraft:jungle","minecraft:beach","minecraft:ocean"});
        add("minecraft:is_nether", {"minecraft:nether_wastes","minecraft:soul_sand_valley","minecraft:crimson_forest","minecraft:warped_forest","minecraft:basalt_deltas"});
        add("minecraft:is_end", {"minecraft:end_highlands","minecraft:end_midlands","minecraft:small_end_islands","minecraft:end_barrens","minecraft:the_end"});
        add("minecraft:is_mountain", {"minecraft:mountains","minecraft:wooded_mountains","minecraft:snowy_mountains"});
        add("minecraft:is_beach", {"minecraft:beach","minecraft:snowy_beach"});
        add("minecraft:is_forest", {"minecraft:forest","minecraft:wooded_hills","minecraft:birch_forest","minecraft:dark_forest"});
    }
};

} // namespace cppfm
