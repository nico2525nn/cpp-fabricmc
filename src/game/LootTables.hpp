// LootTables: JSON evaluator for block drops (issue 65) Replaces hard-coded mineInfo drop with data-driven LootTableEvaluator. Supports
// vanilla loot_tables/blocks/*.json subset: pools, rolls, entries, weight, set_count. GameServer reads the active registry through DatapackManager.
// instead of kOv map.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <unordered_set>
#include <utility>
#include "../core/Json.hpp"
#include "../core/Random.hpp"
#include "../core/Utf8.hpp"
#include "../generated/ItemIds.hpp"
#include "Items.hpp"
#include "DatapackResourceBudget.hpp"

namespace cppfm {

class LootTableRandom;

struct LootNumberProvider {
    enum class Kind { Constant, Uniform, Binomial };
    Kind kind = Kind::Constant;
    double minimum = 1.0;
    double maximum = 1.0;
    std::int32_t trials = 0;
    double probability = 0.0;
};

inline std::optional<double> lootProviderScalar(const json::Value& value) {
    if (value.isNum() && std::isfinite(value.number)) return value.number;
    if (!value.isObj()) return std::nullopt;
    const auto* type = value.find("type");
    const auto* constant = value.find("value");
    if (!type || !type->isStr() ||
        (type->asStr() != "constant" &&
         type->asStr() != "minecraft:constant") ||
        !constant || !constant->isNum() || !std::isfinite(constant->number))
        return std::nullopt;
    return constant->number;
}

inline std::optional<LootNumberProvider> parseLootNumberProvider(
    const json::Value& value) {
    if (const auto scalar = lootProviderScalar(value))
        return LootNumberProvider{LootNumberProvider::Kind::Constant,
                                  *scalar, *scalar, 0, 0.0};
    if (!value.isObj()) return std::nullopt;

    const auto* typeValue = value.find("type");
    const std::string type = typeValue && typeValue->isStr()
        ? typeValue->asStr() : std::string{};
    if (type.empty() || type == "uniform" || type == "minecraft:uniform") {
        const auto* minValue = value.find("min");
        const auto* maxValue = value.find("max");
        if (!minValue || !maxValue) return std::nullopt;
        const auto minimum = lootProviderScalar(*minValue);
        const auto maximum = lootProviderScalar(*maxValue);
        if (!minimum || !maximum || *minimum > *maximum)
            return std::nullopt;
        return LootNumberProvider{LootNumberProvider::Kind::Uniform,
                                  *minimum, *maximum, 0, 0.0};
    }
    if (type == "binomial" || type == "minecraft:binomial") {
        const auto* trialsValue = value.find("n");
        const auto* probabilityValue = value.find("p");
        if (!trialsValue || !trialsValue->isNum() ||
            !probabilityValue || !probabilityValue->isNum() ||
            !std::isfinite(trialsValue->number) ||
            !std::isfinite(probabilityValue->number) ||
            std::round(trialsValue->number) != trialsValue->number ||
            trialsValue->number < 0 || trialsValue->number > 65536 ||
            probabilityValue->number < 0 || probabilityValue->number > 1)
            return std::nullopt;
        const auto trials = static_cast<std::int32_t>(trialsValue->number);
        return LootNumberProvider{LootNumberProvider::Kind::Binomial,
                                  0.0, static_cast<double>(trials), trials,
                                  probabilityValue->number};
    }
    return std::nullopt;
}

inline double sampleLootNumberProvider(const LootNumberProvider& provider,
                                       LootTableRandom& random);

struct LootEntry {
    std::string type = "minecraft:item";
    std::string name; // e.g. minecraft:cobblestone
    bool supported = true;
    int weight = 1;
    int countMin = 1, countMax = 1;
    json::Value conditionsJson;
    bool explosionDecay = false;
    bool furnaceSmelt = false;
    json::Value furnaceSmeltConditionsJson;
    bool countIsBinomial = false;
    int countBinomN = 3;
    double countBinomP = 0.5;
    bool enchantRandomly = false;
    std::vector<std::string> enchantOptions;
    bool applyBonusOre = false;
    std::string applyBonusFormula;
    std::string applyBonusEnchant;
    bool lootingEnchant = false;
    int lootingMin = 0, lootingMax = 1;
    bool hasLimit = false; int limitMin = 1, limitMax = 64;
    bool hasApplyBonusBinomial = false, hasApplyBonusUniform = false;
    int applyBonusN = 1; double applyBonusP = 0.33; double bonusMultiplier = 1.0; double applyBonusExtra = 0;
    bool hasSetDamage = false; double setDamageMin = 0, setDamageMax = 0;
    bool hasSetLore = false; std::vector<std::string> setLore;
    std::string setName;
    json::Value functionsJson;
};
struct LootContext {
    float explosionRadius = 0.f;
    int fortuneLevel = 0;
    int lootingLevel = 0;
    // Set only when this entity's death is credited to player damage. It is
    // not equivalent to evaluating loot during any player-caused action.
    bool wasKilledByPlayer = false;
    bool entityOnFire = false;
    // Registered loot tables that reference `this` or killed_by_player need
    // an actual entity context. A zero-initialized context is not equivalent
    // to a known, not-on-fire/non-player entity.
    bool hasEntityContext = false;
};

struct LootPool {
    int rolls = 1;
    double rollsMin = 1, rollsMax = 1;
    bool rollsIsRange = false;
    LootNumberProvider rollsProvider;
    bool supported = true;
    std::vector<LootEntry> entries;
    json::Value conditionsJson;
};

struct LootTable {
    std::string id; // e.g. minecraft:blocks/stone
    // Support for root-level conditions/functions, which affect every pool.
    bool supported = true;
    // Keep a registered but semantically unsupported table visible to block
    // break code so it suppresses the generic item-drop fallback. Invalid
    // item references remain eligible for the ordinary fallback.
    bool suppressDefaultDropFallback = false;
    std::vector<LootPool> pools;
    json::Value conditionsJson;
};

class LootTableRandom final {
public:
    explicit LootTableRandom(std::optional<std::int64_t> seed)
        : seeded_(seed.value_or(0)), hasSeed_(seed.has_value()) {}

    float nextFloat() {
        return hasSeed_
            ? seeded_.nextFloat()
            : static_cast<float>(
                  static_cast<std::uint32_t>(nextRandom()) >> 7U) *
                  (1.0f / 16777216.0f);
    }

    std::int32_t nextInt(std::int32_t bound) {
        if (bound <= 0) return 0;
        if (hasSeed_) return seeded_.nextInt(bound);

        constexpr std::uint64_t kRandomRange = std::uint64_t{1} << 31U;
        const auto unsignedBound = static_cast<std::uint64_t>(bound);
        const auto acceptedRange =
            kRandomRange - kRandomRange % unsignedBound;
        std::uint32_t bits = 0;
        do {
            bits = static_cast<std::uint32_t>(nextRandom());
        } while (bits >= acceptedRange);
        return static_cast<std::int32_t>(bits % unsignedBound);
    }

private:
    VanillaXoroshiro128PlusPlus seeded_;
    bool hasSeed_ = false;
};

inline double sampleLootNumberProvider(const LootNumberProvider& provider,
                                       LootTableRandom& random) {
    switch (provider.kind) {
    case LootNumberProvider::Kind::Constant:
        return provider.minimum;
    case LootNumberProvider::Kind::Uniform:
        return provider.minimum + random.nextFloat() *
            (provider.maximum - provider.minimum);
    case LootNumberProvider::Kind::Binomial: {
        std::int32_t successes = 0;
        for (std::int32_t trial = 0; trial < provider.trials; ++trial)
            if (random.nextFloat() < provider.probability) ++successes;
        return static_cast<double>(successes);
    }
    }
    return 0.0;
}

inline std::optional<std::int32_t> sampleLootNumberProviderInt(
    const LootNumberProvider& provider, LootTableRandom& random) {
    if (provider.kind == LootNumberProvider::Kind::Uniform) {
        if (provider.minimum < std::numeric_limits<std::int32_t>::min() ||
            provider.maximum > std::numeric_limits<std::int32_t>::max())
            return std::nullopt;
        const auto minimum = static_cast<std::int64_t>(
            std::floor(provider.minimum));
        const auto maximum = static_cast<std::int64_t>(
            std::floor(provider.maximum));
        if (maximum < minimum ||
            maximum - minimum >= std::numeric_limits<std::int32_t>::max())
            return std::nullopt;
        const auto span = static_cast<std::int32_t>(maximum - minimum + 1);
        return static_cast<std::int32_t>(minimum) + random.nextInt(span);
    }
    const auto value = sampleLootNumberProvider(provider, random);
    if (!std::isfinite(value) ||
        value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max())
        return std::nullopt;
    return static_cast<std::int32_t>(std::floor(value));
}

class LootTableEvaluator {
public:
    using Map = std::unordered_map<std::string, LootTable>;
    const Map& tables() const { return tables_; }
    size_t size() const { return tables_.size(); }
    const std::vector<std::string>& loadDiagnostics() const {
        return loadDiagnostics_;
    }
    void clear() {
        tables_.clear();
        loadDiagnostics_.clear();
    }
    const LootTable* find(const std::string& id) const {
        auto it=tables_.find(id);
        return it==tables_.end()?nullptr:&it->second;
    }

