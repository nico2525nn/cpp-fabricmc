// AI goal/sensor implementations. Movement uses the A* pathfinder when the
// target is far and direct steering when close (vanilla hybrid behaviour).
#include "AiBrain.hpp"
#include "BehaviorTree.hpp"
#include "GameServer.hpp"
#include "MetadataTypes.hpp"
#include "../proto/Ids.hpp"
#include "../generated/BlockStates.hpp"

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
    goals_.push_back(std::make_unique<CreakingGoal>());
    goals_.push_back(std::make_unique<SwellGoal>());
    goals_.push_back(std::make_unique<ArmadilloRollUpGoal>());
    goals_.push_back(std::make_unique<PanicGoal>());
    goals_.push_back(std::make_unique<IronGolemDefendGoal>());
    goals_.push_back(std::make_unique<WitchPotionThrowGoal>());
    goals_.push_back(std::make_unique<RavagerRoarGoal>());
    goals_.push_back(std::make_unique<EvokerFangGoal>());
    goals_.push_back(std::make_unique<WolfAngerGoal>());
    goals_.push_back(std::make_unique<FleeSunGoal>());
    goals_.push_back(std::make_unique<LeapAtTargetGoal>());
    goals_.push_back(std::make_unique<BreezeJumpGoal>());
    goals_.push_back(std::make_unique<DrownedTridentGoal>());
    goals_.push_back(std::make_unique<PiglinBarterGoal>());
    goals_.push_back(std::make_unique<CatScareGoal>());
    goals_.push_back(std::make_unique<FoxPounceGoal>());
    goals_.push_back(std::make_unique<BreedGoal>());
    goals_.push_back(std::make_unique<BeePollinateGoal>());
    goals_.push_back(std::make_unique<PandaRollGoal>());
    goals_.push_back(std::make_unique<DolphinPlayGoal>());
    goals_.push_back(std::make_unique<BreezeWindChargeGoal>());
    goals_.push_back(std::make_unique<MeleeAttackGoal>());
    goals_.push_back(std::make_unique<RangedAttackGoal>());
    goals_.push_back(std::make_unique<AvoidEntityGoal>());
    goals_.push_back(std::make_unique<TemptGoal>());
    goals_.push_back(std::make_unique<VillagerScheduleGoal>());
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
    // plan38 B-13: bred_animals trigger for nearest player
    {
        auto nearby = ctx.srv->playersSnapshot();
        Player* best = nullptr; double bestDist=64;
        for (auto &pp: nearby) if (pp->inPlay) {
            double dx=pp->x-bx, dz=pp->z-bz;
            double d2=dx*dx+dz*dz;
            if (d2<bestDist*bestDist) { bestDist=std::sqrt(d2); best=pp.get(); }
        }
        if (best) ctx.srv->onBredAnimals(best);
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

// plan29 §3 Creaking freeze when looked at (60° yaw/pitch + raycast), else chase 0.30 + attack 2.5/3/4.5
static float wrapDegrees(float v) {
    while (v <= -180) v += 360;
    while (v > 180) v -= 360;
    return v;
}
static bool raycastObstructed(World* w, double x0,double y0,double z0, double x1,double y1,double z1) {
    if (!w) return false;
    double dx=x1-x0, dy=y1-y0, dz=z1-z0;
    double dist = std::sqrt(dx*dx+dy*dy+dz*dz);
    int steps = std::max(1, (int)(dist*4));
    for (int i=1;i<steps;++i) {
        double t = (double)i/steps;
        double x = x0 + dx*t, y = y0 + dy*t, z = z0 + dz*t;
        int ix=(int)std::floor(x), iy=(int)std::floor(y), iz=(int)std::floor(z);
        uint16_t st = w->getBlock(ix,iy,iz);
        if (st==0) continue;
        auto* bd = gen::blockByState(st);
        if (!bd) continue;
        if (!bd->transparent) return true;
    }
    return false;
}
static bool isPlayerLookingAtCreaking(Player* p, MobEntity& cr, World* w) {
    if (!p) return false;
    if (p->gamemode==1 || p->gamemode==3) return false;
    for (int i=5;i<=8;++i) if (i>=0 && i < (int)p->inv.size() && !p->inv[i].empty()) {
        if (p->inv[i].name()=="minecraft:carved_pumpkin") return false;
    }
    double dx = cr.x - p->x;
    double dy = (cr.y+0.9) - (p->y+1.62);
    double dz = cr.z - p->z;
    double dist = std::sqrt(dx*dx+dy*dy+dz*dz);
    if (dist > 32 || dist < 0.1) return false;
    double yawToMob = std::atan2(dz,dx)*180.0/3.1415926535 - 90.0;
    double pitchToMob = -std::asin(dy/dist)*180.0/3.1415926535;
    double dYaw = std::abs(wrapDegrees((float)(yawToMob - p->yaw)));
    double dPitch = std::abs((float)(pitchToMob - p->pitch));
    if (dYaw > 60 || dPitch > 60) return false;
    if (raycastObstructed(w, p->x, p->y+1.62, p->z, cr.x, cr.y+0.9, cr.z)) return false;
    return true;
}
bool CreakingGoal::shouldStart(MobEntity& m, AiContext&) {
    return m.kind == MobKind::Creaking;
}
bool CreakingGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Creaking) return false;
    if (!ctx.srv || !ctx.world) return false;
    // check any player looking => frozen
    bool frozen = false;
    for (auto& pp : ctx.srv->playersSnapshot()) {
        if (!pp->inPlay || pp->dead) continue;
        if (isPlayerLookingAtCreaking(pp.get(), m, ctx.world)) { frozen = true; break; }
    }
    m.creakingFrozen = frozen;
    m.creakingAlerted = (ctx.nearestPlayer && ctx.nearestPlayerDist2 < 12*12);
    if (frozen) {
        // immobile, cannot be pushed/knocked; also do not attack
        return true;
    }
    Player* tgt = ctx.nearestPlayer;
    if (!tgt) return false;
    double dx = tgt->x - m.x, dz = tgt->z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d < 1.9) {
        if (now % 20 == 0) ctx.srv->mobAttackPlayer(m, *tgt);
        return true;
    }
    if (d > 24) return false;
    // pathfind occasionally
    if (ctx.pathIdx >= ctx.path.size() ||
        std::abs(ctx.path.back().x - (int)std::floor(tgt->x)) > 3 ||
        std::abs(ctx.path.back().z - (int)std::floor(tgt->z)) > 3) {
        ai::Pathfinder pf(*ctx.world);
        auto res = pf.find((int)std::floor(m.x),(int)std::floor(m.y),(int)std::floor(m.z),
                           (int)std::floor(tgt->x),(int)std::floor(tgt->y),(int)std::floor(tgt->z),800);
        ctx.path = std::move(res.points);
        ctx.pathIdx = res.found ? 1 : 0;
        if (!res.found) {
            m.yaw = (float)(std::atan2(dz,dx)*180.0/3.1415926535 - 90.0);
            m.x += dx/d * 0.14;
            m.z += dz/d * 0.14;
            World& w = *ctx.world;
            w.generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
            int col=4;
            w.withChunk((int)m.x>>4,(int)m.z>>4,[&](const Chunk& c){
                for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, (int)m.z&15, (int)m.x&15)]!=0){col=ry+1;break;}
            });
            m.y = kMinY + col + 1.0;
            return true;
        }
    }
    // step along path at creaking speed 0.14 (approx 0.3 scaled)
    if (ctx.pathIdx < ctx.path.size()) {
        const auto& node = ctx.path[ctx.pathIdx];
        double tx = node.x+0.5, tz=node.z+0.5;
        double pdx=tx-m.x, pdz=tz-m.z;
        double pd = std::sqrt(pdx*pdx+pdz*pdz);
        if (pd < 0.35) { ++ctx.pathIdx; }
        else {
            m.yaw = (float)(std::atan2(pdz,pdx)*180.0/3.1415926535 - 90.0);
            m.x += pdx/pd * 0.14;
            m.z += pdz/pd * 0.14;
        }
    } else {
        m.yaw = (float)(std::atan2(dz,dx)*180.0/3.1415926535 - 90.0);
        m.x += dx/d * 0.10;
        m.z += dz/d * 0.10;
    }
    World& w = *ctx.world;
    w.generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    int col=4;
    w.withChunk((int)m.x>>4,(int)m.z>>4,[&](const Chunk& c){
        for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, (int)m.z&15, (int)m.x&15)]!=0){col=ry+1;break;}
    });
    m.y = kMinY + col + 1.0;
    return true;
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

