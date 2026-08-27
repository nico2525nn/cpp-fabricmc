// BehaviorTree engine (plan6 item 29,39,40,43,44,35)
// Data-driven behavior tree with Selector/Sequence/Condition/Action nodes.
// Factory builds tree from EntityDataDef.behaviors JSON array.
#pragma once
#include <memory>
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include "Entities.hpp"
#include "AiBrain.hpp"

namespace cppfm {

enum class BTStatus { Success, Failure, Running };

// Base node
class BehaviorNode {
public:
    virtual ~BehaviorNode() = default;
    virtual bool canUse(MobEntity&, AiContext&) { return true; }
    virtual BTStatus tick(MobEntity&, AiContext&, std::int64_t) = 0;
};

// ---------- composite ----------
class SelectorNode : public BehaviorNode {
public:
    void addChild(std::unique_ptr<BehaviorNode> c){ children_.push_back(std::move(c)); }
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override {
        for (auto& ch : children_) {
            if (!ch->canUse(m, ctx)) continue;
            BTStatus s = ch->tick(m, ctx, now);
            if (s == BTStatus::Success || s == BTStatus::Running) return s;
        }
        return BTStatus::Failure;
    }
private:
    std::vector<std::unique_ptr<BehaviorNode>> children_;
};

class SequenceNode : public BehaviorNode {
public:
    void addChild(std::unique_ptr<BehaviorNode> c){ children_.push_back(std::move(c)); }
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override {
        for (auto& ch : children_) {
            if (!ch->canUse(m, ctx)) return BTStatus::Failure;
            BTStatus s = ch->tick(m, ctx, now);
            if (s != BTStatus::Success) return s;
        }
        return BTStatus::Success;
    }
private:
    std::vector<std::unique_ptr<BehaviorNode>> children_;
};

// ---------- conditions ----------
class IsPlayerInRangeCondition : public BehaviorNode {
public:
    explicit IsPlayerInRangeCondition(double r=16.0): range_(r) {}
    BTStatus tick(MobEntity&, AiContext& ctx, std::int64_t) override {
        if (!ctx.nearestPlayer) return BTStatus::Failure;
        return ctx.nearestPlayerDist2 < range_*range_ ? BTStatus::Success : BTStatus::Failure;
    }
private: double range_;
};

class IsHurtCondition : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t) override ;
};

class IsBlockAboveCondition : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t) override {
        if (!ctx.world) return BTStatus::Failure;
        int bx = (int)std::floor(m.x);
        int by = (int)std::floor(m.y) + 2;
        int bz = (int)std::floor(m.z);
        std::uint16_t st = ctx.world->getBlock(bx,by,bz);
        return st != 0 ? BTStatus::Success : BTStatus::Failure;
    }
};

class IsPlayerLookingCondition : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t) override ;
};

class CanBreedCondition : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext&, std::int64_t now) override {
        if (!m.inLove) return BTStatus::Failure;
        if (now > m.loveUntilTick) return BTStatus::Failure;
        if (MobEntity::isBaby(m)) return BTStatus::Failure;
        return BTStatus::Success;
    }
};

// ---------- actions ----------
class MoveToPlayerAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t) override ;
};

class AttackPlayerAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};

class TeleportRandomAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};

class PickupBlockAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t) override ;
};

class StareAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t) override ;
};

class WitherSkullAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};

enum class DragonPhase { Circling, ApproachingPerch, Perching, BreathAttack, Takeoff };

class DragonBreathAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};

class BreedAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};

class TradeAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t) override ;
};

// generic wander fallback
class WanderAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};