    const LootTable* findBlockTable(const std::string& blockName) const {
        const auto colon = blockName.find(':');
        const auto nameSpace = colon == std::string::npos
            ? std::string("minecraft") : blockName.substr(0, colon);
        const auto base = colon == std::string::npos
            ? blockName : blockName.substr(colon + 1);
        if (const auto* table = findBlockTableCandidate(
                nameSpace + ":blocks/" + base)) return table;
        if (const auto* table = findBlockTableCandidate(blockName)) return table;
        return nameSpace == "minecraft"
            ? findBlockTableCandidate("minecraft:" + base) : nullptr;
    }

    bool blockTableRequiresEntityContext(
        const std::string& blockName) const {
        const auto* table = findBlockTable(blockName);
        if (!table) return false;
        std::unordered_set<std::string> activeTables;
        return tableRequiresEntityContext(*table, activeTables, 0);
    }

    void loadDirectory(const std::string& base) {
        loadDirectoryPath(std::filesystem::path(base));
    }

    // Native-path entry point for callers that already hold a filesystem path.
    // Keep the legacy narrow-string API above for source and behavior
    // compatibility; new cross-platform callers should use this overload name.
    void loadDirectoryPath(const std::filesystem::path& basePath) {
        datapack_detail::LooseDataBudget budget;
        loadDirectoryPath(basePath, budget);
    }