// ------------------------------------------------- plan34 §2-3 new goals --

bool SwellGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Creeper || m.dead) return false;
    if (!ctx.nearestPlayer) return m.creeperIgnited;
    double dx = ctx.nearestPlayer->x - m.x, dz = ctx.nearestPlayer->z - m.z;
    double d2 = dx*dx+dz*dz;
    if (m.creeperIgnited) return true;
    return d2 < 9; // 3 blocks
}
bool SwellGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Creeper || m.dead) return false;
    if (!ctx.nearestPlayer) {
        // if ignited but player left far, let GameServer_tick handle defuse; hold still until fuse handled
        return m.creeperIgnited;
    }
    double dx = ctx.nearestPlayer->x - m.x, dz = ctx.nearestPlayer->z - m.z;
    double d2 = dx*dx+dz*dz;
    // ignite already handled in GameServer_tick mobsTick; just hold position while swelling
    if (m.creeperIgnited) {
        if (now - m.creeperFuseStart >= MobEntity::CREEPER_FUSE_TICKS) return false;
        return true; // stay still during swell
    }
    if (d2 < 9 && ctx.srv) {
        // trigger ignite here too for Goal-driven path (server tick also does it)
        m.creeperIgnited = true;
        m.creeperFuseStart = now;
        WriteBuffer md; md.varint(m.entityId); meta::writeMetaBool(md, 16, true); md.u8(255);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
        ctx.srv->broadcastSound("minecraft:entity.creeper.primed", m.x, m.y, m.z, 1.f, 1.f, "hostile");
    }
    return m.creeperIgnited;
}

