#include "EntityData.hpp"
#include "BehaviorTree.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <algorithm>
#include <cctype>

namespace cppfm {

EntityDataDef::Behavior EntityDataLoader::parseBehaviorNode(const json::Value& v) {
    EntityDataDef::Behavior out;
    if (v.type == json::Value::Type::Str) {
        out.type = v.asStr();
        out.priority = 0;
        return out;
    }
    if (v.type != json::Value::Type::Obj) return out;
    out.type = v.at("type").asStr();
    out.priority = v.at("priority").asInt(0);
    // also support "prio" alias
    if (out.priority==0) out.priority = v.at("prio").asInt(0);
    const json::Value* children = nullptr;
    // try multiple keys for children
    children = v.find("children");
    if (!children || children->type != json::Value::Type::Arr) children = v.find("child");
    if (!children || children->type != json::Value::Type::Arr) children = v.find("nodes");
    if (!children || children->type != json::Value::Type::Arr) children = v.find("behaviors");
    if (children && children->type == json::Value::Type::Arr) {
        for (auto& ch : children->arr) {
            auto sub = parseBehaviorNode(ch);
            if (!sub.type.empty() || !sub.children.empty()) out.children.push_back(std::move(sub));
        }
    }
    // If type is empty but children exist, treat as selector implicitly
    return out;
}

static std::string toLowerCopy(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
static std::string stripPrefix(const std::string& t) {
    auto pos = t.find(':');
    if (pos != std::string::npos) return t.substr(pos+1);
    return t;
}
static bool isSelectorType(const std::string& raw) {
    std::string t = toLowerCopy(stripPrefix(raw));
    return t=="selector" || t=="select" || t=="minecraft:selector";
}
static bool isSequenceType(const std::string& raw) {
    std::string t = toLowerCopy(stripPrefix(raw));
    return t=="sequence" || t=="seq" || t=="minecraft:sequence";
}

static std::unique_ptr<BehaviorNode> nodeFromBehavior(const EntityDataDef::Behavior& beh) {
    if (isSelectorType(beh.type)) {
        auto sel = std::make_unique<SelectorNode>();
        for (auto& ch : beh.children) {
            auto node = nodeFromBehavior(ch);
            if (node) sel->addChild(std::move(node));
        }
        // allow direct type list children fallback: if no children but beh.type contains composite, create empty
        return sel;
    }
    if (isSequenceType(beh.type)) {
        auto seq = std::make_unique<SequenceNode>();
        for (auto& ch : beh.children) {
            auto node = nodeFromBehavior(ch);
            if (node) seq->addChild(std::move(node));
        }
        return seq;
    }
    // If behavior has children but type is not selector/sequence, treat as sequence of condition + action?
    // For example old flat behaviors with priority: just leaf.
    // If leaf has children, wrap as sequence where first children are conditions and last is action.
    if (!beh.children.empty()) {
        // Build a sequence where children nodes are ticked in order and finally this behavior leaf
        auto seq = std::make_unique<SequenceNode>();
        for (auto& ch : beh.children) {
            auto node = nodeFromBehavior(ch);
            if (node) seq->addChild(std::move(node));
        }
        auto leaf = createNodeForType(beh.type);
        if (leaf) seq->addChild(std::move(leaf));
        return seq;
    }
    return createNodeForType(beh.type);
}

std::shared_ptr<BehaviorTree> EntityDataLoader::buildTreeFor(const EntityDataDef& def) {
    auto uniq = buildUniqueTreeFor(def);
    if (!uniq) return nullptr;
    return std::shared_ptr<BehaviorTree>(std::move(uniq));
}

std::unique_ptr<BehaviorTree> EntityDataLoader::buildUniqueTreeFor(const EntityDataDef& def) {
    if (def.behaviors.empty()) return nullptr;
    auto sorted = def.behaviors;
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b){ return a.priority < b.priority; });
    // If single top-level selector/sequence already, just build that node directly
    if (sorted.size()==1 && (isSelectorType(sorted[0].type) || isSequenceType(sorted[0].type))) {
        auto root = nodeFromBehavior(sorted[0]);
        if (!root) return nullptr;
        return std::make_unique<BehaviorTree>(std::move(root));
    }
    auto sel = std::make_unique<SelectorNode>();
    for (auto& beh : sorted) {
        auto node = nodeFromBehavior(beh);
        if (node) sel->addChild(std::move(node));
    }
    if (sel) {
        // If only one child and it's already selector/sequence, unwrap? keep sel anyway
    }
    return std::make_unique<BehaviorTree>(std::move(sel));
}

void EntityDataLoader::loadDirectory(const std::string& dir){
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
                if(bh.type==json::Value::Type::Arr) for(auto &b: bh.arr){
                    auto beh = parseBehaviorNode(b);
                    if(!beh.type.empty() || !beh.children.empty()) d.behaviors.push_back(std::move(beh));
                }
                // also support behaviors as object with root node: e.g., {"type":"selector","children":[...]}
                else if(bh.type==json::Value::Type::Obj) {
                    auto beh = parseBehaviorNode(bh);
                    if(!beh.type.empty() || !beh.children.empty()) d.behaviors.push_back(std::move(beh));
                }
                const auto &ss=brain.at("sensors");
                if(ss.type==json::Value::Type::Arr) for(auto &s: ss.arr) if(s.isStr()) d.sensors.push_back(s.asStr());
            }
            const auto &eq=v.at("equipment");
            if(eq.type==json::Value::Type::Obj){ for(auto &kv: eq.obj){ int slot=-1; const std::string &k=kv.first; if(k=="mainhand") slot=0; else if(k=="offhand") slot=1; else if(k=="feet"||k=="boots") slot=2; else if(k=="legs"||k=="leggings") slot=3; else if(k=="chest") slot=4; else if(k=="head"||k=="helmet") slot=5; else{ try{slot=std::stoi(k);}catch(...){continue;}} if(slot>=0&&slot<6){ if(kv.second.isStr()) d.equipment[slot]=kv.second.asStr(); else if(kv.second.type==json::Value::Type::Obj){ auto it=kv.second.find("item"); if(it&&it->isStr()) d.equipment[slot]=it->asStr(); } } } }
            const auto &loot=v.at("loot"); if(loot.isStr()) d.loot=loot.asStr();
            // build prototype behavior tree data-driven
            d.behaviorTree = buildTreeFor(d);
            if (d.behaviorTree) {
                fprintf(stderr,"[cppfm] entity data loaded: %s (behaviors=%zu, tree=1)\n",p.string().c_str(), d.behaviors.size());
            } else {
                fprintf(stderr,"[cppfm] entity data loaded: %s (behaviors=%zu)\n",p.string().c_str(), d.behaviors.size());
            }
            defs_[d.type]=std::move(d);
        }catch(const std::exception& e){ fprintf(stderr,"[cppfm] entity json %s skipped: %s\n",p.string().c_str(),e.what()); }
    }
    fprintf(stderr,"[cppfm] entity data total %zu types from %s\n",defs_.size(),dir.c_str());
}

} // namespace cppfm
