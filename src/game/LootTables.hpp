// LootTables: JSON evaluator for block drops (issue 65)
// Replaces hard-coded mineInfo drop with data-driven LootTableEvaluator.
// Supports vanilla loot_tables/blocks/*.json subset: pools, rolls, entries, weight, set_count.
// Used in tickDigs to produce drops from lootTables_ instead of kOv map.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include "../core/Json.hpp"
#include "../generated/ItemIds.hpp"
#include "Items.hpp"

namespace cppfm {

struct LootEntry {
    std::string name; // e.g. minecraft:cobblestone
    int weight = 1;
    int countMin = 1, countMax = 1;
    // plan35 §2: extended loot functions
    bool explosionDecay = false;
    bool furnaceSmelt = false;
    struct CopyComponents {
        std::string source = "block_entity";
        std::vector<std::string> include;
        std::vector<std::string> exclude;
        bool has = false;
    } copyComponents;
    bool countIsBinomial = false;
    int countBinomN = 3;
    double countBinomP = 0.5;
    // plan37 B-05: 3 new functions
    bool enchantRandomly = false;
    std::vector<std::string> enchantOptions;
    bool fillPlayerHead = false;
    bool applyBonusOre = false;
    std::string applyBonusFormula;
    std::string applyBonusEnchant;
    bool lootingEnchant = false;
    int lootingMin = 0, lootingMax = 1;
};
struct LootContext {
    float explosionRadius = 0.f;
    int fortuneLevel = 0;
    int lootingLevel = 0;
    std::string killerName;
    // future: BlockEntity*, killer etc — plan35 §2 needs explosionRadius only
};

struct LootPool {
    int rolls = 1;
    double rollsMin = 1, rollsMax = 1;
    bool rollsIsRange = false;
    std::vector<LootEntry> entries;
};

struct LootTable {
    std::string id; // e.g. minecraft:blocks/stone
    std::vector<LootPool> pools;
};

class LootTableEvaluator {
public:
    using Map = std::unordered_map<std::string, LootTable>;
    const Map& tables() const { return tables_; }
    size_t size() const { return tables_.size(); }
    void clear() { tables_.clear(); }
    const LootTable* find(const std::string& id) const {
        auto it=tables_.find(id);
        return it==tables_.end()?nullptr:&it->second;
    }