bool AvoidEntityGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (!ctx.nearestPlayer) return false;
    // differentiate per mob: creeper avoids cat/ocelot, skeleton avoids wolf, piglin avoids zoglin etc.
    // simplified: any of those kinds use same player-distance check; for non-listed, still flee if close 4
    if (m.kind==MobKind::Creeper || m.kind==MobKind::Skeleton || m.kind==MobKind::Piglin || m.kind==MobKind::Spider) {
        return ctx.nearestPlayerDist2 < dist2_;
    }
    return false;
}
bool AvoidEntityGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    if (!ctx.srv || !ctx.nearestPlayer) return false;
    if (ctx.nearestPlayerDist2 > dist2_) return false;
    double dx = m.x - ctx.nearestPlayer->x, dz = m.z - ctx.nearestPlayer->z;
    double d = std::sqrt(dx*dx+dz*dz)+1e-6;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180/3.14159 -90);
    m.x += dx/d * 0.12;
    m.z += dz/d * 0.12;
    if (ctx.srv && ctx.world) {
        ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
        int col=4;
        ctx.world->withChunk((int)m.x>>4,(int)m.z>>4,[&](const Chunk& c){
            for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, (int)m.z&15, (int)m.x&15)]!=0){col=ry+1;break;}
        });
        m.y = kMinY + col + 1.0;
    }
    return true;
}

bool FleeSunGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (!ctx.srv || !ctx.world) return false;
    if (m.kind!=MobKind::Skeleton && m.kind!=MobKind::Zombie && m.kind!=MobKind::Stray && m.kind!=MobKind::Husk && m.kind!=MobKind::Drowned) return false;
    if (ctx.srv->isNight()) return false;
    // check sky light >=14 at mob feet
    ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    uint8_t sky = ctx.world->getSkyLight((int)m.x,(int)m.y,(int)m.z);
    return sky >= 14;
}
bool FleeSunGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    if (!ctx.srv || !ctx.world) return false;
    if (ctx.srv->isNight()) return false;
    // seek shade: move opposite to player or random if no player
    double dx=0, dz=0;
    if (ctx.nearestPlayer) { dx = m.x - ctx.nearestPlayer->x; dz = m.z - ctx.nearestPlayer->z; }
    else { dx = (rand()/(double)RAND_MAX-0.5)*2; dz = (rand()/(double)RAND_MAX-0.5)*2; }
    double d = std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x += dx/d * 0.13; m.z += dz/d * 0.13;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180/3.14159 -90);
    ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    int col=4;
    ctx.world->withChunk((int)m.x>>4,(int)m.z>>4,[&](const Chunk& c){
        for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, (int)m.z&15, (int)m.x&15)]!=0){col=ry+1;break;}
    });
    m.y = kMinY + col + 1.0;
    return true;
}