    void loadDirectoryPath(const std::filesystem::path& basePath,
                           datapack_detail::LooseDataBudget& budget) {
        LootTableEvaluator staged = *this;
        staged.loadDirectoryPathInPlace(basePath, budget);
        tables_.swap(staged.tables_);
        loadDiagnostics_.swap(staged.loadDiagnostics_);
    }

private:
    void loadDirectoryPathInPlace(
        const std::filesystem::path& basePath,
        datapack_detail::LooseDataBudget& budget) {
        namespace fs = std::filesystem;
        std::error_code ec;
        std::vector<std::pair<fs::path, std::string>> tableRoots;
        const auto isLootTableRoot = [](const fs::path& path) {
            const auto name = utf8::pathToUtf8(path.filename());
            return name == "loot_table" || name == "loot_tables";
        };
        const auto namespaceForRoot = [](const fs::path& root) {
            const auto parent = root.parent_path();
            if (parent.parent_path().filename() == "data")
                return utf8::pathToUtf8(parent.filename());
            return std::string("minecraft");
        };
        const auto addRootIfPresent = [&](const fs::path& root,
                                          const std::string& nameSpace) {
            std::error_code rootError;
            if (fs::is_directory(root, rootError))
                tableRoots.emplace_back(root, nameSpace);
        };

        if (isLootTableRoot(basePath)) {
            addRootIfPresent(basePath, namespaceForRoot(basePath));
        } else {
            const auto directRoot = basePath / "loot_table";
            const auto legacyRoot = basePath / "loot_tables";
            if (fs::is_directory(directRoot, ec))
                addRootIfPresent(directRoot, namespaceForRoot(directRoot));
            else if (fs::is_directory(legacyRoot, ec))
                addRootIfPresent(legacyRoot, namespaceForRoot(legacyRoot));

            // A data-pack data/ directory contains one subdirectory per
            // namespace; 1.21 stores loot tables below <namespace>/loot_table.
            // Also accept the old plural form when reading older packs.
            ec.clear();
            std::vector<fs::directory_entry> namespaceEntries;
            const bool namespaceRootExists = fs::exists(basePath, ec);
            if (ec)
                throw fs::filesystem_error("inspect loot namespace root",
                                           basePath, ec);
            const bool hasNamespaceRoot = namespaceRootExists &&
                fs::is_directory(basePath, ec);
            if (ec)
                throw fs::filesystem_error("inspect loot namespace root",
                                           basePath, ec);
            if (namespaceRootExists && !hasNamespaceRoot)
                throw fs::filesystem_error(
                    "inspect loot namespace root", basePath,
                    std::make_error_code(std::errc::not_a_directory));
            if (hasNamespaceRoot) {
                for (fs::directory_iterator it(basePath, ec), end;
                     !ec && it != end; it.increment(ec)) {
                    budget.recordDirectoryEntry(it->path());
                    std::error_code statusError;
                    if (it->is_directory(statusError))
                        namespaceEntries.push_back(*it);
                    else if (statusError)
                        ec = statusError;
                }
            }
            if (ec)
                throw fs::filesystem_error("enumerate loot namespaces",
                                           basePath, ec);
            std::sort(namespaceEntries.begin(), namespaceEntries.end(),
                [](const fs::directory_entry& lhs,
                   const fs::directory_entry& rhs) {
                    return utf8::pathToUtf8(lhs.path().filename()) <
                           utf8::pathToUtf8(rhs.path().filename());
                });
            for (const auto& namespaceEntry : namespaceEntries) {
                const auto namespaceRoot = namespaceEntry.path() / "loot_table";
                const auto legacyNamespaceRoot =
                    namespaceEntry.path() / "loot_tables";
                if (fs::is_directory(namespaceRoot, ec))
                    addRootIfPresent(namespaceRoot,
                        utf8::pathToUtf8(namespaceEntry.path().filename()));
                else if (fs::is_directory(legacyNamespaceRoot, ec))
                    addRootIfPresent(
                        legacyNamespaceRoot,
                        utf8::pathToUtf8(namespaceEntry.path().filename()));
                ec.clear();
            }
        }
        std::sort(tableRoots.begin(), tableRoots.end(),
            [](const auto& lhs, const auto& rhs) {
                if (lhs.second != rhs.second) return lhs.second < rhs.second;
                return utf8::genericPathToUtf8(lhs.first) <
                       utf8::genericPathToUtf8(rhs.first);
            });
        if (tableRoots.empty()) {
            if (tables_.empty()) loadBuiltins();
            return;
        }

        for (const auto& [root, nameSpace] : tableRoots) {
          std::error_code iterationError;
          for (auto& entry : fs::recursive_directory_iterator(root, iterationError)) {
            budget.recordDirectoryEntry(entry.path());
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            budget.recordFile(entry.path());
            try {
                const std::string text = budget.readFile(entry.path());
                const json::Value v = json::Value::parse(
                    text, json::Value::kMaxTotalValues);
                std::error_code relativeError;
                std::string rel = utf8::genericPathToUtf8(fs::relative(
                    entry.path(), root, relativeError));
                if (relativeError) {
                    loadDiagnostics_.push_back(
                        utf8::pathToUtf8(entry.path()) +
                        ": unable to resolve loot-table path");
                    continue;
                }
                if (rel.size()>=5 && rel.substr(rel.size()-5)==".json") rel = rel.substr(0, rel.size()-5);
                std::string id = nameSpace + ":" + rel;
                LootTable tbl; tbl.id=id;
                const auto& poolsV = v.at("pools");
                const bool hasValidPools =
                    poolsV.isArr() || poolsV.isObj();
                if (const auto* conditions = v.find("conditions")) {
                    if (!conditions->isArr()) {
                        tbl.supported = false;
                    } else {
                        tbl.conditionsJson = *conditions;
                        for (const auto& condition : conditions->arr) {
                            if (!isLootConditionSupported(condition))
                                tbl.supported = false;
                        }
                    }
                    if (!tbl.supported)
                        tbl.suppressDefaultDropFallback = true;
                }
                if (const auto* functions = v.find("functions")) {
                    if (!functions->isArr() || !functions->arr.empty()) {
                        // Root-level functions affect every generated stack;
                        // they are not interchangeable with entry functions.
                        tbl.supported = false;
                        tbl.suppressDefaultDropFallback = true;
                    }
                }
                auto parsePool = [&](const json::Value& poolV, LootTable& out){
                    LootPool pool;
                    const auto* rolls = poolV.find("rolls");
                    if (!rolls) {
                        // The vanilla loot-pool codec defaults omitted rolls
                        // to a single constant roll.
                        pool.rolls = 1;
                        pool.rollsMin = pool.rollsMax = 1;
                    } else {
                        const auto provider = parseLootNumberProvider(*rolls);
                        if (!provider || provider->minimum < 0.0 ||
                            provider->maximum > 65536.0) {
                            pool.supported = false;
                        } else {
                            pool.rollsProvider = *provider;
                            pool.rolls = static_cast<int>(
                                std::floor((provider->minimum +
                                    provider->maximum) * 0.5 + 0.5));
                            pool.rollsIsRange =
                                provider->kind !=
                                    LootNumberProvider::Kind::Constant;
                            pool.rollsMin = provider->minimum;
                            pool.rollsMax = provider->maximum;
                        }
                    }
                    // Vanilla data commonly spells the no-op value as 0.0.
                    // Accept that exact case; nonzero bonus rolls depend on
                    // Luck and stay scoped to this unsupported pool.
                    if (const auto* bonusRolls = poolV.find("bonus_rolls")) {
                        const auto isZeroProvider = [](const json::Value& node) {
                            if (node.isNum())
                                return std::isfinite(node.number) &&
                                       node.number == 0.0;
                            if (!node.isObj()) return false;
                            const auto* typeNode = node.find("type");
                            const auto type = typeNode
                                ? typeNode->asStr() : std::string{};
                            if (type == "minecraft:constant") {
                                const auto* value = node.find("value");
                                return value && value->isNum() &&
                                       std::isfinite(value->number) &&
                                       value->number == 0.0;
                            }
                            if (type == "minecraft:uniform" || type.empty()) {
                                const auto* minimum = node.find("min");
                                const auto* maximum = node.find("max");
                                return minimum && maximum &&
                                       minimum->isNum() && maximum->isNum() &&
                                       std::isfinite(minimum->number) &&
                                       std::isfinite(maximum->number) &&
                                       minimum->number == 0.0 &&
                                       maximum->number == 0.0;
                            }
                            return false;
                        };
                        if (!isZeroProvider(*bonusRolls))
                            pool.supported = false;
                    }
                    if (auto* conds = poolV.find("conditions")) {
                        if (!conds->isArr()) {
                            pool.supported = false;
                        } else {
                            pool.conditionsJson = *conds;
                            for (const auto& condition : conds->arr) {
                                if (!isLootConditionSupported(condition))
                                    pool.supported = false;
                            }
                        }
                    }
                    const auto& entries = poolV.at("entries");
                    if (entries.isArr()) {
                        std::int64_t totalWeight = 0;
                        for (auto& e: entries.arr) {
                            std::string type=e.at("type").asStr();
                            if (type == "minecraft:alternatives" ||
                                type == "minecraft:group" ||
                                type == "minecraft:sequence") {
                                // These composite entry types need their own
                                // condition/ordering semantics. Preserve the
                                // table as unsupported instead of flattening
                                // it into a lossy weighted item list.
                                pool.supported = false;
                                out.suppressDefaultDropFallback = true;
                                continue;
                            }
                            std::string entryName=e.at("name").asStr();
                            if (type != "minecraft:empty" && entryName.empty()) {
                                pool.supported = false;
                                out.suppressDefaultDropFallback = true;
                                continue;
                            }
                            LootEntry ent; ent.name=entryName;
                            if (type.empty()) {
                                ent.supported = false;
                            } else {
                                ent.type = type;
                                if (type != "minecraft:item" &&
                                    type != "minecraft:loot_table" &&
                                    type != "minecraft:empty") {
                                    ent.supported = false;
                                }
                            }
                            const auto& w=e.at("weight");
                            if (e.find("weight")) {
                                if (!w.isNum() || !std::isfinite(w.number) ||
                                    w.number < 1.0 ||
                                    std::round(w.number) != w.number ||
                                    w.number > std::numeric_limits<int>::max()) {
                                    ent.supported = false;
                                    pool.supported = false;
                                } else {
                                    ent.weight = static_cast<int>(w.number);
                                }
                            }
                            totalWeight += ent.weight;
                            if (totalWeight > std::numeric_limits<int>::max())
                                pool.supported = false;
                            // entry-level conditions as functions: skip if needed (survives_explosion handled via explosionDecay)
                            if (auto* entryConditions = e.find("conditions")) {
                                if (entryConditions->isArr()) {
                                    ent.conditionsJson = *entryConditions;
                                    for (const auto& condition : entryConditions->arr) {
                                        if (!isLootConditionSupported(condition))
                                            ent.supported = false;
                                    }
                                } else {
                                    ent.supported = false;
                                }
                            }
                            if (auto* funcs = e.find("functions")) {
                                if (funcs->isArr()) {
                                    applyFunctions(*funcs, ent);
                                    ent.functionsJson = *funcs;
                                }
                                else ent.supported = false;
                            }
                            if (!ent.supported)
                                out.suppressDefaultDropFallback = true;
                            pool.entries.push_back(std::move(ent));
                        }
                    }
                    if (!entries.isArr()) pool.supported = false;
                    if (!pool.supported)
                        out.suppressDefaultDropFallback = true;
                    if(!pool.entries.empty()) out.pools.push_back(std::move(pool));
                };
                if (poolsV.isArr()) for(auto& pv: poolsV.arr) parsePool(pv,tbl);
                else if (poolsV.isObj()) parsePool(poolsV,tbl);
                // An explicitly empty pool list is a real loot table: it
                // suppresses drops. Do not confuse it with an absent or
                // malformed table, which is the only case that may fall back.
                if (hasValidPools) {
                    tbl.suppressDefaultDropFallback =
                        tbl.suppressDefaultDropFallback || !tbl.supported;
                    tables_[id] = std::move(tbl);
                }
            } catch (const datapack_detail::LooseDataLimitExceeded&) {
                throw;
            } catch (const json::ResourceLimitExceeded&) {
                throw;
            } catch (const std::exception& error) {
                loadDiagnostics_.push_back(
                    utf8::pathToUtf8(entry.path()) + ": " + error.what());
            } catch (...) {
                loadDiagnostics_.push_back(
                    utf8::pathToUtf8(entry.path()) +
                    ": unknown loot-table load error");
            }
        }
          if (iterationError)
              throw fs::filesystem_error("enumerate loot-table directory",
                                         root, iterationError);
        }
        if (tables_.empty()) loadBuiltins();
        else {
            ensureBuiltin("minecraft:blocks/stone","minecraft:cobblestone");
            ensureBuiltin("minecraft:blocks/grass_block","minecraft:dirt");
        }
    }

public:
    static std::string smeltResultFor(const std::string& in) {
        static const std::unordered_map<std::string,std::string> mp{
            {"minecraft:beef","minecraft:cooked_beef"},
            {"minecraft:chicken","minecraft:cooked_chicken"},
            {"minecraft:rabbit","minecraft:cooked_rabbit"},
            {"minecraft:mutton","minecraft:cooked_mutton"},
            {"minecraft:porkchop","minecraft:cooked_porkchop"},
            {"minecraft:cod","minecraft:cooked_cod"},
            {"minecraft:salmon","minecraft:cooked_salmon"},
            {"minecraft:iron_ore","minecraft:iron_ingot"},
            {"minecraft:deepslate_iron_ore","minecraft:iron_ingot"},
            {"minecraft:gold_ore","minecraft:gold_ingot"},
            {"minecraft:deepslate_gold_ore","minecraft:gold_ingot"},
            {"minecraft:nether_gold_ore","minecraft:gold_nugget"},
            {"minecraft:copper_ore","minecraft:copper_ingot"},
            {"minecraft:deepslate_copper_ore","minecraft:copper_ingot"},
            {"minecraft:sand","minecraft:glass"},
            {"minecraft:red_sand","minecraft:glass"},
            {"minecraft:cobblestone","minecraft:stone"},
            {"minecraft:cobbled_deepslate","minecraft:deepslate"},
            {"minecraft:clay","minecraft:terracotta"},
            {"minecraft:netherrack","minecraft:nether_brick"},
            {"minecraft:ancient_debris","minecraft:netherite_scrap"},
            {"minecraft:raw_iron_block","minecraft:iron_block"},
            {"minecraft:raw_gold_block","minecraft:gold_block"},
            {"minecraft:raw_copper_block","minecraft:copper_block"},
        };
        auto it=mp.find(in);
        if(it!=mp.end()) return it->second;
        return "";
    }
    // Evaluate returns list of ItemStacks for blockName using tool (silk_touch/fortune).
    // Replaces hard-coded kOv drop table in tickDigs (issue 65).
    std::vector<ItemStack> evaluate(
        const std::string& blockName, const ItemStack& tool = {},
        std::optional<std::int64_t> seed = std::nullopt) const {
        return evaluateWithContext(blockName, tool, nullptr, seed);
    }
    std::optional<std::vector<ItemStack>> evaluateRegisteredTable(
        const std::string& tableId, const ItemStack& tool = {},
        const LootContext* ctx = nullptr,
        std::optional<std::int64_t> seed = std::nullopt) const {
        // Registered-table callers must distinguish a legitimate empty loot
        // result from semantics this evaluator cannot implement. In
        // particular, pending container loot is destructive to materialize:
        // returning an engaged empty vector would clear the saved table and
        // permanently discard its contents. Block-drop fallback suppression
        // remains handled by evaluateWithContext/findBlockTableCandidate.
        return evaluateRegisteredTableImpl(
            tableId, tool, ctx, seed, false);
    }
    // Best-effort evaluation is only for ephemeral drops (currently mob
    // drops), where unsupported entries may be omitted without destroying
    // persisted state. Container materialization and other durable consumers
    // must use evaluateRegisteredTable() and preserve the pending table when
    // it returns nullopt.
    std::optional<std::vector<ItemStack>> evaluateRegisteredTableBestEffort(
        const std::string& tableId, const ItemStack& tool = {},
        const LootContext* ctx = nullptr,
        std::optional<std::int64_t> seed = std::nullopt) const {
        return evaluateRegisteredTableImpl(
            tableId, tool, ctx, seed, true);
    }
    std::vector<ItemStack> evaluateEntity(const std::string& entityName, const LootContext* ctx = nullptr) const {
        std::string base = entityName.find(':')!=std::string::npos ? entityName.substr(entityName.find(':')+1) : entityName;
        std::string id = "minecraft:entities/" + base;
        auto it = tables_.find(id);
        if (it==tables_.end()) it=tables_.find(entityName);
        if (it==tables_.end()) it=tables_.find("minecraft:" + base);
        if (it==tables_.end()) return {};
        LootContext dummy;
        const LootContext* c = ctx ? ctx : &dummy;
        return evaluateTable(it->second, ItemStack{}, c, base, std::nullopt);
    }
    std::vector<ItemStack> evaluateWithContext(
        const std::string& blockName, const ItemStack& tool,
        const LootContext* ctx,
        std::optional<std::int64_t> seed = std::nullopt) const {
        const auto colon = blockName.find(':');
        const auto nameSpace = colon == std::string::npos
            ? std::string("minecraft") : blockName.substr(0, colon);
        std::string base = colon != std::string::npos
            ? blockName.substr(colon + 1) : blockName;
        std::string id = nameSpace + ":blocks/" + base;
        auto it = tables_.find(id);
        if (it==tables_.end()) it=tables_.find(blockName);
        if (it==tables_.end()) {
            // also try entities prefix (for mob drops via blockName)
            auto itE = tables_.find("minecraft:entities/" + base);
            if (itE!=tables_.end()) it=itE;
            else {
                auto it2 = tables_.find(nameSpace + ":" + base);
                if (it2!=tables_.end()) it=it2;
                else if (it==tables_.end()) return {};
            }
        }
        std::unordered_set<std::string> visitedTables;
        const bool hasPartialSemantics =
            containsUnsupportedSemantics(it->second, visitedTables);
        if (!hasPartialSemantics) {
            std::unordered_set<std::string> activeTables;
            if (!isTableSupported(it->second, activeTables, 0)) return {};
        }
        LootContext dummy;
        if (!ctx) { dummy = {}; ctx = &dummy; }
        // propagate fortune from tool if ctx has zero
        LootContext eff = *ctx;
        if (eff.fortuneLevel==0 && tool.itemId!=0) eff.fortuneLevel = tool.fortuneLevel();
        if (eff.lootingLevel==0 && tool.itemId!=0) eff.lootingLevel = tool.lootingLevel();
        return evaluateTable(it->second, tool, &eff, base, seed);
    }
    std::vector<ItemStack> evaluateTable(
        const LootTable& tbl, const ItemStack& tool, const LootContext* ctx,
        const std::string& base,
        std::optional<std::int64_t> seed = std::nullopt) const {
        LootTableRandom random(seed);
        if (!tbl.supported) return {};
        const auto conditionPasses = [&](auto&& self,
                                         const json::Value& condition) -> bool {
            const auto* conditionNode = condition.find("condition");
            if (!conditionNode || !conditionNode->isStr()) return false;
            const auto& name = conditionNode->asStr();
            if (name == "minecraft:any_of" || name == "any_of" ||
                name == "minecraft:all_of" || name == "all_of") {
                const auto* terms = condition.find("terms");
                if (!terms || !terms->isArr()) return false;
                const bool isAny = name == "minecraft:any_of" ||
                                   name == "any_of";
                if (isAny) {
                    for (const auto& term : terms->arr)
                        if (self(self, term)) return true;
                    return false;
                }
                for (const auto& term : terms->arr)
                    if (!self(self, term)) return false;
                return true;
            }
            if (name == "minecraft:random_chance" ||
                name == "random_chance") {
                const auto* value = condition.find("chance");
                const auto provider = value
                    ? parseLootNumberProvider(*value) : std::nullopt;
                return provider &&
                    random.nextFloat() < sampleLootNumberProvider(
                        *provider, random);
            }
            if (name == "minecraft:random_chance_with_looting" ||
                name == "random_chance_with_looting") {
                const auto* chanceValue = condition.find("chance");
                const auto* multiplierValue =
                    condition.find("looting_multiplier");
                const auto chance = chanceValue
                    ? parseLootNumberProvider(*chanceValue) : std::nullopt;
                if (!chance || !multiplierValue ||
                    !multiplierValue->isNum() ||
                    !std::isfinite(multiplierValue->number)) return false;
                const double level = ctx
                    ? std::max(0, ctx->lootingLevel) : 0;
                const double probability = sampleLootNumberProvider(
                    *chance, random) + multiplierValue->number * level;
                return random.nextFloat() < probability;
            }
            if (name == "minecraft:random_chance_with_enchanted_bonus" ||
                name == "random_chance_with_enchanted_bonus") {
                const auto* baseValue = condition.find("unenchanted_chance");
                const auto* enchantedValue =
                    condition.find("enchanted_chance");
                const auto* enchantmentValue = condition.find("enchantment");
                if (!baseValue || !baseValue->isNum() ||
                    !enchantedValue || !enchantedValue->isObj() ||
                    !enchantmentValue || !enchantmentValue->isStr())
                    return false;
                const auto& enchantment = enchantmentValue->asStr();
                int level = tool.enchantLevel(enchantment);
                if (enchantment == "fortune" ||
                    enchantment == "minecraft:fortune") {
                    if (ctx) level = std::max(level, ctx->fortuneLevel);
                } else if (enchantment == "looting" ||
                           enchantment == "minecraft:looting") {
                    if (ctx) level = std::max(level, ctx->lootingLevel);
                } else {
                    // No other enchantment levels are represented by the
                    // loot evaluation context yet. Do not silently evaluate
                    // a different enchantment's level as though it matched.
                    return false;
                }
                level = std::max(0, level);
                double probability = baseValue->number;
                if (level > 0) {
                    const auto* type = enchantedValue->find("type");
                    const auto* base = enchantedValue->find("base");
                    const auto* perLevel =
                        enchantedValue->find("per_level_above_first");
                    if (!type || !type->isStr() ||
                        type->asStr() != "minecraft:linear" ||
                        !base || !base->isNum() ||
                        !perLevel || !perLevel->isNum()) return false;
                    probability = base->number + perLevel->number * (level - 1);
                }
                return random.nextFloat() < std::clamp(
                    probability, 0.0, 1.0);
            }
            if (name == "minecraft:killed_by_player" ||
                name == "killed_by_player")
                return ctx && ctx->wasKilledByPlayer;
            if (name == "minecraft:survives_explosion" ||
                name == "survives_explosion")
                return !ctx || ctx->explosionRadius <= 0.f ||
                    random.nextFloat() <= 1.0 / ctx->explosionRadius;
            if (name == "minecraft:entity_properties" ||
                name == "entity_properties") {
                const auto* entity = condition.find("entity");
                const auto* predicate = condition.find("predicate");
                const auto* onFire = predicate
                    ? predicate->find("on_fire") : nullptr;
                if (!onFire && predicate) {
                    if (const auto* flags = predicate->find("flags"))
                        onFire = flags->find("is_on_fire");
                }
                const bool targetsThisEntity = !entity ||
                    (entity->isStr() && entity->asStr() == "this");
                return targetsThisEntity && onFire && onFire->isBool() &&
                    (!ctx ? !onFire->boolean
                          : ctx->entityOnFire == onFire->boolean);
            }
            return false;
        };
        const auto conditionsPass = [&](const json::Value& conditions) {
            if (!conditions.isArr()) return true;
            for (const auto& condition : conditions.arr)
                if (!conditionPasses(conditionPasses, condition)) return false;
            return true;
        };
        auto evaluate = [&](auto&& self, const LootTable& current,
                            const std::string& tableBase,
                            std::size_t depth) -> std::vector<ItemStack> {
        if (depth > 32) return {};
        std::vector<ItemStack> out;
        if (!current.supported) return out;
        if (!conditionsPass(current.conditionsJson)) return out;
        for (auto& pool: current.pools) {
            if (!pool.supported) continue;
            if (!conditionsPass(pool.conditionsJson)) continue;
            const auto sampledRolls = sampleLootNumberProviderInt(
                pool.rollsProvider, random);
            if (!sampledRolls || *sampledRolls < 0 || *sampledRolls > 65536)
                continue;
            const int rolls = *sampledRolls;
            for(int r=0;r<rolls;++r){
                if(pool.entries.empty()) continue;
                std::int64_t totalWeight = 0;
                for (const auto& entry : pool.entries)
                    totalWeight += entry.weight;
                if (totalWeight <= 0 ||
                    totalWeight > std::numeric_limits<int>::max())
                    return {};
                const auto totalW = static_cast<int>(totalWeight);
                int pick=random.nextInt(totalW);
                const LootEntry* chosen=nullptr;
                for(auto& e:pool.entries){ if(pick<e.weight){chosen=&e;break;} pick-=e.weight; }
                if(!chosen) chosen=&pool.entries.back();
                if (!chosen->supported) continue;
                if (!conditionsPass(chosen->conditionsJson)) continue;
                if (chosen->type == "minecraft:empty") continue;
                if (chosen->type == "minecraft:loot_table") {
                    const auto* nested = find(chosen->name);
                    if (!nested) continue;
                    const auto colon = chosen->name.find(':');
                    const auto nestedBase = colon == std::string::npos
                        ? chosen->name : chosen->name.substr(colon + 1);
                    auto nestedDrops = self(self, *nested, nestedBase,
                                            depth + 1);
                    out.insert(out.end(),
                               std::make_move_iterator(nestedDrops.begin()),
                               std::make_move_iterator(nestedDrops.end()));
                    continue;
                }
                if (chosen->type != "minecraft:item") return {};
                std::string dropName = chosen->name;
                int count = chosen->countMin;
                double damageFraction = 0.0;
                bool hasDamageFraction = false;
                std::vector<std::string> lore;
                std::string displayName;
                std::optional<std::pair<std::string, int>> enchantment;
                const bool hasFunctionProgram =
                    chosen->functionsJson.isArr() &&
                    !chosen->functionsJson.arr.empty();

                if (hasFunctionProgram) {
                    count = 1;
                    const auto boundedCount = [](std::int64_t value) {
                        return static_cast<int>(std::clamp<std::int64_t>(
                            value, 0,
                            std::numeric_limits<std::int16_t>::max()));
                    };
                    for (const auto& function : chosen->functionsJson.arr) {
                        const auto* functionNameNode = function.find("function");
                        if (!functionNameNode || !functionNameNode->isStr())
                            continue; // malformed programs are rejected at load
                        const auto& functionName = functionNameNode->asStr();
                        const auto isFunction = [&functionName](
                                                    const char* shortName) {
                            return functionName == shortName ||
                                functionName ==
                                    std::string("minecraft:") + shortName;
                        };
                        if (const auto* conditions = function.find("conditions");
                            conditions && !conditionsPass(*conditions))
                            continue;

                        if (isFunction("set_count")) {
                            const auto* providerNode = function.find("count");
                            const auto provider = providerNode
                                ? parseLootNumberProvider(*providerNode)
                                : std::nullopt;
                            const auto sampled = provider
                                ? sampleLootNumberProviderInt(*provider, random)
                                : std::nullopt;
                            if (!sampled) continue;
                            const auto* add = function.find("add");
                            const bool shouldAdd = add && add->isBool() &&
                                                   add->boolean;
                            count = shouldAdd
                                ? boundedCount(static_cast<std::int64_t>(count) +
                                               *sampled)
                                : boundedCount(*sampled);
                        } else if (isFunction("apply_bonus")) {
                            const int fortune = ctx
                                ? std::clamp(ctx->fortuneLevel, 0, 65536) : 0;
                            if (fortune == 0) continue;
                            const auto* formulaNode = function.find("formula");
                            const std::string formula = formulaNode &&
                                    formulaNode->isStr()
                                ? formulaNode->asStr() : std::string{};
                            const auto* parameters = function.find("parameters");
                            int bonus = 0;
                            if (formula == "minecraft:ore_drops" ||
                                formula == "ore_drops") {
                                const int factor = std::max(
                                    0, random.nextInt(fortune + 2) - 1);
                                count = std::min(
                                    64, boundedCount(
                                        static_cast<std::int64_t>(count) *
                                        (factor + 1)));
                                continue;
                            }
                            if (formula ==
                                    "minecraft:binomial_with_bonus_count" ||
                                formula == "binomial_with_bonus_count") {
                                int extra = 1;
                                double probability = 0.33;
                                if (parameters && parameters->isObj()) {
                                    if (const auto* value =
                                            parameters->find("extra");
                                        value && value->isNum())
                                        extra = value->asInt(1);
                                    if (const auto* value =
                                            parameters->find("probability");
                                        value && value->isNum())
                                        probability = value->number;
                                }
                                const auto rounds = std::clamp<std::int64_t>(
                                    static_cast<std::int64_t>(fortune) + extra,
                                    0, 65536);
                                for (std::int64_t trial = 0;
                                     trial < rounds; ++trial)
                                    if (random.nextFloat() < probability)
                                        ++bonus;
                            } else if (formula ==
                                           "minecraft:uniform_bonus_count" ||
                                       formula == "uniform_bonus_count") {
                                double multiplier = 1.0;
                                if (parameters && parameters->isObj()) {
                                    if (const auto* value =
                                            parameters->find("bonusMultiplier");
                                        value && value->isNum())
                                        multiplier = value->number;
                                }
                                const auto maximum = static_cast<std::int32_t>(
                                    std::clamp(std::floor(multiplier * fortune),
                                               0.0, 65535.0));
                                bonus = random.nextInt(maximum + 1);
                            }
                            count = std::min(64, boundedCount(
                                static_cast<std::int64_t>(count) + bonus));
                        } else if (isFunction("looting_enchant")) {
                            const int level = ctx
                                ? std::clamp(ctx->lootingLevel, 0, 65536) : 0;
                            if (level == 0) continue;
                            LootNumberProvider countProvider{
                                LootNumberProvider::Kind::Uniform, 0.0, 1.0,
                                0, 0.0};
                            if (const auto* providerNode = function.find("count")) {
                                const auto provider =
                                    parseLootNumberProvider(*providerNode);
                                if (!provider) continue;
                                countProvider = *provider;
                            }
                            const auto extra =
                                sampleLootNumberProviderInt(countProvider, random);
                            if (!extra) continue;
                            count = boundedCount(
                                static_cast<std::int64_t>(count) +
                                random.nextInt(level + 1) + *extra);
                        } else if (isFunction(
                                       "enchanted_count_increase")) {
                            const int level = ctx
                                ? std::clamp(ctx->lootingLevel, 0, 65536) : 0;
                            const auto* providerNode = function.find("count");
                            const auto provider = providerNode
                                ? parseLootNumberProvider(*providerNode)
                                : std::nullopt;
                            if (!provider) continue;
                            for (int enchantmentLevel = 0;
                                 enchantmentLevel < level;
                                 ++enchantmentLevel) {
                                const auto increase =
                                    sampleLootNumberProviderInt(*provider,
                                                                random);
                                if (!increase) break;
                                count = boundedCount(
                                    static_cast<std::int64_t>(count) +
                                    *increase);
                            }
                            if (const auto* limit = function.find("limit");
                                limit && limit->isNum() && limit->number > 0)
                                count = std::min(
                                    count, limit->asInt(32767));
                        } else if (isFunction("limit_count")) {
                            int minimum = 0;
                            int maximum = std::numeric_limits<std::int16_t>::max();
                            const auto* limit = function.find("limit");
                            if (limit && limit->isObj()) {
                                if (const auto* value = limit->find("min");
                                    value && value->isNum())
                                    minimum = value->asInt(0);
                                if (const auto* value = limit->find("max");
                                    value && value->isNum())
                                    maximum = value->asInt(maximum);
                            } else if (limit && limit->isArr() &&
                                       limit->arr.size() >= 2 &&
                                       limit->arr[0].isNum() &&
                                       limit->arr[1].isNum()) {
                                minimum = limit->arr[0].asInt(0);
                                maximum = limit->arr[1].asInt(maximum);
                            }
                            if (minimum > maximum) continue;
                            count = std::clamp(count, minimum, maximum);
                        } else if (isFunction("explosion_decay")) {
                            if (!ctx || ctx->explosionRadius <= 0.0f) continue;
                            const double chance = std::clamp(
                                1.0 / ctx->explosionRadius, 0.0, 1.0);
                            int survivors = 0;
                            for (int item = 0; item < count; ++item)
                                if (random.nextFloat() < chance) ++survivors;
                            count = survivors;
                        } else if (isFunction("furnace_smelt")) {
                            if (const auto smelted = smeltResultFor(dropName);
                                !smelted.empty())
                                dropName = smelted;
                        } else if (isFunction("set_damage")) {
                            const auto* providerNode = function.find("damage");
                            const auto provider = providerNode
                                ? parseLootNumberProvider(*providerNode)
                                : std::nullopt;
                            if (!provider) continue;
                            damageFraction = std::clamp(
                                sampleLootNumberProvider(*provider, random),
                                0.0, 1.0);
                            hasDamageFraction = true;
                        } else if (isFunction("set_lore")) {
                            lore.clear();
                            if (const auto* values = function.find("lore");
                                values && values->isArr())
                                for (const auto& value : values->arr)
                                    lore.push_back(value.isStr()
                                        ? value.asStr() : value.dump());
                        } else if (isFunction("set_name") ||
                                   isFunction("set_custom_name")) {
                            if (const auto* value = function.find("name"))
                                displayName = value->isStr()
                                    ? value->asStr() : value->dump();
                            else if (const auto* value = function.find("value"))
                                displayName = value->isStr()
                                    ? value->asStr() : value->dump();
                        } else if (isFunction("enchant_randomly")) {
                            std::vector<std::string> options;
                            if (const auto* values = function.find("options");
                                values && values->isArr())
                                for (const auto& value : values->arr)
                                    if (value.isStr())
                                        options.push_back(value.asStr());
                            if (options.empty())
                                options = {"minecraft:sharpness",
                                    "minecraft:protection",
                                    "minecraft:efficiency",
                                    "minecraft:unbreaking",
                                    "minecraft:fortune", "minecraft:power",
                                    "minecraft:looting"};
                            if (!options.empty())
                                enchantment = std::pair{
                                    options[random.nextInt(static_cast<int>(
                                        options.size()))],
                                    1 + random.nextInt(3)};
                        }
                    }
                } else if (chosen->countIsBinomial) {
                    count = 0;
                    for (int trial = 0; trial < chosen->countBinomN; ++trial)
                        if (random.nextFloat() < chosen->countBinomP) ++count;
                } else if (chosen->countMax > chosen->countMin) {
                    count = chosen->countMin + random.nextInt(
                        chosen->countMax - chosen->countMin + 1);
                }

                if (count <= 0) continue;
                auto iidIt = gen::itemIdByName().find(dropName);
                if (iidIt == gen::itemIdByName().end()) continue;
                auto stack = ItemStack::of(
                    iidIt->second, static_cast<std::int16_t>(count));
                if (hasDamageFraction) {
                    const int maxDamage = ItemStack::maxDamageFor(iidIt->second);
                    if (maxDamage > 0)
                        stack.setDamage(static_cast<int>(
                            maxDamage * damageFraction));
                }
                stack.lore = std::move(lore);
                if (!displayName.empty()) stack.displayNameLoot = displayName;
                if (enchantment)
                    ItemStack::addEnchant(stack, enchantment->first,
                                          enchantment->second);
                out.push_back(std::move(stack));
            }
        }
        return out;
        };
        return evaluate(evaluate, tbl, base, 0);
    }

private:
    std::optional<std::vector<ItemStack>> evaluateRegisteredTableImpl(
        const std::string& tableId, const ItemStack& tool,
        const LootContext* ctx, std::optional<std::int64_t> seed,
        bool allowUnsupportedSemantics) const {
        const auto* table = find(tableId);
        if (!table) return std::nullopt;
        if (allowUnsupportedSemantics) {
            std::unordered_set<std::string> visitedTables;
            if (!containsUnsupportedSemantics(*table, visitedTables)) {
                std::unordered_set<std::string> activeTables;
                if (!isTableSupported(*table, activeTables, 0))
                    return std::nullopt;
            }
        } else {
            std::unordered_set<std::string> activeTables;
            if (!isTableSupported(*table, activeTables, 0))
                return std::nullopt;
        }

        if (!ctx || !ctx->hasEntityContext) {
            std::unordered_set<std::string> activeTables;
            if (tableRequiresEntityContext(*table, activeTables, 0))
                return std::nullopt;
        }

        LootContext effective = ctx ? *ctx : LootContext{};
        if (effective.fortuneLevel == 0 && tool.itemId != 0)
            effective.fortuneLevel = tool.fortuneLevel();
        if (effective.lootingLevel == 0 && tool.itemId != 0)
            effective.lootingLevel = tool.lootingLevel();
        const auto colon = tableId.find(':');
        const auto base = colon == std::string::npos
            ? tableId : tableId.substr(colon + 1);
        return evaluateTable(*table, tool, &effective, base, seed);
    }

