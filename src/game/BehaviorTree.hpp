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
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t) override {
        if (m.health <= 0) return BTStatus::Failure;
        const float maxH = mobStats(m.kind).maxHealth;
        if (m.health < maxH * 0.9f) return BTStatus::Success;
        if (ctx.lastHurtTick >=0 && ctx.srv && ctx.srv->tickNoForTest() - ctx.lastHurtTick < 20) return BTStatus::Success;
        return BTStatus::Failure;
    }
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
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t) override {
        Player* p = ctx.nearestPlayer;
        if (!p) return BTStatus::Failure;
        // vector from player eyes to mob
        double dx = m.x - p->x;
        double dy = (m.y+1.6) - (p->y + 1.62);
        double dz = m.z - p->z;
        double len = std::sqrt(dx*dx+dy*dy+dz*dz);
        if (len < 1e-6 || len > 64) return BTStatus::Failure;
        dx/=len; dy/=len; dz/=len;
        // player look vector from yaw/pitch
        double yawRad = p->yaw * 3.1415926535 / 180.0;
        double pitchRad = p->pitch * 3.1415926535 / 180.0;
        double lx = -std::sin(yawRad) * std::cos(pitchRad);
        double ly = -std::sin(pitchRad);
        double lz =  std::cos(yawRad) * std::cos(pitchRad);
        double dot = dx*lx + dy*ly + dz*lz;
        // vanilla threshold ~0.99 for staring (narrow cone)
        return dot > 0.985 ? BTStatus::Success : BTStatus::Failure;
    }
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
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t) override {
        Player* t = ctx.nearestPlayer;
        if (!t) return BTStatus::Failure;
        double dx = t->x - m.x, dz = t->z - m.z;
        double d = std::sqrt(dx*dx+dz*dz);
        if (d < 1.9) return BTStatus::Success;
        if (d > 24) return BTStatus::Failure;
        m.yaw = (float)(std::atan2(dz,dx)*180.0/3.1415926535 - 90.0);
        m.x += dx/d * 0.10;
        m.z += dz/d * 0.10;
        if (ctx.srv && ctx.world) {
            // ground snap
            ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
            int col=4;
            ctx.world->withChunk((int)m.x>>4,(int)m.z>>4,[&](const Chunk& c){
                for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, (int)m.z&15, (int)m.x&15)]!=0){col=ry+1;break;}
            });
            m.y = kMinY + col + 1.0;
        }
        return BTStatus::Running;
    }
};

class AttackPlayerAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override {
        Player* t = ctx.nearestPlayer;
        if (!t) return BTStatus::Failure;
        double dx = t->x - m.x, dz = t->z - m.z;
        double d = std::sqrt(dx*dx+dz*dz);
        if (d > 2.2) return BTStatus::Failure;
        if (now % 20 == 0 && ctx.srv) ctx.srv->mobAttackPlayer(m, *t);
        return BTStatus::Success;
    }
};

class TeleportRandomAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override {
        if (now - m.lastTeleportTick < 20) return BTStatus::Failure;
        if (!ctx.world) return BTStatus::Failure;
        for (int attempt=0; attempt<16; ++attempt) {
            double nx = m.x + (rand()/(double)RAND_MAX*64 -32);
            double nz = m.z + (rand()/(double)RAND_MAX*64 -32);
            int ix = (int)std::floor(nx);
            int iz = (int)std::floor(nz);
            ctx.world->generateChunkIfMissing(ix>>4, iz>>4);
            int col = 4;
            bool found=false;
            ctx.world->withChunk(ix>>4, iz>>4,[&](const Chunk& c){
                for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, iz&15, ix&15)]!=0){col=ry+1; found=true; break;}
            });
            if(!found) continue;
            int feetY = kMinY + col;
            // need two air blocks above ground
            std::uint16_t a1 = ctx.world->getBlock(ix, feetY, iz);
            std::uint16_t a2 = ctx.world->getBlock(ix, feetY+1, iz);
            std::uint16_t below = ctx.world->getBlock(ix, feetY-1, iz);
            if (a1==0 && a2==0 && below!=0) {
                m.x = ix + 0.5; m.z = iz + 0.5; m.y = feetY + 1.0;
                m.lastTeleportTick = now;
                if (ctx.srv) ctx.srv->broadcastSound("minecraft:entity.enderman.teleport", m.x,m.y,m.z,1.f,1.f,"hostile");
                return BTStatus::Success;
            }
        }
        return BTStatus::Failure;
    }
};

class PickupBlockAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t) override {
        if (m.carriedBlock !=0) return BTStatus::Failure;
        if (!ctx.world) return BTStatus::Failure;
        int bx=(int)std::floor(m.x), by=(int)std::floor(m.y), bz=(int)std::floor(m.z);
        // pick block directly below or at feet?
        std::uint16_t st = ctx.world->getBlock(bx, by-1, bz);
        if (st==0) st = ctx.world->getBlock(bx, by, bz);
        if (st==0) return BTStatus::Failure;
        auto* def = gen::blockByState(st);
        if (!def) return BTStatus::Failure;
        // don't pickup bedrock/obsidian
        std::string_view n = def->name;
        if (n=="minecraft:bedrock"||n=="minecraft:obsidian") return BTStatus::Failure;
        m.carriedBlock = st;
        ctx.world->setBlock(bx, by-1, bz, 0);
        if (ctx.srv) ctx.srv->broadcastBlockChange(bx, by-1, bz, 0);
        return BTStatus::Success;
    }
};

class StareAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t) override {
        Player* p = ctx.nearestPlayer;
        if (!p) return BTStatus::Failure;
        double dx = p->x - m.x, dz = p->z - m.z;
        m.yaw = (float)(std::atan2(dz,dx)*180.0/3.1415926535 - 90.0);
        return BTStatus::Success;
    }
};

class WitherSkullAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override {
        if (m.kind != MobKind::Wither) return BTStatus::Failure;
        if (m.witherSkullCooldown > now) return BTStatus::Failure;
        Player* t = ctx.nearestPlayer;
        if (!t) return BTStatus::Failure;
        double dx = t->x - m.x, dy = (t->y+1.0) - (m.y+1.5), dz = t->z - m.z;
        double d = std::sqrt(dx*dx+dz*dz);
        if (d>32) return BTStatus::Failure;
        double inv = 1.0/ (d+1e-6);
        double vx = dx*inv*1.1, vz = dz*inv*1.1, vy = dy*inv*0.6 + 0.2;
        if (ctx.srv) ctx.srv->spawnProjectile(ProjectileKind::WitherSkull, m.x, m.y+1.5, m.z, vx, vy, vz, m.entityId, false);
        m.witherSkullCooldown = (int)(now + 40 + rand()%40);
        return BTStatus::Success;
    }
};

enum class DragonPhase { Circling, ApproachingPerch, Perching, BreathAttack, Takeoff };

class DragonBreathAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override {
        if (m.kind != MobKind::EnderDragon) return BTStatus::Failure;
        // simple phase machine
        if (m.dragonPhase == 0 && now > m.dragonPhaseUntil) {
            // transition to perch randomly 10%
            if (rand()%100 < 12) { m.dragonPhase = 1; m.dragonPhaseUntil = now + 40; }
        }
        if (m.dragonPhase == 1) { // approaching center (0,0)
            double dx=-m.x, dz=-m.z;
            double d=std::sqrt(dx*dx+dz*dz);
            if (d<4) { m.dragonPhase=2; m.dragonPhaseUntil = now + 80; }
            else {
                m.x += dx/d * 0.18; m.z += dz/d * 0.18;
                m.y = 65; // perch height
                return BTStatus::Running;
            }
        }
        if (m.dragonPhase == 2) { // perching breath
            if (now % 20 == 0 && ctx.srv) {
                ctx.srv->spawnProjectile(ProjectileKind::DragonFireball, m.x, m.y, m.z, (rand()/(double)RAND_MAX-0.5)*0.6, -0.3, (rand()/(double)RAND_MAX-0.5)*0.6, m.entityId, false);
                ctx.srv->broadcastSound("minecraft:entity.ender_dragon.shoot", m.x,m.y,m.z,2.f,1.f,"hostile");
            }
            if (now > m.dragonPhaseUntil) { m.dragonPhase=3; m.dragonPhaseUntil=now+30; }
            return BTStatus::Running;
        }
        if (m.dragonPhase == 3) { // takeoff back to circling
            double ang = now * 0.04;
            double rx = std::cos(ang)*32, rz = std::sin(ang)*32;
            double dx=rx-m.x, dz=rz-m.z;
            m.x += dx*0.08; m.z += dz*0.08; m.y += (70-m.y)*0.05;
            if (now > m.dragonPhaseUntil) { m.dragonPhase=0; m.dragonPhaseUntil=now+120+rand()%120; }
            return BTStatus::Running;
        }
        // circling default
        double ang = now * 0.03;
        double rx = std::cos(ang)*28, rz = std::sin(ang)*28;
        double dx=rx - m.x, dz=rz - m.z;
        m.x += dx*0.04; m.z += dz*0.04; m.y += (68 - m.y)*0.02;
        m.yaw = (float)(std::atan2(dz,dx)*180/3.14159 -90);
        if (rand()%80==0 && ctx.srv) {
            // occasional breath while circling
            ctx.srv->broadcastSound("minecraft:entity.ender_dragon.flap", m.x,m.y,m.z,1.f,1.f,"hostile");
        }
        return BTStatus::Running;
    }
};

class BreedAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override {
        if (!m.inLove || now > m.loveUntilTick) return BTStatus::Failure;
        if (MobEntity::isBaby(m)) return BTStatus::Failure;
        // need to find partner - delegate to server
        if (!ctx.srv) return BTStatus::Failure;
        // check cooldown
        if (now < m.breedCooldownUntil) return BTStatus::Failure;
        // throttle: only attempt every 30 ticks after love start
        if (now < m.loveUntilTick - 30*20 + 30) return BTStatus::Running; // wait
        auto partner = ctx.srv->findLovePartner(m);
        if (!partner) return BTStatus::Running; // keep waiting
        // spawn baby
        double bx=(m.x+partner->x)/2.0, bz=(m.z+partner->z)/2.0;
        auto baby = std::make_shared<MobEntity>();
        baby->entityId = ctx.srv->nextEntityId();
        baby->kind = m.kind;
        baby->health = mobStats(m.kind).maxHealth;
        baby->age = -60*20;
        baby->x = bx; baby->y = m.y; baby->z = bz;
        ctx.srv->mobsForTest().push_back(baby);
        ctx.srv->broadcastMobSpawn(*baby);
        m.inLove=false; partner->inLove=false;
        m.breedCooldownUntil = now + 60*20;
        partner->breedCooldownUntil = now + 60*20;
        // hearts
        WriteBuffer st; st.i32(m.entityId); st.i8(18);
        // broadcast via server: use broadcast? call srv broadcast?
        // We'll just return success
        return BTStatus::Success;
    }
};

class TradeAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t) override {
        if (m.kind != MobKind::Villager) return BTStatus::Failure;
        // simply ensure villager has trades; actual trade handling is via Session::onUseEntity
        // This action makes villager look at player when in trade range
        if (!ctx.nearestPlayer) return BTStatus::Failure;
        double dx=ctx.nearestPlayer->x - m.x, dz=ctx.nearestPlayer->z - m.z;
        if (dx*dx+dz*dz > 36) return BTStatus::Failure;
        m.yaw = (float)(std::atan2(dz,dx)*180/3.14159 -90);
        return BTStatus::Success;
    }
};

// generic wander fallback
class WanderAction : public BehaviorNode {
public:
    BTStatus tick(MobEntity& m, AiContext& ctx, std::int64_t now) override {
        // delegate to existing wander logic: set target randomly and move
        if (!m.hasTarget) {
            double ang = (rand()/(double)RAND_MAX)*6.28318;
            double dist = 4 + (rand()%8);
            m.tx = m.x + std::cos(ang)*dist;
            m.tz = m.z + std::sin(ang)*dist;
            m.hasTarget=true;
            m.nextWanderAt = now + 3000 + rand()%4000;
        }
        double dx=m.tx-m.x, dz=m.tz-m.z;
        double d=std::sqrt(dx*dx+dz*dz);
        if (d<0.6 || now > m.nextWanderAt) { m.hasTarget=false; return BTStatus::Success; }
        m.yaw = (float)(std::atan2(dz,dx)*180/3.14159 -90);
        m.x += dx/d * 0.05;
        m.z += dz/d * 0.05;
        if (ctx.world && ctx.srv) {
            ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
            int col=4;
            ctx.world->withChunk((int)m.x>>4,(int)m.z>>4,[&](const Chunk& c){
                for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, (int)m.z&15, (int)m.x&15)]!=0){col=ry+1;break;}
            });
            m.y = kMinY + col + 1.0;
        }
        return BTStatus::Running;
    }
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