bool LeapAtTargetGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind!=MobKind::Spider && m.kind!=MobKind::CaveSpider && m.kind!=MobKind::Phantom) return false;
    if (!ctx.nearestPlayer) return false;
    double dx=ctx.nearestPlayer->x - m.x, dz=ctx.nearestPlayer->z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    return d >= 2.0 && d <= 5.0;
}
bool LeapAtTargetGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    if (!ctx.srv || !ctx.nearestPlayer) return false;
    double dx=ctx.nearestPlayer->x - m.x, dz=ctx.nearestPlayer->z - m.z;
    double d = std::sqrt(dx*dx+dz*dz)+1e-6;
    double vx = dx/d * 0.42, vz = dz/d * 0.42;
    double vy = 0.38;
    // apply leap
    m.x += vx; m.z += vz; m.y += vy;
    // gravity will be handled by tick loop groundSnap next tick; clamp y
    if (m.y > kMinY + 320) m.y = kMinY + 320;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180/3.14159 -90);
    if (ctx.srv) {
        WriteBuffer vel; vel.varint(m.entityId); vel.i16((int16_t)(vx*8000)); vel.i16((int16_t)(vy*8000)); vel.i16((int16_t)(vz*8000));
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vel);
        ctx.srv->broadcastSound("minecraft:entity.spider.jump", m.x, m.y, m.z, 1.f, 1.f, "hostile");
    }
    if (ctx.world) {
        ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    }
    return true;
}

bool BreezeJumpGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Breeze) return false;
    if (m.breezeJumpCooldown > now) return false;
    Player* t = ctx.nearestPlayer;
    if (!t) return false;
    double dx=t->x - m.x, dz=t->z - m.z;
    double d=std::sqrt(dx*dx+dz*dz);
    if (d < 4) return false;
    double inv=1.0/(d+1e-6);
    // vanilla breeze jump 15h/5v, simplified to 0.7h + 0.45v scaled; pass via position delta + velocity
    double jx = dx*inv * 0.55;
    double jz = dz*inv * 0.55;
    // clamp lava jump vy=1 case: we just use 0.45 normally, but if in lava would be 0.12 – simplified keep 0.45
    double jy = 0.45;
    // avoid jumping too high if already high
    if (m.y > t->y + 6) jy = 0.15;
    m.x += jx * 2.2; m.z += jz * 2.2; m.y += jy * 3.0;
    m.breezeJumpCooldown = now + 40;
    m.breezeLastJumpTick = now;
    if (ctx.srv) {
        WriteBuffer vel; vel.varint(m.entityId); vel.i16((int16_t)(jx*8000*2)); vel.i16((int16_t)(jy*8000)); vel.i16((int16_t)(jz*8000*2));
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vel);
        ctx.srv->broadcastSound("minecraft:entity.breeze.jump", m.x, m.y, m.z, 1.f, 1.f, "hostile");
    }
    if (ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    return true;
}
bool BreezeWindChargeGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Breeze) return false;
    if (m.breezeWindChargeCooldown > now) return false;
    Player* t = ctx.nearestPlayer;
    if (!t) return false;
    double dx=t->x - m.x, dy=(t->y+1.0)-(m.y+1.2), dz=t->z - m.z;
    double d=std::sqrt(dx*dx+dz*dz);
    if (d>16) return false;
    double inv=1.0/(d+1e-6);
    double vx=dx*inv*1.15, vz=dz*inv*1.15, vy=dy*inv*0.2 + 0.12;
    if (ctx.srv) {
        // use Fireball-like wind_charge; entity type BreezeWindCharge visual via typeId lookup inside spawnProjectile
        // spawn as BreezeWindCharge kind for correct entity type (fallback uses Fireball if mapping fails)
        ctx.srv->spawnProjectile(ProjectileKind::BreezeWindCharge, m.x, m.y+1.2, m.z, vx, vy, vz, m.entityId, false);
        ctx.srv->broadcastSound("minecraft:entity.breeze.wind_burst", m.x, m.y, m.z, 1.f, 1.f, "hostile");
    }
    m.breezeWindChargeCooldown = now + 32;
    return true;
}