    static bool isLootConditionSupported(const json::Value& condition) {
        if (!condition.isObj()) return false;
        const auto* conditionName = condition.find("condition");
        if (!conditionName || !conditionName->isStr()) return false;
        const auto& name = conditionName->asStr();
        if (name == "minecraft:any_of" || name == "any_of" ||
            name == "minecraft:all_of" || name == "all_of") {
            const auto* terms = condition.find("terms");
            if (!terms || !terms->isArr()) return false;
            return std::all_of(terms->arr.begin(), terms->arr.end(),
                [](const json::Value& term) {
                    return isLootConditionSupported(term);
                });
        }
        if (name == "minecraft:killed_by_player" ||
            name == "minecraft:survives_explosion")
            return true;
        if (name == "minecraft:random_chance" || name == "random_chance") {
            const auto* chanceValue = condition.find("chance");
            const auto chance = chanceValue
                ? parseLootNumberProvider(*chanceValue) : std::nullopt;
            return chance && chance->minimum >= 0.0 &&
                chance->maximum <= 1.0;
        }
        if (name == "minecraft:random_chance_with_looting" ||
            name == "random_chance_with_looting") {
            const auto* chanceValue = condition.find("chance");
            const auto* multiplier = condition.find("looting_multiplier");
            const auto chance = chanceValue
                ? parseLootNumberProvider(*chanceValue) : std::nullopt;
            return chance && chance->minimum >= 0.0 &&
                chance->maximum <= 1.0 &&
                multiplier && multiplier->isNum() &&
                std::isfinite(multiplier->number) &&
                multiplier->number >= 0.0;
        }
        if (name == "minecraft:random_chance_with_enchanted_bonus" ||
            name == "random_chance_with_enchanted_bonus") {
            const auto* base = condition.find("unenchanted_chance");
            const auto* enchanted = condition.find("enchanted_chance");
            const auto* enchantment = condition.find("enchantment");
            if (!base || !base->isNum() || !std::isfinite(base->number) ||
                base->number < 0.0 || base->number > 1.0 ||
                !enchanted || !enchanted->isObj() ||
                !enchantment || !enchantment->isStr() ||
                (enchantment->asStr() != "fortune" &&
                 enchantment->asStr() != "minecraft:fortune" &&
                 enchantment->asStr() != "looting" &&
                 enchantment->asStr() != "minecraft:looting"))
                return false;
            const auto* type = enchanted->find("type");
            const auto* linearBase = enchanted->find("base");
            const auto* perLevel =
                enchanted->find("per_level_above_first");
            return type && type->isStr() &&
                type->asStr() == "minecraft:linear" &&
                linearBase && linearBase->isNum() &&
                std::isfinite(linearBase->number) &&
                perLevel && perLevel->isNum() &&
                std::isfinite(perLevel->number);
        }
        if (name != "minecraft:entity_properties") return false;
        const auto* entity = condition.find("entity");
        if (entity && (!entity->isStr() || entity->asStr() != "this"))
            return false;
        const auto* predicate = condition.find("predicate");
        if (!predicate || !predicate->isObj()) return false;
        const auto* onFire = predicate->find("on_fire");
        if (onFire) {
            if (!onFire->isBool() || predicate->obj.size() != 1)
                return false;
            return true;
        }
        const auto* flags = predicate->find("flags");
        if (!flags || !flags->isObj() || predicate->obj.size() != 1)
            return false;
        const auto* isOnFire = flags->find("is_on_fire");
        return isOnFire && isOnFire->isBool() && flags->obj.size() == 1;
    }

