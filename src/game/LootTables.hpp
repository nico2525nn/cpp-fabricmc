// LootTables: JSON evaluator for block drops (issue 65) Replaces hard-coded mineInfo drop with data-driven LootTableEvaluator. Supports
// vanilla loot_tables/blocks/*.json subset: pools, rolls, entries, weight, set_count. Used in tickDigs to produce drops from lootTables_
// instead of kOv map.
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

struct LootAttribute { std::string name, attribute, operation; double amount = 0; };
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
    // plan40 C-05: extended functions
    bool hasLimit = false; int limitMin = 1, limitMax = 64;
    bool hasApplyBonusBinomial = false, hasApplyBonusUniform = false;
    int applyBonusN = 1; double applyBonusP = 0.33; double bonusMultiplier = 1.0; double applyBonusExtra = 0;
    bool hasSetDamage = false; double setDamageMin = 0, setDamageMax = 0;
    bool hasSetAttributes = false; std::vector<LootAttribute> setAttributes;
    bool hasSetNbt = false; std::string setNbt;
    bool hasSetLore = false; std::vector<std::string> setLore;
    std::string setName;
};
struct LootContext {
    float explosionRadius = 0.f;
    int fortuneLevel = 0;
    int lootingLevel = 0;
    std::string killerName;
    bool isPlayerKill = false;
    // future: BlockEntity*, killer etc — plan35 §2 needs explosionRadius only
};