// Factory helper
inline std::unique_ptr<BehaviorNode> createNodeForType(const std::string& rawType) {
    std::string t = rawType;
    // strip prefix minecraft:
    auto pos = t.find(':'); if (pos!=std::string::npos) t = t.substr(pos+1);
    // lower
    for(auto& c: t) c=(char)std::tolower(c);
    if (t=="is_player_in_range"||t=="player_in_range"||t=="look_at_player") return std::make_unique<IsPlayerInRangeCondition>(8.0);
    if (t=="is_hurt"||t=="hurt"||t=="panic") return std::make_unique<IsHurtCondition>();
    if (t=="is_block_above"||t=="block_above") return std::make_unique<IsBlockAboveCondition>();
    if (t=="is_player_looking"||t=="player_looking"||t=="is_player_staring") return std::make_unique<IsPlayerLookingCondition>();
    if (t=="can_breed"||t=="breed"||t=="breeding") return std::make_unique<CanBreedCondition>();
    if (t=="move_to_player"||t=="move_to_target") return std::make_unique<MoveToPlayerAction>();
    if (t=="attack_player"||t=="melee_attack") return std::make_unique<AttackPlayerAction>();
    if (t=="teleport_random"||t=="teleport") return std::make_unique<TeleportRandomAction>();
    if (t=="pickup_block"||t=="pickup") return std::make_unique<PickupBlockAction>();
    if (t=="stare") return std::make_unique<StareAction>();
    if (t=="wither_skull_attack"||t=="wither_skull") return std::make_unique<WitherSkullAction>();
    if (t=="dragon_breath"||t=="dragon_fireball") return std::make_unique<DragonBreathAction>();
    if (t=="breed_action") return std::make_unique<BreedAction>();
    if (t=="trade"||t=="trade_goal") return std::make_unique<TradeAction>();
    if (t=="wander"||t=="wander_around") return std::make_unique<WanderAction>();
    // fallback: treat unknown as wander
    return std::make_unique<WanderAction>();
}

// BehaviorTree wrapper
class BehaviorTree {
public:
    explicit BehaviorTree(std::unique_ptr<BehaviorNode> root): root_(std::move(root)) {}
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now){
        if (!root_) return BTStatus::Failure;
        if (!root_->canUse(m, ctx)) return BTStatus::Failure;
        return root_->tick(m, ctx, now);
    }
private:
    std::unique_ptr<BehaviorNode> root_;
};

// Build tree from a list of behavior type strings with priorities.
// Root is Selector ordered by priority (lowest priority number first).
inline std::unique_ptr<BehaviorTree> buildBehaviorTreeFromTypes(const std::vector<std::pair<std::string,int>>& entries) {
    if (entries.empty()) return nullptr;
    auto sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b){ return a.second < b.second; });
    auto sel = std::make_unique<SelectorNode>();
    for (auto& e : sorted) {
        auto node = createNodeForType(e.first);
        // wrap with sequence if condition? For simplicity, each behavior becomes its own node directly.
        // But for Is* conditions we already have them; they will be evaluated as standalone.
        // For more complex tree, user could define composite via JSON nesting (not needed).
        sel->addChild(std::move(node));
    }
    return std::make_unique<BehaviorTree>(std::move(sel));
}

// Enderman specific tree builder (item 39)
inline std::unique_ptr<BehaviorTree> buildEndermanTree() {
    auto root = std::make_unique<SelectorNode>();
    // Branch1: IsPlayerLooking -> Stare
    {
        auto seq = std::make_unique<SequenceNode>();
        seq->addChild(std::make_unique<IsPlayerLookingCondition>());
        seq->addChild(std::make_unique<StareAction>());
        root->addChild(std::move(seq));
    }
    // Branch2: IsHurt -> TeleportRandom
    {
        auto seq = std::make_unique<SequenceNode>();
        seq->addChild(std::make_unique<IsHurtCondition>());
        seq->addChild(std::make_unique<TeleportRandomAction>());
        root->addChild(std::move(seq));
    }
    // Branch3: IsBlockAbove -> PickupBlock
    {
        auto seq = std::make_unique<SequenceNode>();
        seq->addChild(std::make_unique<IsBlockAboveCondition>());
        seq->addChild(std::make_unique<PickupBlockAction>());
        root->addChild(std::move(seq));
    }
    // Fallback wander
    root->addChild(std::make_unique<WanderAction>());
    return std::make_unique<BehaviorTree>(std::move(root));
}

// Wither tree
inline std::unique_ptr<BehaviorTree> buildWitherTree() {
    auto root = std::make_unique<SelectorNode>();
    root->addChild(std::make_unique<WitherSkullAction>());
    root->addChild(std::make_unique<MoveToPlayerAction>());
    root->addChild(std::make_unique<WanderAction>());
    return std::make_unique<BehaviorTree>(std::move(root));
}

// Dragon tree
inline std::unique_ptr<BehaviorTree> buildDragonTree() {
    auto root = std::make_unique<SelectorNode>();
    root->addChild(std::make_unique<DragonBreathAction>());
    root->addChild(std::make_unique<WanderAction>());
    return std::make_unique<BehaviorTree>(std::move(root));
}

} // namespace cppfm