    static bool lootConditionRequiresEntityContext(
        const json::Value& condition) {
        if (!condition.isObj()) return false;
        const auto* conditionName = condition.find("condition");
        if (!conditionName || !conditionName->isStr()) return false;
        const auto& name = conditionName->asStr();
        if (name == "minecraft:any_of" || name == "any_of" ||
            name == "minecraft:all_of" || name == "all_of") {
            const auto* terms = condition.find("terms");
            return terms && terms->isArr() &&
                std::any_of(terms->arr.begin(), terms->arr.end(),
                    [](const json::Value& term) {
                        return lootConditionRequiresEntityContext(term);
                    });
        }
        return name == "minecraft:entity_properties" ||
               name == "entity_properties" ||
               name == "minecraft:killed_by_player" ||
               name == "killed_by_player";
    }

    static bool conditionListRequiresEntityContext(
        const json::Value& conditions) {
        if (!conditions.isArr()) return false;
        return std::any_of(conditions.arr.begin(), conditions.arr.end(),
            [](const json::Value& condition) {
                return lootConditionRequiresEntityContext(condition);
            });
    }

    static bool functionsRequireEntityContext(const json::Value& functions) {
        if (!functions.isArr()) return false;
        for (const auto& function : functions.arr) {
            if (!function.isObj()) continue;
            const auto* conditions = function.find("conditions");
            if (conditions &&
                conditionListRequiresEntityContext(*conditions))
                return true;
        }
        return false;
    }

