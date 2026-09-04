// Brain/Goal/Sensor framework (plan3.md "Brain-Goal-Sensorフレームワーク").
//
// Each mob owns a Brain with a priority-sorted Goal list and Sensors that
// refresh shared AiContext knowledge. Every tick the Brain picks the highest
// priority goal whose shouldStart() passes, stops the previous one, and runs
// the active goal's tick().
#pragma once
#include <algorithm>
#include <memory>
#include <vector>
#include "Ai.hpp"
#include "Entities.hpp"

namespace cppfm {

class GameServer;
struct Player;

struct AiContext {
    GameServer* srv = nullptr;
    World* world = nullptr;
    Player* nearestPlayer = nullptr;
    double nearestPlayerDist2 = 1e300;
    Player* temptingPlayer = nullptr;      // holding breeding food
    std::int32_t lastHurtByEntityId = -1;
    std::int64_t lastHurtTick = -1000;
    // plan34 §3 armadillo scare sensor state
    bool dangerDetectedRecently = false;
    // active path
    std::vector<ai::PathNode> path;
    std::size_t pathIdx = 0;

    void resetPerception() {
        nearestPlayer = nullptr;
        nearestPlayerDist2 = 1e300;
        temptingPlayer = nullptr;
        dangerDetectedRecently = false;
    }
};

class Goal {
public:
    explicit Goal(int pri) : priority(pri) {}
    virtual ~Goal() = default;
    const int priority;
    virtual bool shouldStart(MobEntity&, AiContext&) { return true; }
    virtual void start(MobEntity&, AiContext&) {}
    virtual bool tick(MobEntity&, AiContext&, std::int64_t) = 0;   // keep running?
    virtual void stop(MobEntity&, AiContext&) {}
};

// ------------------------------------------------------------- sensors ----

class NearestPlayerSensor {
public:
    static void update(MobEntity& m, AiContext& ctx);
};

// -------------------------------------------------------------- goals -----

class PanicGoal final : public Goal {
public:
    PanicGoal() : Goal(1) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};

class MeleeAttackGoal final : public Goal {
public:
    MeleeAttackGoal() : Goal(3) {}
    bool shouldStart(MobEntity&, AiContext& c) override { return c.nearestPlayer != nullptr; }
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};

// Ranged attack for skeletons (plan4 P1-A) — plan34 §2 extends to Stray/Bogged/Piglin/Brute/Blaze/Ghast/SnowGolem
class RangedAttackGoal final : public Goal {
public:
    RangedAttackGoal() : Goal(3) {}
    static bool isRangedKind(MobKind k) {
        return k == MobKind::Skeleton || k == MobKind::Stray || k == MobKind::Bogged
            || k == MobKind::Piglin || k == MobKind::PiglinBrute || k == MobKind::SnowGolem
            || k == MobKind::Blaze || k == MobKind::Ghast;
    }
    bool shouldStart(MobEntity& m, AiContext& c) override {
        return isRangedKind(m.kind) && c.nearestPlayer != nullptr;
    }
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};

// plan34 §2 five new Goal nodes
class SwellGoal final : public Goal {
public:
    SwellGoal() : Goal(1) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class AvoidEntityGoal final : public Goal {
public:
    AvoidEntityGoal(double dist=6.0) : Goal(3), dist2_(dist*dist) {}
    explicit AvoidEntityGoal(MobKind t, double dist=6.0) : Goal(3), target_(t), dist2_(dist*dist) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
private:
    MobKind target_ = MobKind::Pig; bool hasTarget_=false; double dist2_=36;
};
class FleeSunGoal final : public Goal {
public:
    FleeSunGoal() : Goal(1) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class LeapAtTargetGoal final : public Goal {
public:
    LeapAtTargetGoal() : Goal(2) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
// plan34 §3 Breeze / Armadillo goals
class BreezeJumpGoal final : public Goal {
public:
    BreezeJumpGoal() : Goal(1) {}
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class BreezeWindChargeGoal final : public Goal {
public:
    BreezeWindChargeGoal() : Goal(2) {}
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class ArmadilloRollUpGoal final : public Goal {
public:
    ArmadilloRollUpGoal() : Goal(1) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};

class TemptGoal final : public Goal {
public:
    TemptGoal() : Goal(4) {}
    bool shouldStart(MobEntity&, AiContext& c) override { return c.temptingPlayer != nullptr; }
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};

class BreedGoal final : public Goal {
public:
    BreedGoal() : Goal(2) {}
    bool shouldStart(MobEntity&, AiContext& c) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};

class WanderAroundGoal final : public Goal {
public:
    WanderAroundGoal() : Goal(6) {}
    bool shouldStart(MobEntity& m, AiContext&) override {
        return !m.hasTarget && m.age >= 0;
    }
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};

class LookAtPlayerGoal final : public Goal {
public:
    LookAtPlayerGoal() : Goal(7) {}
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};

class CreakingGoal final : public Goal {
public:
    CreakingGoal() : Goal(1) {}
    bool shouldStart(MobEntity& m, AiContext& c) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};

// plan36 §1 12-15 new Goals for 30 species
class WitchPotionThrowGoal final : public Goal {
public: WitchPotionThrowGoal(): Goal(2) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class RavagerRoarGoal final : public Goal {
public: RavagerRoarGoal(): Goal(2) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class IronGolemDefendGoal final : public Goal {
public: IronGolemDefendGoal(): Goal(1) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class BeePollinateGoal final : public Goal {
public: BeePollinateGoal(): Goal(4) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class WolfAngerGoal final : public Goal {
public: WolfAngerGoal(): Goal(2) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class DrownedTridentGoal final : public Goal {
public: DrownedTridentGoal(): Goal(3) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class VillagerScheduleGoal final : public Goal {
public: VillagerScheduleGoal(): Goal(6) {}
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
    // plan46 G-15: villager 10-activity schedule (work 2000-9000, sleep at night); pure for tests.
    static const char* activityFor(int tod) {
        int t = ((tod % 24000) + 24000) % 24000;
        if (t < 2000) return "sleep";    // 0-2000 night tail
        if (t < 9000) return "work";     // 2000-9000 work (Trading restock window)
        if (t < 10000) return "gather";  // 9000-10000 midday gather
        if (t < 11000) return "mingle";  // 10000-11000 gossip/mingle
        if (t < 12000) return "wander";  // 11000-12000 wander
        if (t < 12500) return "play";    // 12000-12500 babies play
        if (t < 13000) return "idle";    // 12500-13000 dusk idle
        if (t < 14000) return "home";    // 13000-14000 return home
        if (t < 23000) return "sleep";   // 14000-23000 night sleep
        return "rest";                   // 23000-24000 pre-dawn rest
    }
    static bool isWorkTime(int tod) {
        int t = ((tod % 24000) + 24000) % 24000;
        return t >= 2000 && t < 9000;
    }
};
class PiglinBarterGoal final : public Goal {
public: PiglinBarterGoal(): Goal(3) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class CatScareGoal final : public Goal {
public: CatScareGoal(): Goal(3) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class FoxPounceGoal final : public Goal {
public: FoxPounceGoal(): Goal(3) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class PandaRollGoal final : public Goal {
public: PandaRollGoal(): Goal(4) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class DolphinPlayGoal final : public Goal {
public: DolphinPlayGoal(): Goal(4) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};
class EvokerFangGoal final : public Goal {
public: EvokerFangGoal(): Goal(2) {}
    bool shouldStart(MobEntity& m, AiContext& ctx) override;
    bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override;
};

// plan39 C-01: 30 new goals (60 species)
class DrownedSwimGoal final : public Goal { public: DrownedSwimGoal(): Goal(1) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class PhantomCircleGoal final : public Goal { public: PhantomCircleGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class WardenSonicBoomGoal final : public Goal { public: WardenSonicBoomGoal(): Goal(1) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class EndermanTeleportGoal final : public Goal { public: EndermanTeleportGoal(): Goal(1) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class ShulkerPeekGoal final : public Goal { public: ShulkerPeekGoal(): Goal(1) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class GuardianBeamGoal final : public Goal { public: GuardianBeamGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class SlimeSplitGoal final : public Goal { public: SlimeSplitGoal(): Goal(3) {} bool shouldStart(MobEntity& m, AiContext&) override { return m.kind==MobKind::Slime; } bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class MagmaCubeJumpGoal final : public Goal { public: MagmaCubeJumpGoal(): Goal(3) {} bool shouldStart(MobEntity& m, AiContext&) override { return m.kind==MobKind::MagmaCube; } bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class SilverfishInfestGoal final : public Goal { public: SilverfishInfestGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class EndermiteTeleportGoal final : public Goal { public: EndermiteTeleportGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class VindicatorAxeGoal final : public Goal { public: VindicatorAxeGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class PillagerCrossbowGoal final : public Goal { public: PillagerCrossbowGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class HoglinRepelGoal final : public Goal { public: HoglinRepelGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class ZoglinFrenzyGoal final : public Goal { public: ZoglinFrenzyGoal(): Goal(1) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class WitherSkeletonEffectGoal final : public Goal { public: WitherSkeletonEffectGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class GoatRamGoal final : public Goal { public: GoatRamGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class AxolotlPlayDeadGoal final : public Goal { public: AxolotlPlayDeadGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class FrogTongueGoal final : public Goal { public: FrogTongueGoal(): Goal(3) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class TurtleEggLayGoal final : public Goal { public: TurtleEggLayGoal(): Goal(3) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class ParrotDanceGoal final : public Goal { public: ParrotDanceGoal(): Goal(3) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class OcelotTrustGoal final : public Goal { public: OcelotTrustGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class SnowGolemSnowTrailGoal final : public Goal { public: SnowGolemSnowTrailGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class WitherSkullBarrageGoal final : public Goal { public: WitherSkullBarrageGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class EnderDragonPerchGoal final : public Goal { public: EnderDragonPerchGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class StriderLavaWalkGoal final : public Goal { public: StriderLavaWalkGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class IllusionerInvisGoal final : public Goal { public: IllusionerInvisGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class SnifferDigGoal final : public Goal { public: SnifferDigGoal(): Goal(3) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class CamelDashGoal final : public Goal { public: CamelDashGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class AllayDuplicateGoal final : public Goal { public: AllayDuplicateGoal(): Goal(3) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class BoggedPoisonGoal final : public Goal { public: BoggedPoisonGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
// plan42 R2 E-11: per-species default Goal sets (params via mobStats); family gates share classes.
class FishSwimGoal final : public Goal { public: FishSwimGoal(): Goal(5) {} bool shouldStart(MobEntity& m, AiContext&) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class GrazeGoal final : public Goal { public: GrazeGoal(): Goal(5) {} bool shouldStart(MobEntity& m, AiContext&) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class BoatDriftGoal final : public Goal { public: BoatDriftGoal(): Goal(5) {} bool shouldStart(MobEntity& m, AiContext&) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class MinecartRollGoal final : public Goal { public: MinecartRollGoal(): Goal(5) {} bool shouldStart(MobEntity& m, AiContext&) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class VexChargeGoal final : public Goal { public: VexChargeGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class PiglinBruteAttackGoal final : public Goal { public: PiglinBruteAttackGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class ZombieVillagerCureGoal final : public Goal { public: ZombieVillagerCureGoal(): Goal(3) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class ZombifiedPiglinAngerGoal final : public Goal { public: ZombifiedPiglinAngerGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext&) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class SkeletonHorseTrapGoal final : public Goal { public: SkeletonHorseTrapGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class GiantStompGoal final : public Goal { public: GiantStompGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class LlamaSpitGoal final : public Goal { public: LlamaSpitGoal(): Goal(3) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class ChickenLayEggGoal final : public Goal { public: ChickenLayEggGoal(): Goal(4) {} bool shouldStart(MobEntity& m, AiContext&) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class HuskHungerGoal final : public Goal { public: HuskHungerGoal(): Goal(3) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class PolarBearDefendGoal final : public Goal { public: PolarBearDefendGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class PufferfishPuffGoal final : public Goal { public: PufferfishPuffGoal(): Goal(2) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class ProjectileFlyGoal final : public Goal { public: ProjectileFlyGoal(): Goal(5) {} bool shouldStart(MobEntity& m, AiContext&) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class EvokerFangsSnapGoal final : public Goal { public: EvokerFangsSnapGoal(): Goal(1) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class EndCrystalHoverGoal final : public Goal { public: EndCrystalHoverGoal(): Goal(5) {} bool shouldStart(MobEntity& m, AiContext&) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class TntFuseGoal final : public Goal { public: TntFuseGoal(): Goal(1) {} bool shouldStart(MobEntity& m, AiContext& ctx) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class BatRoostGoal final : public Goal { public: BatRoostGoal(): Goal(4) {} bool shouldStart(MobEntity& m, AiContext&) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };
class AmbientObjectGoal final : public Goal { public: AmbientObjectGoal(): Goal(5) {} bool shouldStart(MobEntity& m, AiContext&) override; bool tick(MobEntity& m, AiContext& ctx, std::int64_t now) override; };

// ---------------------------------------------------------------- brain ---

class BehaviorTree; // forward (defined in BehaviorTree.hpp)

class Brain {
public:
    Brain();
    ~Brain();
    void setBehaviorTree(std::unique_ptr<BehaviorTree> t);
    bool hasBehaviorTree() const;
    void tick(MobEntity& m, AiContext& ctx, std::int64_t now);
    std::size_t goalCount() const { return goals_.size(); }
    // plan42 R2 E-11: true if any non-generic goal explicitly gates kind k
    // (group gates: Fish/Graze/Boat/Minecart/Projectile/Ambient families + 13 singles).
    static bool coversKind(MobKind k);

private:
    std::vector<std::unique_ptr<Goal>> goals_;
    Goal* active_ = nullptr;
    bool running_ = false;
    std::unique_ptr<BehaviorTree> behaviorTree_;
};

} // namespace cppfm
