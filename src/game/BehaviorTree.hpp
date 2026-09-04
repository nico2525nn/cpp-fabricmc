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

class BlazeFireballAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};
class GuardianBeamAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};
class GhastFireballAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};
class PhantomSwoopAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};
class ShulkerBulletAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};
class WardenSonicBoomAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};
class GenericRangedAttackAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};

// generic wander fallback
class WanderAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override ;
};
class WitchPotionAction : public BehaviorNode { public: BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class RavagerRoarAction : public BehaviorNode { public: BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class IronGolemDefendAction : public BehaviorNode { public: BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class BeePollinateAction : public BehaviorNode { public: BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class VillagerScheduleAction : public BehaviorNode { public: BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class WolfAngerAction : public BehaviorNode { public: BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class DrownedTridentAction : public BehaviorNode { public: BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class PiglinBarterAction : public BehaviorNode { public: BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class CatScareAction : public BehaviorNode { public: BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class FoxPounceAction : public BehaviorNode { public: BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class DolphinPlayAction : public BehaviorNode { public: BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class EvokerFangAction : public BehaviorNode { public: BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };

inline std::unique_ptr<BehaviorNode> createNodeForType(const std::string& rawType) {
    std::string t = rawType;
    // strip prefix minecraft:
    auto pos = t.find(':'); if (pos!=std::string::npos) t = t.substr(pos+1);
    // lower
    for(auto& c: t) c=(char)std::tolower((unsigned char)c);
    // conditions
    if (t=="is_player_in_range"||t=="player_in_range"||t=="look_at_player") return std::make_unique<IsPlayerInRangeCondition>(8.0);
    if (t=="is_hurt"||t=="hurt"||t=="panic") return std::make_unique<IsHurtCondition>();
    if (t=="is_block_above"||t=="block_above") return std::make_unique<IsBlockAboveCondition>();
    if (t=="is_player_looking"||t=="player_looking"||t=="is_player_staring") return std::make_unique<IsPlayerLookingCondition>();
    if (t=="can_breed"||t=="breed"||t=="breeding") return std::make_unique<CanBreedCondition>();
    // actions - movement/attack
    if (t=="move_to_player"||t=="move_to_target"||t=="chase") return std::make_unique<MoveToPlayerAction>();
    if (t=="attack_player"||t=="melee_attack"||t=="attack") return std::make_unique<AttackPlayerAction>();
    if (t=="teleport_random"||t=="teleport") return std::make_unique<TeleportRandomAction>();
    if (t=="pickup_block"||t=="pickup") return std::make_unique<PickupBlockAction>();
    if (t=="stare") return std::make_unique<StareAction>();
    if (t=="wither_skull"||t=="wither_skull_attack"||t=="wither_shoot") return std::make_unique<WitherSkullAction>();
    if (t=="dragon_breath"||t=="dragon_breath_attack"||t=="dragon_fireball") return std::make_unique<DragonBreathAction>();
    if (t=="blaze_fireball"||t=="blaze_shoot"||t=="blaze_attack") return std::make_unique<BlazeFireballAction>();
    if (t=="guardian_beam"||t=="guardian_attack"||t=="elder_guardian_beam") return std::make_unique<GuardianBeamAction>();
    if (t=="ghast_fireball"||t=="ghast_shoot") return std::make_unique<GhastFireballAction>();
    if (t=="phantom_swoop"||t=="phantom_attack") return std::make_unique<PhantomSwoopAction>();
    if (t=="shulker_bullet"||t=="shulker_shoot") return std::make_unique<ShulkerBulletAction>();
    if (t=="warden_sonic_boom"||t=="sonic_boom"||t=="warden_attack") return std::make_unique<WardenSonicBoomAction>();
    if (t=="ranged_attack"||t=="shoot"||t=="fireball") return std::make_unique<GenericRangedAttackAction>();
    if (t=="swell"||t=="creeper_swell"||t=="swell_goal") return std::make_unique<WanderAction>(); // swell is Goal-layer (AiBrain::SwellGoal), BT fallback wander
    if (t=="avoid_entity"||t=="avoid") return std::make_unique<WanderAction>();
    if (t=="flee_sun"||t=="flee_sunlight") return std::make_unique<WanderAction>();
    if (t=="leap_at_target"||t=="leap") return std::make_unique<WanderAction>();
    if (t=="breeze_jump"||t=="breeze_wind_charge"||t=="wind_charge"||t=="armadillo_roll_up"||t=="roll_up") return std::make_unique<WanderAction>();
    if (t=="zombie_attack"||t=="skeleton_attack"||t=="spider_attack"||t=="warden_attack") return std::make_unique<GenericRangedAttackAction>();
    if (t=="witch_throw_potion"||t=="witch_potion"||t=="throw_potion"||t=="witch_attack") return std::make_unique<WitchPotionAction>();
    if (t=="ravager_roar"||t=="roar") return std::make_unique<RavagerRoarAction>();
    if (t=="defend_village"||t=="iron_golem_defend"||t=="golem_defend") return std::make_unique<IronGolemDefendAction>();
    if (t=="pollinate"||t=="bee_pollinate") return std::make_unique<BeePollinateAction>();
    if (t=="villager_schedule"||t=="schedule") return std::make_unique<VillagerScheduleAction>();
    if (t=="wolf_anger"||t=="wolf_angry") return std::make_unique<WolfAngerAction>();
    if (t=="drowned_trident"||t=="trident") return std::make_unique<DrownedTridentAction>();
    if (t=="piglin_barter"||t=="barter") return std::make_unique<PiglinBarterAction>();
    if (t=="cat_scare"||t=="scare_creeper") return std::make_unique<CatScareAction>();
    if (t=="fox_pounce"||t=="pounce") return std::make_unique<FoxPounceAction>();
    if (t=="dolphin_play"||t=="play") return std::make_unique<DolphinPlayAction>();
    if (t=="evoker_fang"||t=="fang"||t=="evoker_attack") return std::make_unique<EvokerFangAction>();
    if (t=="breed_action") return std::make_unique<BreedAction>();
    if (t=="trade"||t=="trade_goal") return std::make_unique<TradeAction>();
    if (t=="swim_wander"||t=="fish_swim"||t=="fly_wander"||t=="bat_roost") return std::make_unique<WanderAction>();
    if (t=="graze"||t=="eat_grass"||t=="nibble_carrots"||t=="raid_crops") return std::make_unique<WanderAction>();
    if (t=="boat_drift"||t=="boat_float") return std::make_unique<WanderAction>();
    if (t=="minecart_roll") return std::make_unique<WanderAction>();
    if (t=="projectile_fly") return std::make_unique<WanderAction>();
    if (t=="crystal_hover") return std::make_unique<WanderAction>();
    if (t=="tnt_fuse") return std::make_unique<WanderAction>();
    if (t=="stationary_object"||t=="object_idle") return std::make_unique<WanderAction>();
    if (t=="creaking_heart_link") return std::make_unique<WanderAction>();
    if (t=="cure_shake") return std::make_unique<WanderAction>();
    if (t=="trap_spawn") return std::make_unique<WanderAction>();
    if (t=="lay_egg") return std::make_unique<WanderAction>();
    if (t=="puff_defense") return std::make_unique<WanderAction>();
    if (t=="allay_duplicate") return std::make_unique<WanderAction>();
    if (t=="bat_roost") return std::make_unique<WanderAction>();
    if (t=="armor_stand_pose"||t=="ominous_spawn") return std::make_unique<WanderAction>();
    if (t=="xp_magnet"||t=="item_magnet") return std::make_unique<WanderAction>();
    if (t=="falling_gravity") return std::make_unique<WanderAction>();
    if (t=="lightning_strike") return std::make_unique<WanderAction>();
    if (t=="hoglin_repel"||t=="repel") return std::make_unique<WanderAction>();
    if (t=="magma_cube_jump"||t=="magma_jump") return std::make_unique<WanderAction>();
    if (t=="silverfish_infest"||t=="infest") return std::make_unique<WanderAction>();
    if (t=="slime_hop"||t=="slime") return std::make_unique<WanderAction>();
    if (t=="snow_trail"||t=="turtle_home") return std::make_unique<WanderAction>();
    if (t=="vex_charge"||t=="vex_attack") return std::make_unique<AttackPlayerAction>();
    if (t=="brute_attack"||t=="piglin_brute_attack") return std::make_unique<AttackPlayerAction>();
    if (t=="pack_anger"||t=="zombified_anger") return std::make_unique<AttackPlayerAction>();
    if (t=="stomp"||t=="giant_stomp") return std::make_unique<AttackPlayerAction>();
    if (t=="husk_hunger"||t=="husk_attack") return std::make_unique<AttackPlayerAction>();
    if (t=="bear_defend"||t=="polar_bear_defend") return std::make_unique<AttackPlayerAction>();
    if (t=="fangs_snap"||t=="evoker_fangs_snap") return std::make_unique<AttackPlayerAction>();
    if (t=="vindicator_axe"||t=="axe_attack") return std::make_unique<AttackPlayerAction>();
    if (t=="zoglin_frenzy"||t=="frenzy") return std::make_unique<AttackPlayerAction>();
    if (t=="llama_spit"||t=="spit") return std::make_unique<GenericRangedAttackAction>();
    if (t=="axolotl_play_dead"||t=="bogged_poison"||t=="camel_dash") return std::make_unique<WanderAction>();
    if (t=="endermite_teleport"||t=="frog_tongue"||t=="goat_ram") return std::make_unique<WanderAction>();
    if (t=="illusioner_invis"||t=="ocelot_trust"||t=="panda_roll") return std::make_unique<WanderAction>();
    if (t=="parrot_dance"||t=="sniffer_dig"||t=="strider_lava_walk") return std::make_unique<WanderAction>();
    if (t=="wither_skeleton_effect"||t=="tempt") return std::make_unique<WanderAction>();
    if (t=="wander"||t=="wander_around") return std::make_unique<WanderAction>();
    // composite keys fallback to selector/sequence (handled in EntityData parsing)
    if (t=="selector"||t=="sequence") return nullptr;
    // fallback: treat unknown as wander/action generic
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

// Build tree from a list of behavior type strings with priorities. Root is Selector ordered by priority (lowest priority number first).
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