    bool tableRequiresEntityContext(
        const LootTable& table,
        std::unordered_set<std::string>& activeTables,
        std::size_t depth) const {
        constexpr std::size_t kMaximumNestingDepth = 32;
        if (depth > kMaximumNestingDepth || table.id.empty() ||
            !activeTables.emplace(table.id).second)
            return false;
        struct ActiveTableGuard {
            std::unordered_set<std::string>& active;
            const std::string& id;
            ~ActiveTableGuard() { active.erase(id); }
        } guard{activeTables, table.id};

        if (conditionListRequiresEntityContext(table.conditionsJson))
            return true;
        for (const auto& pool : table.pools) {
            if (conditionListRequiresEntityContext(pool.conditionsJson))
                return true;
            for (const auto& entry : pool.entries) {
                if (conditionListRequiresEntityContext(
                        entry.conditionsJson) ||
                    functionsRequireEntityContext(entry.functionsJson))
                    return true;
                if (entry.type != "minecraft:loot_table") continue;
                const auto* nested = find(entry.name);
                if (nested && tableRequiresEntityContext(
                        *nested, activeTables, depth + 1))
                    return true;
            }
        }
        return false;
    }

    bool isTableSupported(
        const LootTable& table,
        std::unordered_set<std::string>& activeTables,
        std::size_t depth) const {
        constexpr std::size_t kMaximumNestingDepth = 32;
        if (!table.supported || depth > kMaximumNestingDepth ||
            table.id.empty() ||
            !activeTables.emplace(table.id).second)
            return false;
        struct ActiveTableGuard {
            std::unordered_set<std::string>& active;
            const std::string& id;
            ~ActiveTableGuard() { active.erase(id); }
        } guard{activeTables, table.id};

        for (const auto& pool : table.pools) {
            if (!pool.supported) return false;
            for (const auto& entry : pool.entries) {
                if (!entry.supported) return false;
                if (entry.type == "minecraft:item") {
                    if (!gen::itemIdByName().contains(entry.name))
                        return false;
                    continue;
                }
                if (entry.type == "minecraft:empty") continue;
                if (entry.type != "minecraft:loot_table") return false;
                const auto* nested = find(entry.name);
                if (!nested ||
                    !isTableSupported(*nested, activeTables, depth + 1))
                    return false;
            }
        }
        return true;
    }