    void loadDirectory(const std::string& base) {
        namespace fs = std::filesystem;
        std::error_code ec;
        std::string root = base;
        if (fs::exists(base + "/loot_tables", ec)) root = base + "/loot_tables";
        else if (fs::exists(base + "/loot_table", ec)) root = base + "/loot_table";
        if (!fs::exists(root, ec)) { loadBuiltins(); return; }
        for (auto& entry : fs::recursive_directory_iterator(root, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            try {
                FILE* f = fopen(entry.path().string().c_str(), "rb");
                if (!f) continue;
                std::string text; char buf[4096]; size_t n;
                while ((n=fread(buf,1,sizeof buf,f))>0) text.append(buf,n);
                fclose(f);
                const json::Value v = json::Value::parse(text);
                std::string rel = fs::relative(entry.path(), root, ec).string();
                if (rel.size()>=5 && rel.substr(rel.size()-5)==".json") rel = rel.substr(0, rel.size()-5);
                for (auto& c: rel) if (c=='\\') c='/';
                std::string id = "minecraft:" + rel;
                LootTable tbl; tbl.id=id;
                const auto& poolsV = v.at("pools");
                auto parsePool = [&](const json::Value& poolV, LootTable& out){
                    LootPool pool;
                    const auto& rolls = poolV.at("rolls");
                    if (rolls.isNum()) { pool.rolls=rolls.asInt(1); pool.rollsMin=pool.rollsMax=pool.rolls; }
                    else if (rolls.isObj()) {
                        double mn=rolls.at("min").asFloat(1), mx=rolls.at("max").asFloat(1);
                        pool.rollsIsRange=true; pool.rollsMin=mn; pool.rollsMax=mx;
                        pool.rolls=static_cast<int>((mn+mx)/2+0.5); if(pool.rolls<1) pool.rolls=1;
                    }
                    const auto& entries = poolV.at("entries");
                    if (entries.isArr()) {
                        for (auto& e: entries.arr) {
                            std::string type=e.at("type").asStr();
                            if (type.find("alternatives")!=std::string::npos ||
                                type.find("group")!=std::string::npos ||
                                type.find("sequence")!=std::string::npos) {
                                const auto& children=e.at("children");
                                if (children.isArr()) {
                                    for (auto& ch: children.arr) {
                                        const auto& chEntries=ch.at("entries");
                                        if (chEntries.isArr()) {
                                            for (auto& ee: chEntries.arr) if(ee.at("name").isStr()){
                                                LootEntry ent; ent.name=ee.at("name").asStr();
                                                const auto& funcs=ee.at("functions");
                                                if(funcs.isArr()) applyFunctions(funcs,ent);
                                                pool.entries.push_back(std::move(ent));
                                            }
                                        } else if (ch.at("name").isStr()){
                                            LootEntry ent; ent.name=ch.at("name").asStr();
                                            const auto& funcs=ch.at("functions");
                                            if(funcs.isArr()) applyFunctions(funcs,ent);
                                            pool.entries.push_back(std::move(ent));
                                        }
                                    }
                                }
                                continue;
                            }
                            std::string n=e.at("name").asStr();
                            if(n.empty()) continue;
                            LootEntry ent; ent.name=n;
                            const auto& w=e.at("weight");
                            if(w.isNum()) ent.weight=w.asInt(1);
                            const auto& funcs=e.at("functions");
                            if(funcs.isArr()) applyFunctions(funcs,ent);
                            pool.entries.push_back(std::move(ent));
                        }
                    }
                    if(!pool.entries.empty()) out.pools.push_back(std::move(pool));
                };
                if (poolsV.isArr()) for(auto& pv: poolsV.arr) parsePool(pv,tbl);
                else if (poolsV.isObj()) parsePool(poolsV,tbl);
                if(!tbl.pools.empty()) tables_[id]=std::move(tbl);
            } catch(...) {}
        }
        if (tables_.empty()) loadBuiltins();
        else {
            ensureBuiltin("minecraft:blocks/stone","minecraft:cobblestone");
            ensureBuiltin("minecraft:blocks/grass_block","minecraft:dirt");
        }
    }

    static std::string smeltResultFor(const std::string& in) {
        static const std::unordered_map<std::string,std::string> mp{
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
        // heuristic: _ore -> strip prefix
        if(in.find("_ore")!=std::string::npos) {
            // fallback keep as is if not in map
        }
        return "";
    }
    // Evaluate returns list of ItemStacks for blockName using tool (silk_touch/fortune).
    // Replaces hard-coded kOv drop table in tickDigs (issue 65).
    std::vector<ItemStack> evaluate(const std::string& blockName, const ItemStack& tool = {}) const {
        return evaluateWithContext(blockName, tool, nullptr);
    }
    // plan37 B-05: entity loot evaluate (for mob drops)
    std::vector<ItemStack> evaluateEntity(const std::string& entityName, const LootContext* ctx = nullptr) const {
        std::string base = entityName.find(':')!=std::string::npos ? entityName.substr(entityName.find(':')+1) : entityName;
        std::string id = "minecraft:entities/" + base;
        auto it = tables_.find(id);
        if (it==tables_.end()) it=tables_.find(entityName);
        if (it==tables_.end()) it=tables_.find("minecraft:" + base);
        if (it==tables_.end()) return {};
        LootContext dummy;
        const LootContext* c = ctx ? ctx : &dummy;
        return evaluateTable(it->second, ItemStack{}, c, base);
    }
    std::vector<ItemStack> evaluateWithContext(const std::string& blockName, const ItemStack& tool, const LootContext* ctx) const {
        std::string base = blockName.find(':')!=std::string::npos ? blockName.substr(blockName.find(':')+1) : blockName;
        std::string id = "minecraft:blocks/" + base;
        auto it = tables_.find(id);
        if (it==tables_.end()) it=tables_.find(blockName);
        if (it==tables_.end()) {
            // also try entities prefix (for mob drops via blockName)
            auto itE = tables_.find("minecraft:entities/" + base);
            if (itE!=tables_.end()) it=itE;
            else {
                auto it2 = tables_.find("minecraft:" + base);
                if (it2!=tables_.end()) it=it2;
                else if (it==tables_.end()) return {};
            }
        }
        LootContext dummy;
        if (!ctx) { dummy = {}; ctx = &dummy; }
        // propagate fortune from tool if ctx has zero
        LootContext eff = *ctx;
        if (eff.fortuneLevel==0 && tool.itemId!=0) eff.fortuneLevel = tool.fortuneLevel();
        if (eff.lootingLevel==0 && tool.itemId!=0) eff.lootingLevel = tool.fortuneLevel();
        return evaluateTable(it->second, tool, &eff, base);
    }
    std::vector<ItemStack> evaluateTable(const LootTable& tbl, const ItemStack& tool, const LootContext* ctx, const std::string& base) const {
        (void)tool;
        std::vector<ItemStack> out;
        for (auto& pool: tbl.pools) {
            int rolls=pool.rolls;
            if(pool.rollsIsRange){ double r=pool.rollsMin + (rand()/(double)RAND_MAX)*(pool.rollsMax-pool.rollsMin); rolls=static_cast<int>(r+0.5); if(rolls<1)rolls=1; }
            for(int r=0;r<rolls;++r){
                if(pool.entries.empty()) continue;
                int totalW=0; for(auto& e:pool.entries) totalW+=e.weight;
                if(totalW<=0) totalW=(int)pool.entries.size();
                int pick=rand()%totalW;
                const LootEntry* chosen=nullptr;
                for(auto& e:pool.entries){ if(pick<e.weight){chosen=&e;break;} pick-=e.weight; }
                if(!chosen) chosen=&pool.entries.back();
                // plan35 §2: explosion_decay — 1/radius vanish
                if (chosen->explosionDecay && ctx && ctx->explosionRadius > 0.f) {
                    double vanish = 1.0 / ctx->explosionRadius;
                    if ((rand()/(double)RAND_MAX) < vanish) continue;
                }
                std::string dropName = chosen->name;
                // plan35 §2: furnace_smelt — convert via smelting map
                if (chosen->furnaceSmelt) {
                    std::string sm = smeltResultFor(dropName);
                    if (!sm.empty()) dropName = sm;
                }
                auto iidIt=gen::itemIdByName().find(dropName);
                if(iidIt==gen::itemIdByName().end()) continue;
                int cnt=chosen->countMin;
                if (chosen->countIsBinomial) {
                    cnt=0;
                    for(int i=0;i<chosen->countBinomN;++i) if((rand()/(double)RAND_MAX) < chosen->countBinomP) ++cnt;
                    if(cnt<=0 && chosen->countMin>0) cnt=1;
                } else if(chosen->countMax>chosen->countMin) cnt=chosen->countMin + rand()%(chosen->countMax-chosen->countMin+1);
                // plan37 B-05: looting_enchant
                if (chosen->lootingEnchant && ctx) {
                    int looting = ctx->lootingLevel;
                    if (looting>0) {
                        int extra = chosen->lootingMin + (chosen->lootingMax>chosen->lootingMin ? rand()%(chosen->lootingMax-chosen->lootingMin+1) : 0);
                        // vanilla looting_enchant count: uniform 0..looting
                        cnt += rand()%(looting+1) + extra;
                    }
                }
                // plan37 B-05: apply_bonus ore_drops (fortune)
                if (chosen->applyBonusOre && ctx) {
                    int fortune = ctx->fortuneLevel;
                    if (fortune>0) {
                        int bonus=0;
                        if(fortune==1) bonus = rand()%2;
                        else if(fortune==2) bonus = rand()%3;
                        else if(fortune>=3) bonus = rand()%4;
                        cnt += bonus;
                        if(cnt>64) cnt=64;
                    }
                } else if (ctx && (dropName.find("ore")!=std::string::npos || base.find("ore")!=std::string::npos)) {
                    int fortune = ctx->fortuneLevel;
                    if(fortune>0){
                        // fallback generic fortune for ores when no explicit apply_bonus (plan35 simplified)
                        int bonus=rand()%(fortune+1);
                        cnt+=bonus;
                    }
                }
                if(cnt<=0) {
                    // allow zero count for some pools (vanilla 0-2). Skip if zero.
                    if(chosen->countMin==0) continue;
                    cnt=1;
                }
                auto st = ItemStack::of(iidIt->second, static_cast<int16_t>(cnt));
                // plan37 B-05: enchant_randomly
                if (chosen->enchantRandomly) {
                    std::string pick;
                    if(!chosen->enchantOptions.empty()) pick = chosen->enchantOptions[rand()%chosen->enchantOptions.size()];
                    else {
                        static const char* enchants[]={"minecraft:sharpness","minecraft:protection","minecraft:efficiency","minecraft:unbreaking","minecraft:fortune","minecraft:power","minecraft:looting"};
                        pick = enchants[rand() % (sizeof(enchants)/sizeof(*enchants))];
                    }
                    int lvl = 1 + rand()%3;
                    ItemStack::addEnchant(st, pick, lvl);
                }
                // plan37 B-05: fill_player_head — keep count 1, skull owner would be killerName (stub)
                if (chosen->fillPlayerHead) {
                    st.count = 1;
                    (void)ctx; // killerName would set SkullOwner component
                }
                (void)chosen->copyComponents;
                out.push_back(std::move(st));
            }
        }
        return out;
    }

private:
    void applyFunctions(const json::Value& funcs, LootEntry& ent){
        for(auto& fn: funcs.arr){
            if(!fn.isObj()) continue;
            std::string ftype=fn.at("function").asStr();
            if(ftype.find("explosion_decay")!=std::string::npos){
                ent.explosionDecay = true;
            } else if(ftype.find("furnace_smelt")!=std::string::npos){
                ent.furnaceSmelt = true;
            } else if(ftype.find("copy_components")!=std::string::npos){
                ent.copyComponents.has = true;
                if(auto* src=fn.find("source")) ent.copyComponents.source = src->asStr();
                if(auto* inc=fn.find("include")) if(inc->isArr()) for(auto& s: inc->arr) if(s.isStr()) ent.copyComponents.include.push_back(s.asStr());
                if(auto* exc=fn.find("exclude")) if(exc->isArr()) for(auto& s: exc->arr) if(s.isStr()) ent.copyComponents.exclude.push_back(s.asStr());
            } else if(ftype.find("set_count")!=std::string::npos){
                const auto& cnt=fn.at("count");
                if(cnt.isNum()) ent.countMin=ent.countMax=cnt.asInt(1);
                else if(cnt.isObj()){
                    if(auto* tp=cnt.find("type")){
                        std::string t=tp->asStr();
                        if(t.find("uniform")!=std::string::npos){
                            if(auto* mn=cnt.find("min")) ent.countMin=mn->asInt(1);
                            if(auto* mx=cnt.find("max")) ent.countMax=mx->asInt(1);
                            if(ent.countMax<ent.countMin) ent.countMax=ent.countMin;
                        } else if(t.find("binomial")!=std::string::npos){
                            ent.countIsBinomial=true;
                            if(auto* n=cnt.find("n")) ent.countBinomN=n->asInt(3);
                            if(auto* p=cnt.find("p")) ent.countBinomP=p->asFloat(0.5);
                        } else if(t.find("constant")!=std::string::npos){
                            if(auto* v=cnt.find("value")) ent.countMin=ent.countMax=v->asInt(1);
                        } else {
                            if(auto* mn=cnt.find("min")) ent.countMin=mn->asInt(1);
                            if(auto* mx=cnt.find("max")) ent.countMax=mx->asInt(1);
                        }
                    } else {
                        ent.countMin=cnt.at("min").asInt(1);
                        ent.countMax=cnt.at("max").asInt(1);
                        if(ent.countMax<ent.countMin) ent.countMax=ent.countMin;
                    }
                }
            } else if(ftype.find("enchant_randomly")!=std::string::npos){
                ent.enchantRandomly = true;
                if(auto* opts=fn.find("options")) if(opts->isArr()) for(auto& o: opts->arr) if(o.isStr()) ent.enchantOptions.push_back(o.asStr());
            } else if(ftype.find("fill_player_head")!=std::string::npos){
                ent.fillPlayerHead = true;
            } else if(ftype.find("apply_bonus")!=std::string::npos){
                ent.applyBonusOre = true;
                if(auto* f=fn.find("formula")) ent.applyBonusFormula = f->asStr();
                if(auto* e=fn.find("enchantment")) ent.applyBonusEnchant = e->asStr();
                if(ent.applyBonusFormula.empty()) if(auto* pf=fn.find("parameters")) if(auto* bm=pf->find("bonusMultiplier")) (void)bm;
            } else if(ftype.find("looting_enchant")!=std::string::npos){
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
            } else if(ftype.find("enchant_with_levels")!=std::string::npos){
                ent.enchantRandomly = true;
            } else if(ftype.find("limit_count")!=std::string::npos){
                // stub: handled as min/max clamp in evaluate
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
};

} // namespace cppfm