struct LootPool {
    int rolls = 1;
    double rollsMin = 1, rollsMax = 1;
    bool rollsIsRange = false;
    std::vector<LootEntry> entries;
    json::Value conditionsJson;
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
                    // plan40: pool-level conditions
                    if(auto* conds=poolV.find("conditions")) if(conds->isArr()) pool.conditionsJson = *conds;
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
                                                if(auto* w2=ee.find("weight")) if(w2->isNum()) ent.weight=w2->asInt(1);
                                                if (auto* f2=ee.find("functions")) {
                                                    if (f2->isArr()) applyFunctions(*f2,ent);
                                                    else if (auto* cl=e.find("conditions")) (void)cl;
                                                }
                                                pool.entries.push_back(std::move(ent));
                                            }
                                        } else if (ch.at("name").isStr()){
                                            LootEntry ent; ent.name=ch.at("name").asStr();
                                            if(auto* w2=ch.find("weight")) if(w2->isNum()) ent.weight=w2->asInt(1);
                                            if(auto* f2=ch.find("functions")) if(f2->isArr()) applyFunctions(*f2,ent);
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
                            // entry-level conditions as functions: skip if needed (survives_explosion handled via explosionDecay)
                            if(auto* ec=e.find("conditions")) if(ec->isArr()){
                                for(auto& cc: ec->arr){
                                    std::string cn=cc.at("condition").asStr();
                                    if(cn.find("survives_explosion")!=std::string::npos) ent.explosionDecay=true;
                                }
                            }
                            if(auto* funcs=e.find("functions")) if(funcs->isArr()) applyFunctions(*funcs,ent);
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
        std::vector<ItemStack> out;
        for (auto& pool: tbl.pools) {
            // plan40 C-05: pool conditions (random_chance/killed_by_player/survives_explosion)
            bool poolSkip=false;
            if(pool.conditionsJson.isArr()){
                for(auto& cond: pool.conditionsJson.arr){
                    std::string c=cond.at("condition").asStr();
                    if(c.find("random_chance")!=std::string::npos){
                        double ch=1.0;
                        if(auto* v=cond.find("chance")) if(v->isNum()) ch=v->number;
                        if(ch<1.0 && (rand()/(double)RAND_MAX) >= ch) { poolSkip=true; break; }
                    } else if(c.find("killed_by_player")!=std::string::npos){
                        if(ctx && ctx->killerName.empty() && !ctx->isPlayerKill) { poolSkip=true; break; }
                    } else if(c.find("survives_explosion")!=std::string::npos){
                        if(ctx && ctx->explosionRadius>0.f){
                            double vanish=1.0/ctx->explosionRadius;
                            if((rand()/(double)RAND_MAX) < vanish) { poolSkip=true; break; }
                        }
                    }
                }
            }
            if(poolSkip) continue;
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
                // plan35 §2: explosion_decay — 1/radius vanish (entry-level)
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
                        cnt += rand()%(looting+1) + extra;
                    }
                }
                // plan40 C-05: apply_bonus 3 formulas (ore_drops binomial p=0.33, uniform, binomial)
                if (chosen->applyBonusOre && ctx) {
                    int fortune = ctx->fortuneLevel;
                    if (fortune>0) {
                        int bonus=0;
                        if(chosen->applyBonusFormula.find("ore_drops")!=std::string::npos){
                            for(int i=0;i<fortune;++i) if((rand()/(double)RAND_MAX) < 0.33) ++bonus;
                            if(fortune>=3 && (rand()/(double)RAND_MAX) < 0.33) ++bonus;
                        } else if(chosen->hasApplyBonusBinomial){
                            for(int i=0;i<chosen->applyBonusN;++i) if((rand()/(double)RAND_MAX) < chosen->applyBonusP) ++bonus;
                            bonus += fortune;
                        } else if(chosen->hasApplyBonusUniform){
                            bonus = (int)(chosen->applyBonusExtra * fortune);
                        } else {
                            if(fortune==1) bonus = rand()%2;
                            else if(fortune==2) bonus = rand()%3;
                            else if(fortune>=3) bonus = rand()%4;
                        }
                        cnt += bonus;
                        if(cnt>64) cnt=64;
                    }
                } else if (ctx && (dropName.find("ore")!=std::string::npos || base.find("ore")!=std::string::npos)) {
                    int fortune = ctx->fortuneLevel;
                    if(fortune>0){
                        int bonus=0;
                        for(int i=0;i<fortune;++i) if((rand()/(double)RAND_MAX) < 0.33) ++bonus;
                        cnt+=bonus;
                    }
                }
                // plan40: limit_count clamp after bonus
                if(chosen->hasLimit){ if(cnt < chosen->limitMin) cnt=chosen->limitMin; if(cnt > chosen->limitMax) cnt=chosen->limitMax; }
                if(cnt<=0) {
                    if(chosen->countMin==0) continue;
                    cnt=1;
                }
                auto st = ItemStack::of(iidIt->second, static_cast<int16_t>(cnt));
                // plan40: set_damage
                if(chosen->hasSetDamage){
                    float dmg = (float)chosen->setDamageMin + (float)rand()/(float)RAND_MAX * (float)(chosen->setDamageMax - chosen->setDamageMin);
                    int maxDmg = ItemStack::maxDamageFor(dropName.find(':')!=std::string::npos? gen::itemIdByName().at(dropName) : 0);
                    // fallback: use iid
                    if(maxDmg==0) maxDmg = ItemStack::maxDamageFor(iidIt->second);
                    if(maxDmg>0) st.setDamage((int)(maxDmg * dmg));
                }
                if(!chosen->setLore.empty()) st.lore = chosen->setLore;
                if(!chosen->setName.empty()) st.displayNameLoot = chosen->setName;
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
                if (chosen->fillPlayerHead) {
                    st.count = 1;
                }
                (void)chosen->copyComponents;
                (void)chosen->setAttributes;
                (void)chosen->setNbt;
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
                if(auto* pf=fn.find("parameters")){
                    if(auto* bm=pf->find("bonusMultiplier")) ent.bonusMultiplier = bm->asFloat(1.0f);
                    if(auto* ex=pf->find("extra")) ent.applyBonusN = ex->asInt(1);
                    if(auto* p=pf->find("probability")) ent.applyBonusP = p->asFloat(0.33f);
                }
                if(ent.applyBonusFormula.find("ore_drops")!=std::string::npos) ent.applyBonusOre=true;
                else if(ent.applyBonusFormula.find("binomial")!=std::string::npos){ ent.hasApplyBonusBinomial=true; if(ent.applyBonusN<=0) ent.applyBonusN=1; }
                else if(ent.applyBonusFormula.find("uniform")!=std::string::npos){ ent.hasApplyBonusUniform=true; ent.applyBonusExtra = ent.bonusMultiplier; }
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
                ent.hasLimit=true;
                if(auto* l=fn.find("limit")){
                    if(l->isNum()) ent.limitMin=ent.limitMax=l->asInt(1);
                    else if(l->isObj()){ if(auto* mn=l->find("min")) ent.limitMin=mn->asInt(1); if(auto* mx=l->find("max")) ent.limitMax=mx->asInt(64); if(ent.limitMax<ent.limitMin) ent.limitMax=ent.limitMin; }
                    else if(l->isArr() && l->arr.size()>=2){ ent.limitMin=l->arr[0].asInt(1); ent.limitMax=l->arr[1].asInt(64); }
                } else {
                    if(auto* mn=fn.find("min")) ent.limitMin=mn->asInt(1);
                    if(auto* mx=fn.find("max")) ent.limitMax=mx->asInt(64);
                }
            } else if(ftype.find("set_damage")!=std::string::npos){
                ent.hasSetDamage=true;
                if(auto* d=fn.find("damage")){
                    if(d->isNum()) ent.setDamageMin=ent.setDamageMax=d->asFloat(0);
                    else if(d->isObj()){ if(auto* mn=d->find("min")) ent.setDamageMin=mn->asFloat(0); if(auto* mx=d->find("max")) ent.setDamageMax=mx->asFloat(0); }
                }
            } else if(ftype.find("set_attributes")!=std::string::npos){
                ent.hasSetAttributes=true;
                if(auto* attrs=fn.find("attributes")) if(attrs->isArr()) for(auto& a: attrs->arr) if(a.isObj()){
                    LootAttribute at; if(auto* n=a.find("name")) at.name=n->asStr(); if(auto* at2=a.find("attribute")) at.attribute=at2->asStr(); if(auto* op=a.find("operation")) at.operation=op->asStr(); if(auto* am=a.find("amount")){ if(am->isNum()) at.amount=am->number; else if(am->isObj()){ if(auto* mn=am->find("min")) at.amount=mn->asFloat(0); } } ent.setAttributes.push_back(std::move(at));
                }
            } else if(ftype.find("set_nbt")!=std::string::npos || ftype.find("set_components")!=std::string::npos){
                ent.hasSetNbt=true;
                if(auto* t=fn.find("tag")) ent.setNbt=t->asStr(); else if(auto* c=fn.find("components")) ent.setNbt=c->dump(); else if(auto* comps=fn.find("components")) ent.setNbt=comps->dump();
            } else if(ftype.find("set_lore")!=std::string::npos){
                ent.hasSetLore=true;
                if(auto* lore=fn.find("lore")) if(lore->isArr()) for(auto& l: lore->arr) ent.setLore.push_back(l.isStr()?l.asStr():l.dump());
            } else if(ftype.find("set_name")!=std::string::npos || ftype.find("set_custom_name")!=std::string::npos){
                if(auto* nm=fn.find("name")) ent.setName=nm->asStr(); else if(auto* v=fn.find("value")) ent.setName=v->asStr();
                ent.hasSetLore=true;
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
