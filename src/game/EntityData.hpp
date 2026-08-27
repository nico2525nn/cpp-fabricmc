// EntityData: data-driven entity definitions loaded from assets/entities/*.json
// Expanded per plan6: {type, attributes, spawning, brain:{behaviors:[{type,priority}], sensors:[...]}, equipment, loot}
// BehaviorTree built via factory (switch on type string) and stored in def for Brain assignment.
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <memory>
#include "../core/Json.hpp"
// Forward declare BehaviorTree to avoid heavy include in header; loader builds it via factory
namespace cppfm { class BehaviorTree; }
namespace cppfm {
struct EntityDataDef{
    std::string type;
    float max_health=-1;
    float movement_speed=-1;
    float attack_damage=-1;
    std::vector<std::string> biomes;
    int lightMin=-1;
    int lightMax=-1;
    std::vector<std::string> structures;
    struct Behavior{ std::string type; int priority=0; };
    std::vector<Behavior> behaviors;
    std::vector<std::string> sensors;
    std::unordered_map<int,std::string> equipment;
    std::string loot;
    // prototype behavior tree built from behaviors (plan6 item 29)
    std::shared_ptr<BehaviorTree> behaviorTree;
};
class EntityDataLoader{
public:
    // Build BehaviorTree prototype from behaviors via factory (switch on type string) -- plan6 item 29
    static std::shared_ptr<BehaviorTree> buildTreeFor(const EntityDataDef& def);
    void loadDirectory(const std::string& dir){
        namespace fs=std::filesystem; std::error_code ec;
        if(!fs::exists(dir,ec)){ fprintf(stderr,"[cppfm] entity data dir missing: %s\n",dir.c_str()); return; }
        for(auto &e: fs::directory_iterator(dir,ec)){
            if(!e.is_regular_file()) continue;
            auto p=e.path(); if(p.extension()!=".json") continue;
            try{
                std::ifstream f(p); if(!f) continue;
                std::string txt((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
                auto v=json::Value::parse(txt);
                EntityDataDef d; d.type=v.at("type").asStr();
                if(d.type.empty()) d.type="minecraft:"+p.stem().string();
                const auto &a=v.at("attributes");
                if(a.type==json::Value::Type::Obj){
                    if(!a.at("max_health").isNull()) d.max_health=(float)a.at("max_health").asFloat(20);
                    if(!a.at("movement_speed").isNull()) d.movement_speed=(float)a.at("movement_speed").asFloat(0.1);
                    if(!a.at("attack_damage").isNull()) d.attack_damage=(float)a.at("attack_damage").asFloat(1);
                }
                const auto &sp=v.at("spawning");
                if(sp.type==json::Value::Type::Obj){
                    const auto &bm=sp.at("biomes"); if(bm.type==json::Value::Type::Arr) for(auto &ee: bm.arr) if(ee.isStr()) d.biomes.push_back(ee.asStr());
                    const auto &ll=sp.at("light_level");
                    if(ll.type==json::Value::Type::Obj){ d.lightMin=ll.at("min").asInt(-1); d.lightMax=ll.at("max").asInt(-1); }
                    else if(ll.isNum()) d.lightMax=ll.asInt(-1);
                    const auto &st=sp.at("structures"); if(st.type==json::Value::Type::Arr) for(auto &ee: st.arr) if(ee.isStr()) d.structures.push_back(ee.asStr()); else if(st.isStr()) d.structures.push_back(st.asStr());
                }
                const auto &brain=v.at("brain");
                if(brain.type==json::Value::Type::Obj){
                    const auto &bh=brain.at("behaviors");
                    if(bh.type==json::Value::Type::Arr) for(auto &b: bh.arr){ EntityDataDef::Behavior beh; beh.type=b.at("type").asStr(); beh.priority=b.at("priority").asInt(0); if(!beh.type.empty()) d.behaviors.push_back(std::move(beh)); }
                    const auto &ss=brain.at("sensors");
                    if(ss.type==json::Value::Type::Arr) for(auto &s: ss.arr) if(s.isStr()) d.sensors.push_back(s.asStr());
                }
                const auto &eq=v.at("equipment");
                if(eq.type==json::Value::Type::Obj){ for(auto &kv: eq.obj){ int slot=-1; const std::string &k=kv.first; if(k=="mainhand") slot=0; else if(k=="offhand") slot=1; else if(k=="feet"||k=="boots") slot=2; else if(k=="legs"||k=="leggings") slot=3; else if(k=="chest") slot=4; else if(k=="head"||k=="helmet") slot=5; else{ try{slot=std::stoi(k);}catch(...){continue;}} if(slot>=0&&slot<6){ if(kv.second.isStr()) d.equipment[slot]=kv.second.asStr(); else if(kv.second.type==json::Value::Type::Obj){ auto it=kv.second.find("item"); if(it&&it->isStr()) d.equipment[slot]=it->asStr(); } } } }
                const auto &loot=v.at("loot"); if(loot.isStr()) d.loot=loot.asStr();
                defs_[d.type]=std::move(d);
                fprintf(stderr,"[cppfm] entity data loaded: %s\n",p.string().c_str());
            }catch(const std::exception& e){ fprintf(stderr,"[cppfm] entity json %s skipped: %s\n",p.string().c_str(),e.what()); }
        }
        fprintf(stderr,"[cppfm] entity data total %zu types from %s\n",defs_.size(),dir.c_str());
    }
    const EntityDataDef* get(const std::string& t) const{ auto it=defs_.find(t); return it==defs_.end()?nullptr:&it->second; }
    const std::unordered_map<std::string,EntityDataDef>& all() const{ return defs_; }
private: std::unordered_map<std::string,EntityDataDef> defs_;
};
}
