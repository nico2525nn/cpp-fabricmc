// EntityData: data-driven entity definitions loaded from assets/entities/*.json
// BehaviorTree built via factory (switch on type string) and stored in def for Brain assignment.
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
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
    struct Behavior{
        std::string type;
        int priority=0;
        std::vector<Behavior> children;
    };
    std::vector<Behavior> behaviors;
    std::vector<std::string> sensors;
    std::unordered_map<int,std::string> equipment;
    std::string loot;
    int spawnWeight = 10;
    int spawnMinCount = 1;
    int spawnMaxCount = 4;
    std::string spawnGroup;
    std::shared_ptr<BehaviorTree> behaviorTree;
};
class EntityDataLoader{
public:
    static std::shared_ptr<BehaviorTree> buildTreeFor(const EntityDataDef& def);
    static std::unique_ptr<BehaviorTree> buildUniqueTreeFor(const EntityDataDef& def);
    void loadDirectory(const std::string& dir);
    const EntityDataDef* get(const std::string& t) const{ auto it=defs_.find(t); return it==defs_.end()?nullptr:&it->second; }
    const std::unordered_map<std::string,EntityDataDef>& all() const{ return defs_; }
private:
    static EntityDataDef::Behavior parseBehaviorNode(const json::Value& v);
    std::unordered_map<std::string,EntityDataDef> defs_;
};
}