bool ArmadilloRollUpGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Armadillo) return false;
    // scanRate 5: only check every 5 ticks; danger flag from AiContext or pending until
    return ctx.dangerDetectedRecently || m.armadilloDangerDetectedUntil > 0;
}
bool ArmadilloRollUpGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Armadillo) return false;
    bool danger = ctx.dangerDetectedRecently || now < m.armadilloDangerDetectedUntil;
    // water check: immediate unroll if in water (simplified: y below sea or block water)
    if (ctx.world) {
        uint16_t st = ctx.world->getBlock((int)std::floor(m.x),(int)std::floor(m.y),(int)std::floor(m.z));
        auto* bd = gen::blockByState(st);
        if (bd && std::string(bd->name).find("water")!=std::string::npos) danger = false;
    }
    if (danger && !m.armadilloRolledUp) {
        m.armadilloRolledUp = true;
        m.armadilloRollUpUntil = now + 60;
        if (ctx.srv) {
            WriteBuffer md; md.varint(m.entityId); meta::writeMetaByte(md, 16, 1); md.u8(255);
            ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
            ctx.srv->broadcastSound("minecraft:entity.armadillo.roll", m.x, m.y, m.z, 1.f, 1.f, "neutral");
        }
        return true;
    }
    if (m.armadilloRolledUp) {
        if (now > m.armadilloRollUpUntil) {
            if (!danger) {
                m.armadilloRolledUp = false;
                if (ctx.srv) {
                    WriteBuffer md; md.varint(m.entityId); meta::writeMetaByte(md, 16, 0); md.u8(255);
                    ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
                }
                return false;
            } else {
                m.armadilloRollUpUntil = now + 20;
            }
        }
        return true;
    }
    return false;
}

