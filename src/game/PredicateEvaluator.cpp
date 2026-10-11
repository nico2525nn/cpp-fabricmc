#include "PredicateEvaluator.hpp"

#include "DamageSource.hpp"
#include "Entities.hpp"
#include "GameRules.hpp"
#include "Scoreboard.hpp"
#include "TagManager.hpp"
#include "World.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace cppfm {
namespace {
inline bool jsonSubset(const json::Value& want, const json::Value& have){
    if(want.isObj()){
        if(!have.isObj()) return false;
        for(auto& kv : want.obj){
            auto* hv = have.find(kv.first);
            if(!hv) return false;
            if(!jsonSubset(kv.second, *hv)) return false;
        }
        return true;
    }
    if(want.isArr()){
        if(!have.isArr()) return false;
        for(auto& wv: want.arr){
            bool found=false;
            for(auto& hv: have.arr) if(jsonSubset(wv, hv)) { found=true; break; }
            if(!found) return false;
        }
        return true;
    }
    if(want.isStr()) return have.isStr() && want.asStr()==have.asStr();
    if(want.isNum()) return have.isNum() && want.number==have.number;
    if(want.isBool()) return have.isBool() && want.boolean==have.boolean;
    if(want.isNull()) return have.isNull();
    return true;
}
inline std::string entityNbtDump(const MobEntity* e){
    if(!e) return "{}";
    json::Value o = json::Value::object();
    o.set("Health", json::Value::ofNumber(e->health));
    json::Value tags = json::Value::array();
    o.set("Tags", tags);
    return o.dump();
}


double predicateRandom01(const PredicateContext& context) {
    std::uint64_t state = context.randomSeed + 0x9E3779B97F4A7C15ULL;
    state = (state ^ (state >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    state = (state ^ (state >> 27U)) * 0x94D049BB133111EBULL;
    state ^= state >> 31U;
    return static_cast<double>(state >> 11U) * 0x1.0p-53;
}
} // namespace

bool PredicateEvaluator::evaluatePredicateValue(const json::Value& v, const PredicateContext& ctx, int depth) const {
    if (depth > 5) return false; // cycle guard for reference
    if (v.isObj()) {
        if (auto* cond = v.find("condition")) {
            std::string c = cond->asStr();
            if (c == "minecraft:random_chance" || c == "random_chance") {
                if (auto* chance = v.find("chance")) {
                    double ch = chance->isNum() ? chance->number : 1.0;
                    if (!chance->isNum() || !std::isfinite(ch)) return false;
                    if (ch >= 1.0) return true;
                    if (ch <= 0.0) return false;
                    return predicateRandom01(ctx) < ch;
                }
                return false;
            } else if (c == "minecraft:random_chance_with_looting" || c == "random_chance_with_looting") {
                if (auto* chance = v.find("chance")) {
                    double ch = chance->isNum() ? chance->number : 1.0;
                    if (!chance->isNum() || !std::isfinite(ch)) return false;
                    double multiplier = 0.0;
                    if (auto* looting = v.find("looting_multiplier")) {
                        if (!looting->isNum() || !std::isfinite(looting->number)) return false;
                        multiplier = looting->number;
                    }
                    ch += multiplier * static_cast<double>(ctx.lootingLevel);
                    if (ch >= 1.0) return true;
                    if (ch <= 0.0) return false;
                    return predicateRandom01(ctx) < ch;
                }
                return false;
            } else if (c == "minecraft:inverted" || c == "inverted") {
                if (auto* term = v.find("term")) return !evaluatePredicateValue(*term, ctx, depth+1);
                return false;
            } else if (c == "minecraft:any_of" || c == "minecraft:alternative" || c == "alternative" || c == "any_of") {
                if (auto* terms = v.find("terms")) {
                    if (terms->isArr()) { for (auto& t: terms->arr) if (evaluatePredicateValue(t, ctx, depth+1)) return true; return false; }
                }
                if (auto* conds = v.find("conditions")) {
                    if (conds->isArr()) { for (auto& t: conds->arr) if (evaluatePredicateValue(t, ctx, depth+1)) return true; return false; }
                }
                return false;
            } else if (c == "minecraft:all_of" || c == "all_of") {
                if (auto* terms = v.find("terms")) {
                    if (terms->isArr()) { for (auto& t: terms->arr) if (!evaluatePredicateValue(t, ctx, depth+1)) return false; return true; }
                }
                if (auto* conds = v.find("conditions")) {
                    if (conds->isArr()) { for (auto& t: conds->arr) if (!evaluatePredicateValue(t, ctx, depth+1)) return false; return true; }
                }
                return true;
            } else if (c == "minecraft:check_gamerule" || c == "check_gamerule") {
                std::string rule;
                if (auto* gr = v.find("game_rule")) rule = gr->asStr();
                else if (auto* r = v.find("rule")) rule = r->asStr();
                bool expected = true;
                if (auto* val = v.find("value")) {
                    if (val->isBool()) expected = val->boolean;
                    else if (val->isStr()) expected = (val->asStr()=="true" || val->asStr()=="1");
                    else if (val->isNum()) expected = (val->number != 0);
                    else expected = true;
                }
                if (ctx.gamerules) {
                    if (rule.empty() || !ctx.gamerules->contains(rule)) return false;
                    bool actual = ctx.gamerules->getBool(rule);
                    return actual == expected;
                }
                return false;
            } else if (c == "minecraft:location_check" || c == "location_check") {
                if (!ctx.world) return false;
                if (auto* pred = v.find("predicate")) {
                    // dimension gate: predicate.dimension == "minecraft:overworld" etc
                    if (auto* dim = pred->find("dimension")) {
                        std::string wantDim = dim->asStr();
                        std::string haveDim = ctx.dimension.empty() ? ctx.world->dimensionKey() : ctx.dimension;
                        if(wantDim.find(':')==std::string::npos) wantDim="minecraft:"+wantDim;
                        if(haveDim.find(':')==std::string::npos) haveDim="minecraft:"+haveDim;
                        // lenient substring: the_nether vs nether
                        bool dimMatch = (wantDim==haveDim) || (wantDim.find(haveDim)!=std::string::npos) || (haveDim.find(wantDim)!=std::string::npos) || (wantDim.find("nether")!=std::string::npos && haveDim.find("nether")!=std::string::npos) || (wantDim.find("end")!=std::string::npos && haveDim.find("end")!=std::string::npos);
                        if(!dimMatch) return false;
                    }
                    // biome check with tag support
                    if (auto* biome = pred->find("biome")) {
                        std::string want = biome->asStr();
                        std::string have;
                        try { have = ctx.world->sampledBiome(ctx.x, ctx.y, ctx.z); } catch(...) { have = ctx.world->biomeKey(); }
                        if (!want.empty() && want[0]=='#') {
                            std::string tag = want.substr(1);
                            auto* mgr = &tagManager_;
                            auto it = mgr->biomeTags.find(tag);
                            if(it != mgr->biomeTags.end()){
                                if(it->second.find(have)==it->second.end() && it->second.find(have.substr(have.find(':')+1))==it->second.end()){
                                    // also try without minecraft: prefix in tag values
                                    bool found=false;
                                    for(auto& val: it->second) if(val==have || val==have.substr(have.find(':')+1)) { found=true; break; }
                                    if(!found) return false;
                                }
                            } else {
                                // alternative: tag without minecraft: prefix
                                std::string tag2 = tag.find(':')!=std::string::npos ? tag.substr(tag.find(':')+1) : tag;
                                auto it2 = mgr->biomeTags.find("minecraft:"+tag2);
                                if(it2 != mgr->biomeTags.end()){
                                    if(it2->second.count(have)==0) return false;
                                } else {
                                    // unsupported tag -> pass
                                }
                            }
                        } else if (!want.empty() && have != want) {
                            std::string wantNorm = want.find(':')==std::string::npos ? "minecraft:"+want : want;
                            std::string haveNorm = have.find(':')==std::string::npos ? "minecraft:"+have : have;
                            if(wantNorm != haveNorm && want != have) return false;
                        }
                    }
                    // block check: predicate.block.blocks or predicate.block
                    if (auto* block = pred->find("block")) {
                        std::string want;
                        if (block->isStr()) want = block->asStr();
                        else if (block->isObj()) {
                            if (auto* b = block->find("blocks")) {
                                if (b->isStr()) want = b->asStr();
                                else if (b->isArr() && !b->arr.empty() && b->arr[0].isStr()) want = b->arr[0].asStr();
                            } else if (auto* id = block->find("id")) want = id->asStr();
                        }
                        if (!want.empty()) {
                            std::uint16_t st = ctx.world->getBlock(ctx.x, ctx.y, ctx.z);
                            std::string have;
                            if (auto* bd = gen::blockByState(st)) have = bd->name;
                            if (!want.empty() && have != want) {
                                // allow without minecraft: prefix
                                std::string wantNorm = want.find(':')==std::string::npos ? "minecraft:"+want : want;
                                std::string haveNorm = have.find(':')==std::string::npos ? "minecraft:"+have : have;
                                if (wantNorm != haveNorm) return false;
                            }
                        }
                    }
                    // position range {x:{min,max}, y:{min,max}, z:{min,max}}
                    if (auto* off = pred->find("position")) {
                        auto checkAxis = [&](const char* axis, int32_t posVal)->bool{
                            if (auto* r = off->find(axis)) {
                                if (r->isNum()) { if (posVal != r->asInt(posVal)) return false; }
                                else if (r->isObj()) {
                                    int mn = r->find("min") ? r->find("min")->asInt(posVal) : posVal;
                                    int mx = r->find("max") ? r->find("max")->asInt(posVal) : posVal;
                                    if (auto* mnV = r->find("min")) mn = mnV->asInt(mn);
                                    if (auto* mxV = r->find("max")) mx = mxV->asInt(mx);
                                    if (posVal < mn || posVal > mx) return false;
                                }
                            }
                            return true;
                        };
                        if (!checkAxis("x", ctx.x) || !checkAxis("y", ctx.y) || !checkAxis("z", ctx.z)) return false;
                    }
                    // light check: predicate.light.light {min,max} — use block light
                    if (auto* light = pred->find("light")) {
                        auto* lvl = light->find("light");
                        if (!lvl) lvl = light;
                        if (lvl) {
                            int haveLight = 0;
                            try { haveLight = ctx.world->getBlockLight(ctx.x, ctx.y, ctx.z); } catch(...){}
                            if (lvl->isNum()) { if (haveLight != lvl->asInt(haveLight)) return false; }
                            else if (lvl->isObj()) {
                                if (auto* mn = lvl->find("min")) if (haveLight < mn->asInt(haveLight)) return false;
                                if (auto* mx = lvl->find("max")) if (haveLight > mx->asInt(haveLight)) return false;
                            }
                        }
                    }
                }
                return v.find("predicate") != nullptr;
            } else if (c == "minecraft:entity_properties" || c == "entity_properties") {
                if (!ctx.entity && !ctx.player) return false;
                if (auto* pred = v.find("predicate")) {
                    if (auto* tp = pred->find("type")) {
                        std::string want = tp->asStr();
                        std::string have;
                        if (ctx.entity) have = MobEntity::kindName(ctx.entity->kind);
                        else if (ctx.player) have = "minecraft:player";
                        else have = "";
                        auto norm = [](std::string s){ return s.find(':')==std::string::npos ? "minecraft:"+s : s; };
                        if (!want.empty() && !have.empty() && norm(want) != norm(have)) return false;
                        if (!want.empty() && have.empty()) return false;
                    }
                    // equipment: {mainhand:{items:..., enchantments:...}}
                    if (auto* equip = pred->find("equipment")) {
                        if (equip->isObj()) {
                            if (auto* mh = equip->find("mainhand")) {
                                if (mh->isObj()) {
                                    if (auto* items = mh->find("items")) {
                                        bool any=false;
                                        std::string haveName = ctx.heldItemName.empty() ? "minecraft:air" : ctx.heldItemName;
                                        auto norm2 = [](std::string s){ return s.find(':')==std::string::npos ? "minecraft:"+s : s; };
                                        if(items->isStr()){
                                            if(norm2(items->asStr())==norm2(haveName)) any=true;
                                        } else if(items->isArr()){
                                            for(auto& it: items->arr) if(it.isStr() && norm2(it.asStr())==norm2(haveName)) { any=true; break; }
                                        }
                                        if(!any) return false;
                                    }
                                    if (auto* ench = mh->find("enchantments")) {
                                        if (ench->isArr()) {
                                            for(auto& e: ench->arr) if(e.isObj()){
                                                if(auto* en = e.find("enchantment")){
                                                    std::string enStr=en->asStr();
                                                    bool wantFortune=enStr.find("fortune")!=std::string::npos;
                                                    bool wantSilk=enStr.find("silk_touch")!=std::string::npos;
                                                    if(wantFortune && ctx.fortuneLevel==0) return false;
                                                    if(wantSilk && !ctx.silkTouch) return false;
                                                    if(auto* lv=e.find("levels")){
                                                        int haveLv = wantSilk ? (ctx.silkTouch?1:0) : ctx.fortuneLevel;
                                                        if(lv->isNum()){ if(haveLv!=lv->asInt(haveLv)) return false; }
                                                        else if(lv->isObj()){
                                                            if(auto* mn=lv->find("min")) if(haveLv < mn->asInt(haveLv)) return false;
                                                            if(auto* mx=lv->find("max")) if(haveLv > mx->asInt(haveLv)) return false;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    // nbt inside entity_properties: predicate.nbt
                    if (auto* nbt = pred->find("nbt")) if(nbt->isStr()){
                        std::string wantNbt=nbt->asStr();
                        std::string haveNbt = ctx.nbt.empty() ? (ctx.entity ? entityNbtDump(ctx.entity) : "") : ctx.nbt;
                        if(!wantNbt.empty()){
                            bool contains=false;
                            try{
                                auto wantV=json::Value::parse(wantNbt);
                                auto haveV=json::Value::parse(haveNbt);
                                contains=jsonSubset(wantV, haveV);
                            } catch(...){
                                contains = haveNbt.find(wantNbt)!=std::string::npos || haveNbt.find(wantNbt.substr(1, wantNbt.size()>2?wantNbt.size()-2:0))!=std::string::npos;
                            }
                            if(!contains) return false;
                        }
                    }
                    // type_specific inside entity_properties
                    if (auto* ts = pred->find("type_specific")) {
                        if(ts->isObj()){
                            if(auto* tp = ts->find("type")){
                                std::string wantTp=tp->asStr();
                                std::string haveTp = ctx.entity ? std::string(MobEntity::kindName(ctx.entity->kind)) : (ctx.player ? "minecraft:player" : "");
                                // reconstruct haveTp properly
                                if(ctx.entity) haveTp = MobEntity::kindName(ctx.entity->kind);
                                else if(ctx.player) haveTp = "minecraft:player";
                                else haveTp = "";
                                if(!wantTp.empty() && wantTp.find("player")!=std::string::npos && !ctx.player) return false;
                                if(!wantTp.empty() && wantTp.find("fishing_hook")!=std::string::npos) return false;
                                if(!wantTp.empty() && !haveTp.empty() && wantTp!=haveTp){
                                    auto norm3=[&](std::string s){ return s.find(':')==std::string::npos?"minecraft:"+s:s; };
                                    if(norm3(wantTp)!=norm3(haveTp)) return false;
                                }
                            }
                            if(auto* pl = ts->find("player")){
                                if(pl->isObj()){
                                    if(auto* advs = pl->find("advancements")){
                                        if(advs->isObj()) for(auto& kv: advs->obj){
                                            bool haveAdv = ctx.advancements.count(kv.first);
                                            bool expected = kv.second.isBool()? kv.second.boolean : (kv.second.isStr()? kv.second.asStr()=="true" : kv.second.asInt(1)!=0);
                                            if(haveAdv != expected) return false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                return true;
            } else if (c == "minecraft:weather_check" || c == "weather_check") {
                if (auto* r = v.find("raining")) if (r->isBool()) { if (r->boolean != ctx.raining) return false; }
                if (auto* t = v.find("thundering")) if (t->isBool()) { if (t->boolean != ctx.thundering) return false; }
                return true;
            } else if (c == "minecraft:time_check" || c == "time_check") {
                int period = 24000;
                if (auto* p = v.find("period")) period = p->asInt(24000);
                if (period <= 0) period = 24000;
                long t = (long)(ctx.dayTime % period);
                if (auto* val = v.find("value")) {
                    if (val->isNum()) { if (t != val->asInt((int)t)) return false; }
                    else if (val->isObj()) {
                        int mn = val->find("min") ? val->find("min")->asInt(0) : 0;
                        int mx = val->find("max") ? val->find("max")->asInt(period) : period;
                        if (t < mn || t > mx) return false;
                    }
                }
                return true;
            } else if (c == "minecraft:block_state_property" || c == "block_state_property") {
                if (!ctx.world) return false;
                const std::uint16_t state = ctx.world->getBlock(ctx.x, ctx.y, ctx.z);
                if (auto* blk = v.find("block")) {
                    std::string want = blk->asStr();
                    if (!want.empty() && want[0] != '#') {
                        std::string have;
                        try {
                            if (auto* bd = gen::blockByState(state)) have = bd->name;
                            else return false;
                        } catch (...) { return false; }
                        std::string wantN = want.find(':')==std::string::npos ? "minecraft:"+want : want;
                        std::string haveN = have.find(':')==std::string::npos ? "minecraft:"+have : have;
                        if (wantN != haveN) return false;
                    } else if (!want.empty() && want[0] == '#') {
                        const std::string tag = want.substr(1);
                        auto tagIt = tagManager_.blockTags.find(tag);
                        if (tagIt == tagManager_.blockTags.end()) return false;
                        if (!tagIt->second.count(state)) return false;
                    }
                } else {
                    return false;
                }
                if (auto* properties = v.find("properties")) {
                    if (!properties->isObj()) return false;
                    const auto actual = gen::propsOf(state);
                    for (const auto& [key, expected] : properties->obj) {
                        if (!expected.isStr()) return false;
                        const auto found = std::find_if(
                            actual.begin(), actual.end(),
                            [&](const auto& prop) { return prop.first == key; });
                        if (found == actual.end() || found->second != expected.asStr())
                            return false;
                    }
                }
                return true;
            } else if (c == "minecraft:damage_source_properties" || c == "damage_source_properties") {
                if (auto* pred = v.find("predicate")) {
                    if (auto* tags = pred->find("tags")) {
                        auto checkTag = [&](const json::Value& tag)->bool{
                            if(!tag.isObj()) return false;
                            if(!ctx.damageSource) return false;
                            std::string tagId = tag.find("id") ? tag.at("id").asStr() : "";
                            bool expected = true;
                            if(auto* ex = tag.find("expected")){
                                if(ex->isBool()) expected=ex->boolean;
                                else if(ex->isStr()) expected=(ex->asStr()=="true");
                                else if(ex->isNum()) expected=(ex->number!=0);
                            }
                            bool actual=false;
                            if(ctx.damageSource){
                                if(tagId.find("is_fire")!=std::string::npos) actual=ctx.damageSource->isFire();
                                else if(tagId.find("is_fall")!=std::string::npos) actual=ctx.damageSource->isFall();
                                else if(tagId.find("is_explosion")!=std::string::npos) actual=ctx.damageSource->isExplosion();
                                else if(tagId.find("is_projectile")!=std::string::npos) actual=ctx.damageSource->isProjectile();
                                else if(tagId.find("is_magic")!=std::string::npos) actual=ctx.damageSource->isMagic();
                                else if(tagId.find("is_lightning")!=std::string::npos) actual=ctx.damageSource->isLightning();
                                else return false;
                            }
                            return actual==expected;
                        };
                        if(tags->isArr()){
                            for(auto& tag: tags->arr) if(!checkTag(tag)) return false;
                        } else if(tags->isObj()){
                            if(!checkTag(*tags)) return false;
                        }
                    }
                }
                return v.find("predicate") != nullptr;
            } else if (c == "minecraft:killed_by_player" || c == "killed_by_player") {
                return ctx.hasKiller && ctx.killedByPlayer;
            } else if (c == "minecraft:survives_explosion" || c == "survives_explosion") {
                return true;
            } else if (c == "minecraft:table_bonus" || c == "table_bonus") {
                if(ctx.fortuneLevel>=0){
                    double ch=0.5;
                    if(auto* chances=v.find("chances")){
                        if(chances->isArr()){
                            int idx = std::min(ctx.fortuneLevel, (int)chances->arr.size()-1);
                            if(idx>=0 && chances->arr[idx].isNum()) ch=chances->arr[idx].number;
                            else if(idx>=0 && chances->arr[idx].isStr()) ch=std::stod(chances->arr[idx].asStr());
                        } else if(chances->isNum()) ch=chances->number;
                    } else if(auto* chance=v.find("chance")) ch=chance->isNum()?chance->number:0.5;
                    return ch >= 0.5;
                }
                return true;
            } else if (c == "minecraft:entity_scores" || c == "entity_scores") {
                if (ctx.playerName.empty() && !ctx.entity) return false;
                // scores: {objective: {min,max} or number}
                if (auto* scores = v.find("scores")) {
                    if (scores->isObj()) {
                        for (auto& kv : scores->obj) {
                            const std::string& obj = kv.first;
                            const json::Value& range = kv.second;
                            int have = 0;
                            if (ctx.scoreboard && !ctx.playerName.empty()) have = ctx.scoreboard->getScore(obj, ctx.playerName);
                            else if (ctx.scoreboard && ctx.entity) have = ctx.scoreboard->getScore(obj, std::to_string(ctx.entity->entityId));
                            if (range.isNum()) {
                                if (have != range.asInt(have)) return false;
                            } else if (range.isObj()) {
                                if (auto* mn = range.find("min")) if (have < mn->asInt(have)) return false;
                                if (auto* mx = range.find("max")) if (have > mx->asInt(have)) return false;
                            }
                        }
                    }
                }
                return v.find("scores") != nullptr;
            } else if (c == "minecraft:reference" || c == "reference") {
                if (auto* name = v.find("name")) {
                    std::string ref = name->asStr();
                    if (!ref.empty()) return testPredicate(ref, ctx, depth+1);
                }
                return false;
            } else if (c == "minecraft:value_check" || c == "value_check") {
                int have = ctx.hasValueCheck ? ctx.valueCheckValue : ctx.fortuneLevel;
                if (auto* val = v.find("value")) {
                    if (val->isNum()) have = val->asInt(have);
                    else if (val->isObj()) {
                        // value as range {min,max} — check have in range directly
                        if (auto* mn = val->find("min")) if (have < mn->asInt(have)) return false;
                        if (auto* mx = val->find("max")) if (have > mx->asInt(have)) return false;
                        if (val->find("min") || val->find("max")) {
                            if (auto* rng = v.find("range")) {
                                if (rng->isObj()) {
                                    if (auto* mn2 = rng->find("min")) if (have < mn2->asInt(have)) return false;
                                    if (auto* mx2 = rng->find("max")) if (have > mx2->asInt(have)) return false;
                                }
                            }
                            return true;
                        }
                    }
                }
                if (auto* rng = v.find("range")) {
                    if (rng->isNum()) { if (have != rng->asInt(have)) return false; }
                    else if (rng->isObj()) {
                        if (auto* mn = rng->find("min")) if (have < mn->asInt(have)) return false;
                        if (auto* mx = rng->find("max")) if (have > mx->asInt(have)) return false;
                    }
                }
                return true;
            } else if (c == "minecraft:match_tool" || c == "match_tool" || c == "minecraft:enchantment_active" || c == "enchantment_active") {
                // match_tool predicate: check held tool items/enchantments if player context present
                if (ctx.playerName.empty() && !ctx.player) return false;
                // items check
                if (auto* pred = v.find("predicate")) {
                    if (auto* items = pred->find("items")) {
                        bool any = false;
                        std::string haveItemName = ctx.heldItemName.empty() ? "minecraft:air" : ctx.heldItemName;
                        if (items->isStr()) {
                            std::string want = items->asStr();
                            std::string wn = want.find(':')==std::string::npos ? "minecraft:"+want : want;
                            std::string hn = haveItemName.find(':')==std::string::npos ? "minecraft:"+haveItemName : haveItemName;
                            if (wn==hn) any = true;
                        } else if (items->isArr()) {
                            for (auto &it : items->arr) if (it.isStr()) {
                                std::string want = it.asStr();
                                std::string wn = want.find(':')==std::string::npos ? "minecraft:"+want : want;
                                std::string hn = haveItemName.find(':')==std::string::npos ? "minecraft:"+haveItemName : haveItemName;
                                if (wn==hn) { any=true; break; }
                            }
                        }
                        if (!any) return false;
                    }
                    if (auto* ench = pred->find("enchantments")) {
                        if (ench->isArr()) {
                            for (auto& e : ench->arr) if (e.isObj()) {
                                if (auto* en = e.find("enchantment")) {
                                    std::string enStr = en->asStr();
                                    bool wantSilk = enStr.find("silk_touch")!=std::string::npos;
                                    bool wantFortune = enStr.find("fortune")!=std::string::npos;
                                    if (wantSilk && !ctx.silkTouch) return false;
                                    if (wantFortune && ctx.fortuneLevel==0) return false;
                                    if (auto* lv = e.find("levels")) {
                                        int haveLv = wantSilk ? (ctx.silkTouch?1:0) : ctx.fortuneLevel;
                                        if (lv->isNum()) { if (haveLv != lv->asInt(haveLv)) return false; }
                                        else if (lv->isObj()) {
                                            if (auto* mn = lv->find("min")) if (haveLv < mn->asInt(haveLv)) return false;
                                            if (auto* mx = lv->find("max")) if (haveLv > mx->asInt(haveLv)) return false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (c.find("enchantment_active")!=std::string::npos) {
                    if (auto* ench = v.find("enchantment")) {
                        std::string enStr = ench->asStr();
                        bool wantSilk = enStr.find("silk_touch")!=std::string::npos;
                        if (wantSilk && !ctx.silkTouch) return false;
                    }
                }
                return true;
            } else if (c == "minecraft:enchantment_active_check" || c == "enchantment_active_check") {
                std::string enchantName;
                if(auto* enchantmentValue=v.find("enchantment"))
                    enchantName=enchantmentValue->asStr();
                else if(auto* enchantmentsValue=v.find("enchantments"))
                    enchantName=enchantmentsValue->asStr();
                bool wantFortune = enchantName.find("fortune")!=std::string::npos;
                bool wantSilk = enchantName.find("silk_touch")!=std::string::npos;
                bool wantMending = enchantName.find("mending")!=std::string::npos;
                int haveLv=0;
                if(wantFortune) haveLv=ctx.fortuneLevel;
                else if(wantSilk) haveLv=ctx.silkTouch?1:0;
                else if(wantMending) haveLv=ctx.heldItemName.find("mending")!=std::string::npos?1:0;
                else haveLv=ctx.fortuneLevel;
                if(auto* lv=v.find("levels")){
                    if(lv->isNum()){ if(haveLv != lv->asInt(haveLv)) return false; }
                    else if(lv->isObj()){
                        if(auto* mn=lv->find("min")) if(haveLv < mn->asInt(haveLv)) return false;
                        if(auto* mx=lv->find("max")) if(haveLv > mx->asInt(haveLv)) return false;
                    }
                } else {
                    if(haveLv<=0) return false;
                }
                return true;
            } else if (c == "minecraft:nbt" || c == "nbt") {
                std::string wantNbt;
                if(auto* n=v.find("nbt")) wantNbt=n->asStr();
                else if(auto* tag=v.find("tag")) wantNbt=tag->asStr();
                if(!wantNbt.empty()){
                    std::string haveNbt = ctx.nbt.empty() ? (ctx.entity? entityNbtDump(ctx.entity) : "") : ctx.nbt;
                    bool contains=true;
                    try{
                        auto wantV=json::Value::parse(wantNbt);
                        auto haveV=json::Value::parse(haveNbt);
                        contains = jsonSubset(wantV, haveV);
                    } catch(...){
                        contains = haveNbt.find(wantNbt)!=std::string::npos;
                        if(!contains && wantNbt.size()>2) contains = haveNbt.find(wantNbt.substr(1,wantNbt.size()-2))!=std::string::npos;
                    }
                    if(!contains) return false;
                }
                return true;
            } else if (c == "minecraft:type_specific" || c == "type_specific") {
                if(auto* pred=v.find("predicate")){
                    if(auto* ts=pred->find("type_specific")){
                        if(ts->isObj()){
                            if(auto* tp=ts->find("type")){
                                std::string wantTp=tp->asStr();
                                std::string haveTp = ctx.entity ? MobEntity::kindName(ctx.entity->kind) : (ctx.player ? "minecraft:player" : "");
                                if(!wantTp.empty() && wantTp.find("player")!=std::string::npos && !ctx.player) return false;
                                if(!wantTp.empty() && wantTp.find("fishing_hook")!=std::string::npos) return false;
                                if(!wantTp.empty() && !haveTp.empty() && wantTp!=haveTp){
                                    auto norm=[&](std::string s){ return s.find(':')==std::string::npos?"minecraft:"+s:s; };
                                    if(norm(wantTp)!=norm(haveTp)) return false;
                                }
                            }
                            if(auto* pl=ts->find("player")){
                                if(pl->isObj()){
                                    if(auto* advs=pl->find("advancements")){
                                        if(advs->isObj()) for(auto& kv: advs->obj){
                                            bool haveAdv = ctx.advancements.count(kv.first);
                                            bool expected = kv.second.isBool()? kv.second.boolean : (kv.second.isStr()? kv.second.asStr()=="true" : kv.second.asInt(1)!=0);
                                            if(haveAdv != expected) return false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else if(auto* ts=v.find("type_specific")){
                    if(ts->isObj() && ts->find("type") && !ctx.player) return false;
                }
                return true;
            } else {
                return false;
            }
        }
        if (auto* pred = v.find("predicate")) {
            return evaluatePredicateValue(*pred, ctx, depth+1);
        }
        if (auto* conds = v.find("conditions")) {
            if (conds->isArr()) {
                for (auto& cc: conds->arr) if (!evaluatePredicateValue(cc, ctx, depth+1)) return false;
                return true;
            }
        }
        if (auto* terms = v.find("terms")) {
            if (terms->isArr()) {
                for (auto& cc: terms->arr) if (!evaluatePredicateValue(cc, ctx, depth+1)) return false;
                return true;
            }
        }
        return !v.obj.empty();
    } else if (v.isArr()) {
        for (auto& e: v.arr) if (!evaluatePredicateValue(e, ctx, depth+1)) return false;
        return true;
    } else {
        return false;
    }
}
bool PredicateEvaluator::evaluatePredicateValue(const json::Value& v) const {
    return evaluatePredicateValue(v, PredicateContext{}, 0);
}

bool PredicateEvaluator::testPredicate(const std::string& id, const PredicateContext& ctx, int depth) const {
    if (depth > 5) return false;
    auto it = predicates_.find(id);
    if (it == predicates_.end()) {
        if (id.find(':')==std::string::npos) {
            auto it2 = predicates_.find("minecraft:"+id);
            if (it2 != predicates_.end()) it = it2;
            else return false;
        } else {
            return false;
        }
    }
    try {
        auto v = json::Value::parse(it->second);
        return evaluatePredicateValue(v, ctx, depth);
    } catch (...) {
        return false;
    }
}
bool PredicateEvaluator::testPredicate(const std::string& id) const {
    return testPredicate(id, PredicateContext{}, 0);
}


} // namespace cppfm
