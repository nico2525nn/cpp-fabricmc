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

    // Evaluate returns list of ItemStacks for blockName using tool (silk_touch/fortune).
    // Replaces hard-coded kOv drop table in tickDigs (issue 65).
    std::vector<ItemStack> evaluate(const std::string& blockName, const ItemStack& tool = {}) const {
        std::string base = blockName.find(':')!=std::string::npos ? blockName.substr(blockName.find(':')+1) : blockName;
        std::string id = "minecraft:blocks/" + base;
        auto it = tables_.find(id);
        if (it==tables_.end()) it=tables_.find(blockName);
        if (it==tables_.end()) return {};
        const LootTable& tbl=it->second;
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
                auto iidIt=gen::itemIdByName().find(chosen->name);
                if(iidIt==gen::itemIdByName().end()) continue;
                int cnt=chosen->countMin;
                if(chosen->countMax>chosen->countMin) cnt=chosen->countMin + rand()%(chosen->countMax-chosen->countMin+1);
                // fortune bonus for ores (simplified vanilla bonus_ore_drops)
                if(tool.itemId!=0){
                    int fortune=tool.fortuneLevel();
                    if(fortune>0 && (chosen->name.find("ore")!=std::string::npos || base.find("ore")!=std::string::npos)){
                        int bonus=rand()%(fortune+1);
                        cnt+=bonus;
                    }
                }
                if(cnt<=0) cnt=1;
                out.push_back(ItemStack::of(iidIt->second, static_cast<int16_t>(cnt)));
            }
        }
        return out;
    }

private:
    void applyFunctions(const json::Value& funcs, LootEntry& ent){
        for(auto& fn: funcs.arr){
            std::string ftype=fn.at("function").asStr();
            if(ftype.find("set_count")!=std::string::npos){
                const auto& cnt=fn.at("count");
                if(cnt.isNum()) ent.countMin=ent.countMax=cnt.asInt(1);
                else if(cnt.isObj()){
                    ent.countMin=cnt.at("min").asInt(1);
                    ent.countMax=cnt.at("max").asInt(1);
                    if(ent.countMax<ent.countMin) ent.countMax=ent.countMin;
                }
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