// -------------------------------------------------------- plan36 30 species goals --
bool WitchPotionThrowGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Witch) return false;
    if (!ctx.nearestPlayer) return false;
    if (ctx.nearestPlayerDist2 > 16*16) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.witchPotionCooldown) return false;
    return true;
}
bool WitchPotionThrowGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Witch) return false;
    Player* t = ctx.nearestPlayer; if (!t) return false;
    double dx=t->x - m.x, dz=t->z - m.z; double d2=dx*dx+dz*dz; if (d2>256) return false;
    if (now < m.witchPotionCooldown) return false;
    double d=std::sqrt(d2)+1e-6;
    if (ctx.srv) {
        // heal if low health
        if (m.health < 13) {
            m.health = std::min(m.health+6.f, (double)mobStats(m.kind).maxHealth);
            ctx.srv->broadcastSound("minecraft:entity.witch.drink", m.x,m.y,m.z,1.f,1.f,"hostile");
            WriteBuffer md; md.varint(m.entityId); meta::writeMetaBool(md,16,true); md.u8(255);
            ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
        } else {
            double vx=dx/d*0.9, vz=dz/d*0.9;
            ctx.srv->spawnProjectile(ProjectileKind::Potion, m.x, m.y+1.6, m.z, vx, 0.12, vz, m.entityId, false);
            ctx.srv->broadcastSound("minecraft:entity.witch.throw", m.x,m.y,m.z,1.f,1.f,"hostile");
            WriteBuffer md; md.varint(m.entityId); meta::writeMetaBool(md,16,false); md.u8(255);
            ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
        }
    }
    m.witchPotionCooldown = now + 40 + (rand()%20);
    return true;
}
bool RavagerRoarGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Ravager) return false;
    if (!ctx.nearestPlayer) return false;
    if (ctx.nearestPlayerDist2 > 5*5) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.ravagerRoarCooldown) return false;
    return true;
}
bool RavagerRoarGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Ravager) return false;
    if (now < m.ravagerRoarCooldown) return false;
    Player* t = ctx.nearestPlayer; if (!t) return false;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d>5) return false;
    if (ctx.srv) {
        double vx=dx/d*0.4, vz=dz/d*0.4;
        WriteBuffer vel; vel.varint(t->entityId); vel.i16((int16_t)(vx*8000)); vel.i16((int16_t)(0.3*8000)); vel.i16((int16_t)(vz*8000));
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vel);
        ctx.srv->broadcastHurtAnimation(t->entityId, (float)(std::atan2(dz,dx)*180/3.14159));
        ctx.srv->broadcastSound("minecraft:entity.ravager.roar", m.x,m.y,m.z,1.f,1.f,"hostile");
    }
    m.ravagerRoarCooldown = now + 100;
    m.ravagerStunUntil = now + 10;
    return true;
}
bool IronGolemDefendGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::IronGolem) return false;
    if (!ctx.nearestPlayer) return false;
    // defend if any hostile within 12 or player attacked recently
    if (ctx.nearestPlayerDist2 < 12*12) return true;
    if (ctx.lastHurtTick >=0 && ctx.srv && ctx.srv->tickNoForTest() - ctx.lastHurtTick < 40) return true;
    return false;
}
bool IronGolemDefendGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::IronGolem) return false;
    if (now < m.ironGolemDefendCooldown) return false;
    Player* t = ctx.nearestPlayer; if (!t) return false;
    // attack nearest hostile mob if present, else just approach player
    std::shared_ptr<MobEntity> nearestHostile;
    double best=1e300;
    if (ctx.srv) {
        for (auto& mm : ctx.srv->mobsForTest()) if (MobEntity::isHostile(mm->kind) && !mm->dead) {
            double dx=mm->x - m.x, dz=mm->z - m.z; double d2=dx*dx+dz*dz; if (d2<best){best=d2; nearestHostile=mm;}
        }
    }
    if (nearestHostile && best < 12*12) {
        double dx=nearestHostile->x - m.x, dz=nearestHostile->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
        if (d < 2.2) {
            if (ctx.srv) { ctx.srv->applyDamageToMob(*nearestHostile, 8.f, "mob"); ctx.srv->broadcastSound("minecraft:entity.iron_golem.attack", m.x,m.y,m.z,1.f,1.f,"neutral");}
            m.ironGolemDefendCooldown = now + 20;
            return true;
        }
        m.x += dx/d*0.12; m.z += dz/d*0.12; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
        if (ctx.srv) ctx.srv->broadcastSound("minecraft:entity.iron_golem.step", m.x,m.y,m.z,0.5f,1.f,"neutral");
        return true;
    }
    return false;
}
bool BeePollinateGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Bee) return false;
    if (m.beeHasNectar) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.beePollenUntil) return false;
    return true;
}
bool BeePollinateGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Bee) return false;
    if (m.beeHasNectar) return false;
    // scan for flower every 20 ticks
    if (now % 20 != 0) return true;
    if (!ctx.world) return false;
    for (int dx=-8; dx<=8; ++dx) for (int dz=-8; dz<=8; ++dz) for (int dy=-2; dy<=2; ++dy){
        int bx=(int)std::floor(m.x)+dx, by=(int)std::floor(m.y)+dy, bz=(int)std::floor(m.z)+dz;
        uint16_t st=ctx.world->getBlock(bx,by,bz); if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        std::string n(bd->name);
        if (n.find("flower")!=std::string::npos || n=="minecraft:dandelion" || n=="minecraft:poppy") {
            double ddx=bx+0.5 - m.x, ddz=bz+0.5 - m.z; double d=std::sqrt(ddx*ddx+ddz*ddz)+1e-6;
            m.x += ddx/d*0.08; m.z += ddz/d*0.08; if(d<1.2){ m.beeHasNectar=true; m.beePollenUntil=now+400; if(ctx.srv){ WriteBuffer md; md.varint(m.entityId); meta::writeMetaBool(md,17,true); md.u8(255); ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);} return true; }
            return true;
        }
    }
    // no flower: wander
    return false;
}
bool WolfAngerGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Wolf) return false;
    if (ctx.lastHurtTick>=0 && ctx.srv && ctx.srv->tickNoForTest() - ctx.lastHurtTick < 40) return true;
    if (m.wolfAngerTarget!=-1 && ctx.srv && ctx.srv->tickNoForTest() < m.wolfAngerUntil) return true;
    if (ctx.nearestPlayer && ctx.nearestPlayerDist2 < 4) return false; // not angry by proximity alone
    return false;
}
bool WolfAngerGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Wolf) return false;
    if (m.isTamed) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d>12) return false;
    if (d<1.9) { if(now%20==0 && ctx.srv) ctx.srv->mobAttackPlayer(m,*t); return true; }
    m.x += dx/d*0.11; m.z += dz/d*0.11; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (ctx.srv) { WriteBuffer md; md.varint(m.entityId); meta::writeMetaByte(md,16,1); md.u8(255); ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);}
    m.wolfAngerTarget = t->entityId; m.wolfAngerUntil = now + 100;
    return true;
}
bool DrownedTridentGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Drowned) return false;
    if (!ctx.nearestPlayer) return false;
    if (ctx.nearestPlayerDist2 > 16*16) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.drownedTridentCooldown) return false;
    return true;
}
bool DrownedTridentGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Drowned) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    double dx=t->x - m.x, dy=(t->y+1.0)-(m.y+1.6), dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d>16) return false;
    if (d<5) return false; // melee takes over
    if (ctx.srv) {
        ctx.srv->spawnProjectile(ProjectileKind::Trident, m.x, m.y+1.6, m.z, dx/d*1.2, dy/d*0.2+0.15, dz/d*1.2, m.entityId, false);
        ctx.srv->broadcastSound("minecraft:entity.drowned.shoot", m.x,m.y,m.z,1.f,1.f,"hostile");
    }
    m.drownedTridentCooldown = now + 40;
    return true;
}
bool VillagerScheduleGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Villager && m.kind != MobKind::WanderingTrader) return false;
    int tod = (int)(ctx.srv ? ctx.srv->dayTime()%24000 : now%24000);
    // work 0-12000 wander, gather 12000-18000, rest 18000-24000
    if (tod < 12000) {
        if (rand()%40==0 && ctx.srv) ctx.srv->broadcastSound("minecraft:entity.villager.work", m.x,m.y,m.z,0.5f,1.f,"neutral");
    } else if (tod < 18000) {
        // gather: move slowly
        if (rand()%20==0){ double ang=rand()/(double)RAND_MAX*6.28; m.x+=std::cos(ang)*0.04; m.z+=std::sin(ang)*0.04; }
    } else {
        // rest: stay
    }
    // restock tick already in mobsTick
    return true;
}
bool PiglinBarterGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Piglin) return false;
    if (!ctx.nearestPlayer) return false;
    if (ctx.nearestPlayerDist2 > 8*8) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.piglinBarterCooldown) return false;
    // check player holds gold ingot
    if (ctx.nearestPlayer->heldSlot>=0 && ctx.nearestPlayer->heldSlot<9) {
        auto& h=ctx.nearestPlayer->inv[36+ctx.nearestPlayer->heldSlot];
        if (!h.empty() && h.name()=="minecraft:gold_ingot") return true;
    }
    return false;
}
bool PiglinBarterGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Piglin) return false;
    m.piglinBarterCooldown = now + 100;
    if (ctx.srv) ctx.srv->broadcastSound("minecraft:entity.piglin.admiring_item", m.x,m.y,m.z,1.f,1.f,"neutral");
    return true;
}
bool CatScareGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Cat) return false;
    if (!ctx.nearestPlayer) return false;
    return ctx.nearestPlayerDist2 < 6*6;
}
bool CatScareGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Cat) return false;
    if (now < m.catScareCooldown) return false;
    // cat scares creeper: just sit and purr
    if (ctx.srv) ctx.srv->broadcastSound("minecraft:entity.cat.purr", m.x,m.y,m.z,0.5f,1.f,"neutral");
    m.catScareCooldown = now + 60;
    // creeper avoid handled elsewhere
    return true;
}
bool FoxPounceGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Fox) return false;
    if (!ctx.nearestPlayer) return false;
    double d=std::sqrt(ctx.nearestPlayerDist2); return d>=2 && d<=6;
}
bool FoxPounceGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Fox) return false;
    if (now < m.foxPounceCooldown) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x += dx/d*0.42; m.z += dz/d*0.42; m.y += 0.38; if(ctx.srv){ WriteBuffer vel; vel.varint(m.entityId); vel.i16((int16_t)(dx/d*0.42*8000)); vel.i16((int16_t)(0.38*8000)); vel.i16((int16_t)(dz/d*0.42*8000)); ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vel); }
    m.foxPounceCooldown = now + 40;
    return true;
}
bool PandaRollGoal::shouldStart(MobEntity& m, AiContext&) {
    if (m.kind != MobKind::Panda) return false;
    return (rand()%200==0);
}
bool PandaRollGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Panda) return false;
    if (now < m.pandaRollCooldown) return false;
    m.pandaRollCooldown = now + 100;
    if (ctx.srv) ctx.srv->broadcastSound("minecraft:entity.panda.cant_breed", m.x,m.y,m.z,1.f,1.f,"neutral");
    return true;
}
bool DolphinPlayGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Dolphin) return false;
    if (!ctx.nearestPlayer) return false;
    return ctx.nearestPlayerDist2 < 10*10;
}
bool DolphinPlayGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Dolphin) return false;
    if (now < m.dolphinPlayCooldown) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x += dx/d*0.13; m.z += dz/d*0.13; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (ctx.srv && rand()%30==0) ctx.srv->broadcastSound("minecraft:entity.dolphin.play", m.x,m.y,m.z,1.f,1.f,"neutral");
    m.dolphinPlayCooldown = now + 20;
    return true;
}
bool EvokerFangGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Evoker) return false;
    if (!ctx.nearestPlayer) return false;
    if (ctx.nearestPlayerDist2 > 12*12) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.evokerFangCooldown) return false;
    return true;
}
bool EvokerFangGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Evoker) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    if (ctx.srv) {
        // spawn evoker fangs at target pos
        for(int i=0;i<3;++i){
            double fx=t->x + (rand()/(double)RAND_MAX-0.5)*2, fz=t->z + (rand()/(double)RAND_MAX-0.5)*2;
            auto fang=std::make_shared<MobEntity>(); fang->entityId=ctx.srv->nextEntityId(); fang->kind=MobKind::EvokerFangs; fang->x=fx; fang->y=t->y; fang->z=fz; fang->health=1;
            ctx.srv->mobsForTest().push_back(fang); ctx.srv->broadcastMobSpawn(*fang);
        }
        ctx.srv->broadcastSound("minecraft:entity.evoker.cast_spell", m.x,m.y,m.z,1.f,1.f,"hostile");
        ctx.srv->applyDamage(*t, 6.f, DamageSource::magic());
    }
    m.evokerFangCooldown = now + 60;
    return true;
}

