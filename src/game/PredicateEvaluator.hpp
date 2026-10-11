#pragma once

#include "../core/Json.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cppfm {

class World;
class GameRuleManager;
class Scoreboard;
struct Player;
struct MobEntity;
struct DamageSource;
class TagManager;

struct PredicateContext {
    World* world = nullptr;
    GameRuleManager* gamerules = nullptr;
    Player* player = nullptr;
    MobEntity* entity = nullptr;
    std::int32_t x = 0, y = -64, z = 0;
    float explosionRadius = 0.0F;
    std::int64_t dayTime = 0;
    bool raining = false;
    bool thundering = false;
    bool hasKiller = false;
    bool killedByPlayer = false;
    int fortuneLevel = 0;
    bool silkTouch = false;
    Scoreboard* scoreboard = nullptr;
    int valueCheckValue = 0;
    bool hasValueCheck = false;
    std::string playerName;
    std::string heldItemName;
    DamageSource* damageSource = nullptr;
    std::string nbt;
    std::unordered_set<std::string> advancements;
    std::string dimension;
    std::uint64_t randomSeed = 0;
    int lootingLevel = 0;
};

class PredicateEvaluator final {
public:
    PredicateEvaluator(
        const TagManager& tagManager,
        const std::unordered_map<std::string, std::string>& predicates)
        : tagManager_(tagManager), predicates_(predicates) {}

    bool evaluatePredicateValue(const json::Value& value,
                                const PredicateContext& context,
                                int depth = 0) const;
    bool evaluatePredicateValue(const json::Value& value) const;
    bool testPredicate(const std::string& id,
                       const PredicateContext& context,
                       int depth = 0) const;
    bool testPredicate(const std::string& id) const;

private:
    const TagManager& tagManager_;
    const std::unordered_map<std::string, std::string>& predicates_;
};

} // namespace cppfm