    bool containsUnsupportedSemantics(
        const LootTable& table,
        std::unordered_set<std::string>& visited) const {
        if (table.suppressDefaultDropFallback) return true;
        if (!visited.emplace(table.id).second) return false;
        for (const auto& pool : table.pools) {
            for (const auto& entry : pool.entries) {
                if (entry.type != "minecraft:loot_table") continue;
                const auto* nested = find(entry.name);
                if (nested && containsUnsupportedSemantics(*nested, visited)) {
                    visited.erase(table.id);
                    return true;
                }
            }
        }
        visited.erase(table.id);
        return false;
    }

    const LootTable* findBlockTableCandidate(const std::string& id) const {
        const auto* table = find(id);
        if (!table) return nullptr;
        std::unordered_set<std::string> activeTables;
        if (isTableSupported(*table, activeTables, 0)) return table;
        std::unordered_set<std::string> visited;
        return containsUnsupportedSemantics(*table, visited) ? table : nullptr;
    }

    void applyFunctions(const json::Value& funcs, LootEntry& ent){
        for(auto& fn: funcs.arr){
            if(!fn.isObj()) {
                ent.supported = false;
                continue;
            }
            std::string ftype=fn.at("function").asStr();
            const auto isFunction = [&ftype](const char* name) {
                return ftype == name ||
                    ftype == std::string("minecraft:") + name;
            };
            if (const auto* conditions = fn.find("conditions")) {
                bool supportedConditions = conditions->isArr();
                if (supportedConditions) {
                    for (const auto& condition : conditions->arr)
                        if (!isLootConditionSupported(condition)) {
                            supportedConditions = false;
                            break;
                        }
                }
                if (!supportedConditions)
                    ent.supported = false;
                else if (isFunction("furnace_smelt"))
                    ent.furnaceSmeltConditionsJson = *conditions;
            }
            if(isFunction("explosion_decay")){
                ent.explosionDecay = true;
            } else if(isFunction("furnace_smelt")){
                ent.furnaceSmelt = true;
            } else if(isFunction("copy_components")){
                // Source components are block-entity-specific and require a
                // real predicate/copy context; do not retain an unapplied stub.
                ent.supported = false;
            } else if(isFunction("set_count")){
                const auto& cnt=fn.at("count");
                const auto provider = parseLootNumberProvider(cnt);
                if (!provider || provider->minimum < 0.0 ||
                    provider->maximum > 65536.0) {
                    ent.supported = false;
                } else if (provider->kind ==
                           LootNumberProvider::Kind::Constant) {
                    ent.countMin = ent.countMax = static_cast<int>(
                        std::floor(provider->minimum));
                } else if (provider->kind ==
                           LootNumberProvider::Kind::Uniform) {
                    ent.countMin = static_cast<int>(
                        std::floor(provider->minimum));
                    ent.countMax = static_cast<int>(
                        std::floor(provider->maximum));
                } else {
                    ent.countIsBinomial = true;
                    ent.countMin = 0;
                    ent.countBinomN = provider->trials;
                    ent.countBinomP = provider->probability;
                }
                if (const auto* add = fn.find("add");
                    add && !add->isBool())
                    ent.supported = false;
            } else if(isFunction("enchant_randomly")){
                ent.enchantRandomly = true;
                if(auto* opts=fn.find("options")) if(opts->isArr()) for(auto& o: opts->arr) if(o.isStr()) ent.enchantOptions.push_back(o.asStr());
            } else if(isFunction("fill_player_head")){
                ent.supported = false;
            } else if(isFunction("apply_bonus")){
                ent.applyBonusOre = true;
                if(auto* f=fn.find("formula")) ent.applyBonusFormula = f->asStr();
                if(auto* e=fn.find("enchantment")) ent.applyBonusEnchant = e->asStr();
                if (ent.applyBonusEnchant != "minecraft:fortune")
                    ent.supported = false;
                if(auto* pf=fn.find("parameters")){
                    if(auto* bm=pf->find("bonusMultiplier")) ent.bonusMultiplier = bm->asFloat(1.0f);
                    if(auto* ex=pf->find("extra")) ent.applyBonusN = ex->asInt(1);
                    if(auto* p=pf->find("probability")) ent.applyBonusP = p->asFloat(0.33f);
                }
                if (ent.applyBonusFormula == "minecraft:ore_drops") {
                    ent.applyBonusOre = true;
                } else if (ent.applyBonusFormula ==
                           "minecraft:binomial_with_bonus_count") {
                    ent.hasApplyBonusBinomial = true;
                    if (ent.applyBonusN < 0 ||
                        !std::isfinite(ent.applyBonusP) ||
                        ent.applyBonusP < 0.0 || ent.applyBonusP > 1.0)
                        ent.supported = false;
                } else if (ent.applyBonusFormula ==
                           "minecraft:uniform_bonus_count") {
                    ent.hasApplyBonusUniform = true;
                    ent.applyBonusExtra = ent.bonusMultiplier;
                    if (!std::isfinite(ent.applyBonusExtra) ||
                        ent.applyBonusExtra < 0.0 ||
                        std::floor(ent.applyBonusExtra) !=
                            ent.applyBonusExtra)
                        ent.supported = false;
                } else {
                    ent.supported = false;
                }
            } else if(isFunction("looting_enchant")){
                ent.lootingEnchant = true;
                if(auto* cnt=fn.find("count")){
                    if(cnt->isNum()) ent.lootingMin=ent.lootingMax=cnt->asInt(1);
                    else if(cnt->isObj()){
                        if(auto* mn=cnt->find("min")) ent.lootingMin=mn->asInt(0);
                        if(auto* mx=cnt->find("max")) ent.lootingMax=mx->asInt(1);
                        if(auto* c=cnt->find("count")) { ent.lootingMin=ent.lootingMax=c->asInt(1); }
                    }
                } else {
                    ent.lootingMin=0; ent.lootingMax=1;
                }
            } else if(isFunction("enchant_with_levels")){
                // Its level provider and treasure rules are not implemented.
                // Never substitute the unrelated enchant_randomly behavior.
                ent.supported = false;
            } else if(isFunction("enchanted_count_increase")){
                const auto* enchantment = fn.find("enchantment");
                const auto* count = fn.find("count");
                const auto provider = count
                    ? parseLootNumberProvider(*count) : std::nullopt;
                if (!enchantment || !enchantment->isStr() ||
                    enchantment->asStr() != "minecraft:looting" ||
                    !provider || provider->minimum < 0.0 ||
                    provider->maximum > 32767.0) {
                    ent.supported = false;
                }
                if (const auto* limit = fn.find("limit");
                    limit && (!limit->isNum() ||
                        !std::isfinite(limit->number) ||
                        std::round(limit->number) != limit->number ||
                        limit->number < 0 || limit->number > 32767))
                    ent.supported = false;
            } else if(isFunction("limit_count")){
                ent.hasLimit=true;
                if(auto* l=fn.find("limit")){
                    if(l->isNum()) ent.limitMin=ent.limitMax=l->asInt(1);
                    else if(l->isObj()){ if(auto* mn=l->find("min")) ent.limitMin=mn->asInt(1); if(auto* mx=l->find("max")) ent.limitMax=mx->asInt(64); if(ent.limitMax<ent.limitMin) ent.limitMax=ent.limitMin; }
                    else if(l->isArr() && l->arr.size()>=2){ ent.limitMin=l->arr[0].asInt(1); ent.limitMax=l->arr[1].asInt(64); }
                } else {
                    if(auto* mn=fn.find("min")) ent.limitMin=mn->asInt(1);
                    if(auto* mx=fn.find("max")) ent.limitMax=mx->asInt(64);
                }
            } else if(isFunction("set_damage")){
                ent.hasSetDamage=true;
                if(auto* d=fn.find("damage")){
                    if(d->isNum()) ent.setDamageMin=ent.setDamageMax=d->asFloat(0);
                    else if(d->isObj()){ if(auto* mn=d->find("min")) ent.setDamageMin=mn->asFloat(0); if(auto* mx=d->find("max")) ent.setDamageMax=mx->asFloat(0); }
                }
            } else if(isFunction("set_attributes")){
                ent.supported = false;
            } else if(isFunction("set_nbt") || isFunction("set_components")){
                ent.supported = false;
            } else if(isFunction("set_lore")){
                ent.hasSetLore=true;
                if(auto* lore=fn.find("lore")) if(lore->isArr()) for(auto& l: lore->arr) ent.setLore.push_back(l.isStr()?l.asStr():l.dump());
            } else if(isFunction("set_name") || isFunction("set_custom_name")){
                if(auto* nm=fn.find("name")) ent.setName=nm->asStr(); else if(auto* v=fn.find("value")) ent.setName=v->asStr();
                ent.hasSetLore=true;
            } else {
                ent.supported = false;
            }
        }
    }
    void ensureBuiltin(const std::string& tblId,const std::string& drop){
        if(tables_.count(tblId)==0){ LootTable t; t.id=tblId; LootPool p; LootEntry e; e.name=drop; p.entries.push_back(e); t.pools.push_back(p); tables_[tblId]=t; }
    }
    void loadBuiltins(){
        tables_.clear();
        auto add=[&](const char* tblId,const char* drop,int cmin=1,int cmax=1){
            LootTable t; t.id=tblId; LootPool p; LootEntry e; e.name=drop; e.countMin=cmin; e.countMax=cmax; p.entries.push_back(e); t.pools.push_back(p); tables_[t.id]=t;
        };
        add("minecraft:blocks/stone","minecraft:cobblestone");
        add("minecraft:blocks/grass_block","minecraft:dirt");
        add("minecraft:blocks/dirt","minecraft:dirt");
        add("minecraft:blocks/sand","minecraft:sand");
        add("minecraft:blocks/gravel","minecraft:gravel");
        add("minecraft:blocks/oak_log","minecraft:oak_log");
        add("minecraft:blocks/cobblestone","minecraft:cobblestone");
        add("minecraft:blocks/glass","minecraft:glass");
        add("minecraft:blocks/coal_ore","minecraft:coal",1,1);
        add("minecraft:blocks/iron_ore","minecraft:raw_iron",1,1);
        add("minecraft:blocks/diamond_ore","minecraft:diamond",1,1);
        add("minecraft:blocks/deepslate","minecraft:cobbled_deepslate");
        add("minecraft:blocks/netherrack","minecraft:netherrack");
        add("minecraft:blocks/obsidian","minecraft:obsidian");
    }
    Map tables_;
    std::vector<std::string> loadDiagnostics_;
};

} // namespace cppfm