// -------------------------------------------------------- ranged attacks --

Brain::~Brain() = default;
void Brain::setBehaviorTree(std::unique_ptr<BehaviorTree> t) { behaviorTree_ = std::move(t); }
bool Brain::hasBehaviorTree() const { return behaviorTree_ != nullptr; }
void Brain::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    NearestPlayerSensor::update(m, ctx);
    // plan34 §3 ArmadilloScareDetectedSensor scanRate 5 / 80 TTL
    if (m.kind == MobKind::Armadillo) {
        if (now - m.armadilloLastScanTick >= 5) {
            m.armadilloLastScanTick = now;
            bool danger = false;
            if (ctx.nearestPlayer && ctx.nearestPlayerDist2 < 7*7) {
                if (ctx.nearestPlayer->isSprinting) danger = true;
                else if (now - ctx.lastHurtTick < 80) danger = true;
                else if (m.health < mobStats(m.kind).maxHealth) danger = true;
                else if (ctx.nearestPlayerDist2 < 3*3) danger = true;
            } else if (now - ctx.lastHurtTick < 80) danger = true;
            if (danger) m.armadilloDangerDetectedUntil = now + 80;
        }
        ctx.dangerDetectedRecently = now < m.armadilloDangerDetectedUntil;
        // also water immediate danger clear handled in goal tick, but keep TTL
    }
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
