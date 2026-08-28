// AI goal/sensor implementations. Movement uses the A* pathfinder when the
// target is far and direct steering when close (vanilla hybrid behaviour).
#include "AiBrain.hpp"
#include "BehaviorTree.hpp"
#include "GameServer.hpp"

namespace cppfm {

namespace {
bool stepAlongPath(MobEntity& m, AiContext& ctx, double speed) {
    if (ctx.pathIdx >= ctx.path.size()) return false;
    const auto& node = ctx.path[ctx.pathIdx];
    const double tx = node.x + 0.5, tz = node.z + 0.5;
    double dx = tx - m.x, dz = tz - m.z;
    const double d = std::sqrt(dx * dx + dz * dz);
    if (d < 0.35) { ++ctx.pathIdx; return ctx.pathIdx < ctx.path.size(); }
    m.yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / 3.14159 - 90.0);
    m.x += dx / d * speed;
    m.z += dz / d * speed;
    return true;
}
void groundSnap(GameServer& srv, MobEntity& m) {
    World& w = srv.world();
    w.generateChunkIfMissing(static_cast<std::int32_t>(m.x) >> 4,
                             static_cast<std::int32_t>(m.z) >> 4);
    int col = 4;
    w.withChunk(static_cast<std::int32_t>(m.x) >> 4,
                static_cast<std::int32_t>(m.z) >> 4,
                [&](const Chunk& c) {
                    for (int ry = kSectionsPerChunk * 16 - 1; ry >= 0; --ry)
                        if (c.blocks[Chunk::index(ry >> 4, ry & 15,
                                                  static_cast<std::int32_t>(m.z) & 15,
                                                  static_cast<std::int32_t>(m.x) & 15)] != 0) {
                            col = ry + 1; break;
                        }
                });
    m.y = kMinY + col + 1.0;
}
} // namespace

Brain::Brain() {
    goals_.push_back(std::make_unique<PanicGoal>());
    goals_.push_back(std::make_unique<BreedGoal>());
    goals_.push_back(std::make_unique<MeleeAttackGoal>());
    goals_.push_back(std::make_unique<RangedAttackGoal>());
    goals_.push_back(std::make_unique<TemptGoal>());
    goals_.push_back(std::make_unique<WanderAroundGoal>());
    goals_.push_back(std::make_unique<LookAtPlayerGoal>());
}

void NearestPlayerSensor::update(MobEntity& m, AiContext& ctx) {
    if (!ctx.srv) return;
    ctx.resetPerception();
    for (auto& p : ctx.srv->playersSnapshot()) {
        if (!p->inPlay || p->dead || p->gamemode == 1 || p->gamemode == 3)
            continue;
        const double dx = p->x - m.x, dz = p->z - m.z;
        const double d2 = dx * dx + dz * dz;
        if (d2 < ctx.nearestPlayerDist2) {
            ctx.nearestPlayerDist2 = d2;
            ctx.nearestPlayer = p.get();
        }
        // temptation: player holds breeding item for this mob kind
        const auto foodId = MobEntity::breedingItemFor(m.kind);
        if (foodId && d2 < 12 * 12 && p->heldSlot >= 0 && p->heldSlot < 9 &&
            p->inv[36 + p->heldSlot].itemId == foodId)
            ctx.temptingPlayer = p.get();
    }
}

bool PanicGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (!ctx.srv) return false;
    const std::int64_t now = ctx.srv->tickNoForTest();
    return m.health > 0 && now - ctx.lastHurtTick < 100;
}

bool PanicGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    // run away from nearest player at 1.6x speed
    if (!ctx.nearestPlayer) return false;
    const double dx = m.x - ctx.nearestPlayer->x;
    const double dz = m.z - ctx.nearestPlayer->z;
    const double d = std::sqrt(dx * dx + dz * dz) + 1e-6;
    m.yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / 3.14159 - 90.0);
    m.x += dx / d * 0.14;
    m.z += dz / d * 0.14;
    groundSnap(*ctx.srv, m);
    return true;
}

