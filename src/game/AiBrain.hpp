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
    // active path
    std::vector<ai::PathNode> path;
    std::size_t pathIdx = 0;

    void resetPerception() {
        nearestPlayer = nullptr;
        nearestPlayerDist2 = 1e300;
        temptingPlayer = nullptr;
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

// Ranged attack for skeletons (plan4 P1-A).
class RangedAttackGoal final : public Goal {
public:
    RangedAttackGoal() : Goal(3) {}
    static bool isRangedKind(MobKind k) { return k == MobKind::Skeleton; }
    bool shouldStart(MobEntity&, AiContext& c) override {
        return c.nearestPlayer != nullptr;
    }
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

// ---------------------------------------------------------------- brain ---

class BehaviorTree; // forward (defined in BehaviorTree.hpp)

class Brain {
public:
    Brain();
    ~Brain();
    void setBehaviorTree(std::unique_ptr<BehaviorTree> t);
    bool hasBehaviorTree() const;
    void tick(MobEntity& m, AiContext& ctx, std::int64_t now);

private:
    std::vector<std::unique_ptr<Goal>> goals_;
    Goal* active_ = nullptr;
    bool running_ = false;
    std::unique_ptr<BehaviorTree> behaviorTree_;
};

} // namespace cppfm