bool MeleeAttackGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    Player* tgt = ctx.nearestPlayer;
    if (!tgt) return false;
    const double dx = tgt->x - m.x, dz = tgt->z - m.z;
    const double dist = std::sqrt(dx * dx + dz * dz);
    if (dist > 24) return false;
    if (dist < 1.9) {
        if (now % 20 == 0) ctx.srv->mobAttackPlayer(m, *tgt);
        return true;
    }
    // pathfind occasionally, follow path otherwise
    if (ctx.pathIdx >= ctx.path.size() ||
        std::abs(ctx.path.back().x - static_cast<std::int32_t>(tgt->x)) > 3 ||
        std::abs(ctx.path.back().z - static_cast<std::int32_t>(tgt->z)) > 3) {
        ai::Pathfinder pf(*ctx.world);
        auto res = pf.find(static_cast<std::int32_t>(std::floor(m.x)),
                           static_cast<std::int32_t>(std::floor(m.y)),
                           static_cast<std::int32_t>(std::floor(m.z)),
                           static_cast<std::int32_t>(std::floor(tgt->x)),
                           static_cast<std::int32_t>(std::floor(tgt->y)),
                           static_cast<std::int32_t>(std::floor(tgt->z)), 800);
        ctx.path = std::move(res.points);
        ctx.pathIdx = res.found ? 1 : 0;
        if (!res.found) {
            // fall back to straight steering
            m.tx = tgt->x; m.tz = tgt->z; m.hasTarget = true;
            const double inv = 1.0 / dist;
            m.x += dx * inv * 0.09;
            m.z += dz * inv * 0.09;
            groundSnap(*ctx.srv, m);
            return true;
        }
    }
    stepAlongPath(m, ctx, 0.10);
    groundSnap(*ctx.srv, m);
    return true;
}

bool TemptGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    Player* t = ctx.temptingPlayer;
    if (!t) return false;
    const double dx = t->x - m.x, dz = t->z - m.z;
    const double d = std::sqrt(dx * dx + dz * dz);
    if (d < 2.5) { m.hasTarget = false; return true; }   // sit near player
    const double inv = 1.0 / (d + 1e-6);
    m.yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / 3.14159 - 90.0);
    m.x += dx * inv * 0.07;
    m.z += dz * inv * 0.07;
    groundSnap(*ctx.srv, m);
    return true;
}

bool BreedGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (!m.inLove || ctx.srv==nullptr) return false;
    if (ctx.srv->tickNoForTest() < m.breedCooldownUntil) return false;
    if (MobEntity::isBaby(m)) return false;
    // plan14 §3: require love partner within 8 blocks sameKind & inLove
    return ctx.srv->findLovePartner(m) != nullptr;
}

bool BreedGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    GameServer& srv = *ctx.srv;
    if (!m.inLove || now < m.breedCooldownUntil) return false;
    if (MobEntity::isBaby(m)) { m.inLove=false; return false; }
    // wait 30 ticks after entering love (love 600t -> hearts)
    if (now < m.loveUntilTick - 30*20 + 30) return true;
    auto partner = srv.findLovePartner(m);
    if (!partner) return true; // keep waiting for partner to approach
    double dx = partner->x - m.x, dz = partner->z - m.z;
    double d2 = dx*dx + dz*dz;
    if (d2 > 4.0) { // >2 blocks: move towards partner (plan14 §3 moveTo)
        double d = std::sqrt(d2) + 1e-6;
        m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159 -90.0);
        m.x += dx/d * 0.09;
        m.z += dz/d * 0.09;
        // groundSnap
        World& w = srv.world();
        w.generateChunkIfMissing(static_cast<std::int32_t>(m.x)>>4, static_cast<std::int32_t>(m.z)>>4);
        int col=4;
        w.withChunk(static_cast<std::int32_t>(m.x)>>4, static_cast<std::int32_t>(m.z)>>4,[&](const Chunk& c){
            for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, static_cast<std::int32_t>(m.z)&15, static_cast<std::int32_t>(m.x)&15)]!=0){col=ry+1;break;}
        });
        m.y = kMinY + col + 1.0;
        partner->x += -dx/d * 0.04; partner->z += -dz/d * 0.04; // partner slowly approaches
        return true;
    }
    // breed: spawn baby at mid pos (plan14 §3: age -24000, love reset, cooldown 6000, xp 1-7)
    const double bx = (m.x + partner->x) / 2.0;
    const double bz = (m.z + partner->z) / 2.0;
    auto baby = std::make_shared<MobEntity>();
    baby->entityId = ctx.srv->nextEntityId();
    baby->kind = m.kind;
    baby->health = mobStats(m.kind).maxHealth;
    baby->age = -24000; // 20 min vanilla
    baby->x = bx; baby->y = m.y; baby->z = bz;
    ctx.srv->mobsForTest().push_back(baby);
    ctx.srv->broadcastMobSpawn(*baby);
    // xp 1-7
    ctx.srv->spawnXpOrbs(bx, m.y+0.5, bz, 1 + (rand()%7), nullptr);
    // reset love and set cooldown 6000t (5 min) simplified to 6000 = 300*20? use 6000
    m.inLove = false;
    partner->inLove = false;
    m.breedCooldownUntil = now + 6000;
    partner->breedCooldownUntil = now + 6000;
    // hearts already via EntityEvent 18 in tryBreedFeed, but also broadcast here
    {
        WriteBuffer st; st.i32(m.entityId); st.i8(18);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityEvent, st);
        WriteBuffer st2; st2.i32(partner->entityId); st2.i8(18);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityEvent, st2);
    }
    return false;
}

bool WanderAroundGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    GameServer& srv = *ctx.srv;
    if (!m.hasTarget) {
        const double ang = (rand() / double(RAND_MAX)) * 6.28318;
        const double dist = 4 + (rand() % 8);
        m.tx = m.x + std::cos(ang) * dist;
        m.tz = m.z + std::sin(ang) * dist;
        m.hasTarget = true;
        m.nextWanderAt = now + 3000 + rand() % 4000;
        // build a short path
        ai::Pathfinder pf(*ctx.world);
        auto res = pf.find(static_cast<std::int32_t>(std::floor(m.x)),
                           static_cast<std::int32_t>(std::floor(m.y)),
                           static_cast<std::int32_t>(std::floor(m.z)),
                           static_cast<std::int32_t>(std::floor(m.tx)),
                           static_cast<std::int32_t>(std::floor(m.y)),
                           static_cast<std::int32_t>(std::floor(m.tz)), 300);
        ctx.path = std::move(res.points);
        ctx.pathIdx = res.found ? 1 : 0;
    }
    bool moving = stepAlongPath(m, ctx, 0.05);
    if (!moving) {
        // straight-line fallback toward wander target
        const double dx = m.tx - m.x, dz = m.tz - m.z;
        const double d = std::sqrt(dx * dx + dz * dz);
        if (d < 0.6 || now > m.nextWanderAt) {
            m.hasTarget = false;
            return false;
        }
        m.yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / 3.14159 - 90.0);
        m.x += dx / d * 0.05;
        m.z += dz / d * 0.05;
    }
    groundSnap(srv, m);
    return true;
}

bool LookAtPlayerGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!ctx.nearestPlayer) return false;
    const double dx = ctx.nearestPlayer->x - m.x;
    const double dz = ctx.nearestPlayer->z - m.z;
    m.yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / 3.14159 - 90.0);
    return ctx.nearestPlayerDist2 < 8 * 8;
}


// -------------------------------------------------------- ranged attacks --

bool RangedAttackGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!RangedAttackGoal::isRangedKind(m.kind)) return false;
    Player* tgt = ctx.nearestPlayer;
    if (!tgt) return false;
    const double dx = tgt->x - m.x, dz = tgt->z - m.z;
    const double dy = (tgt->y + 1.0) - (m.y + 1.6);
    const double dist = std::sqrt(dx * dx + dz * dz);
    if (dist < 5) return false;                       // melee goal takes over
    if (dist > 16) { m.hasTarget = false; return true; }
    // face the target
    m.yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / 3.14159 - 90.0);
    // fire every 2 s with a short warm-up
    if (m.nextWanderAt == 0) m.nextWanderAt = now + 20;
    if (now >= m.nextWanderAt) {
        m.nextWanderAt = now + 40;
        const double inv = 1.0 / dist;
        double vx = dx * inv * 1.4;
        double vz = dz * inv * 1.4;
        double vy = dy * inv + dist * 0.04;
        ctx.srv->spawnProjectile(ProjectileKind::Arrow, m.x, m.y + 1.6, m.z,
                                 vx, vy, vz, m.entityId, false);
        ctx.srv->broadcastSound("minecraft:entity.arrow.shoot",
                                m.x, m.y, m.z, 1.f, 1.f, "hostile");
    }
    // hold ground while shooting
    return true;
}

// -------------------------------------------------------- ranged attacks --

Brain::~Brain() = default;
void Brain::setBehaviorTree(std::unique_ptr<BehaviorTree> t) { behaviorTree_ = std::move(t); }
bool Brain::hasBehaviorTree() const { return behaviorTree_ != nullptr; }
void Brain::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    NearestPlayerSensor::update(m, ctx);
    // BehaviorTree evaluation (plan6 item 29)
    if (behaviorTree_) {
        BTStatus s = behaviorTree_->tick(m, ctx, now);
        if (s == BTStatus::Running || s == BTStatus::Success) {
            // tree handled this tick; still allow fallback if tree returned Failure
            if (s == BTStatus::Running) return;
            // if Success, we consider tree consumed tick
            return;
        }
        // Failure -> fall through to Goal logic
    }
    Goal* chosen = nullptr;
    for (auto& g : goals_) {
        if (g->shouldStart(m, ctx)) { chosen = g.get(); break; }
        if (g.get() == active_ && running_) break;   // keep active unless preempted
    }
    if (!chosen && running_) chosen = active_;
    if (chosen != active_) {
        if (active_) active_->stop(m, ctx);
        active_ = chosen;
        running_ = false;
        if (active_) { active_->start(m, ctx); running_ = true; }
    }
    if (active_) {
        running_ = active_->tick(m, ctx, now);
        if (!running_) { active_->stop(m, ctx); active_ = nullptr; }
    }
}

} // namespace cppfm
