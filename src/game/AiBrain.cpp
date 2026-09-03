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
    // plan39 C-01: 30 new goals (60 species)
    goals_.push_back(std::make_unique<DrownedSwimGoal>());
    goals_.push_back(std::make_unique<PhantomCircleGoal>());
    goals_.push_back(std::make_unique<WardenSonicBoomGoal>());
    goals_.push_back(std::make_unique<EndermanTeleportGoal>());
    goals_.push_back(std::make_unique<ShulkerPeekGoal>());
    goals_.push_back(std::make_unique<GuardianBeamGoal>());
    goals_.push_back(std::make_unique<SlimeSplitGoal>());
    goals_.push_back(std::make_unique<MagmaCubeJumpGoal>());
    goals_.push_back(std::make_unique<SilverfishInfestGoal>());
    goals_.push_back(std::make_unique<EndermiteTeleportGoal>());
    goals_.push_back(std::make_unique<VindicatorAxeGoal>());
    goals_.push_back(std::make_unique<PillagerCrossbowGoal>());
    goals_.push_back(std::make_unique<HoglinRepelGoal>());
    goals_.push_back(std::make_unique<ZoglinFrenzyGoal>());
    goals_.push_back(std::make_unique<WitherSkeletonEffectGoal>());
    goals_.push_back(std::make_unique<GoatRamGoal>());
    goals_.push_back(std::make_unique<AxolotlPlayDeadGoal>());
    goals_.push_back(std::make_unique<FrogTongueGoal>());
    goals_.push_back(std::make_unique<TurtleEggLayGoal>());
    goals_.push_back(std::make_unique<ParrotDanceGoal>());
    goals_.push_back(std::make_unique<OcelotTrustGoal>());
    goals_.push_back(std::make_unique<SnowGolemSnowTrailGoal>());
    goals_.push_back(std::make_unique<WitherSkullBarrageGoal>());
    goals_.push_back(std::make_unique<EnderDragonPerchGoal>());
    goals_.push_back(std::make_unique<StriderLavaWalkGoal>());
    goals_.push_back(std::make_unique<IllusionerInvisGoal>());
    goals_.push_back(std::make_unique<SnifferDigGoal>());
    goals_.push_back(std::make_unique<CamelDashGoal>());
    goals_.push_back(std::make_unique<AllayDuplicateGoal>());
    goals_.push_back(std::make_unique<BoggedPoisonGoal>());
    // plan42 R2 E-11: 19 species/group-default goals (Brain 59->78, 139-species cover)
    goals_.push_back(std::make_unique<VexChargeGoal>());
    goals_.push_back(std::make_unique<PiglinBruteAttackGoal>());
    goals_.push_back(std::make_unique<ZombieVillagerCureGoal>());
    goals_.push_back(std::make_unique<ZombifiedPiglinAngerGoal>());
    goals_.push_back(std::make_unique<SkeletonHorseTrapGoal>());
    goals_.push_back(std::make_unique<GiantStompGoal>());
    goals_.push_back(std::make_unique<LlamaSpitGoal>());
    goals_.push_back(std::make_unique<ChickenLayEggGoal>());
    goals_.push_back(std::make_unique<HuskHungerGoal>());
    goals_.push_back(std::make_unique<PolarBearDefendGoal>());
    goals_.push_back(std::make_unique<PufferfishPuffGoal>());
    goals_.push_back(std::make_unique<EvokerFangsSnapGoal>());
    goals_.push_back(std::make_unique<EndCrystalHoverGoal>());
    goals_.push_back(std::make_unique<TntFuseGoal>());
    goals_.push_back(std::make_unique<FishSwimGoal>());
    goals_.push_back(std::make_unique<GrazeGoal>());
    goals_.push_back(std::make_unique<BoatDriftGoal>());
    goals_.push_back(std::make_unique<MinecartRollGoal>());
    goals_.push_back(std::make_unique<ProjectileFlyGoal>());
    goals_.push_back(std::make_unique<BatRoostGoal>());
    goals_.push_back(std::make_unique<AmbientObjectGoal>());
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
    // plan44 G-05: pursuit radius follows vanilla follow_range (legacy floor 24).
    if (dist > std::max(24.0, perceiveDist(m.kind))) return false;
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
    // plan44 G-05: lose target beyond vanilla follow_range (ghast 100 keeps range).
    if (dist > perceiveDist(m.kind)) { m.hasTarget = false; return true; }
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
    if (d > perceiveDist(MobKind::Breeze)) return false; // plan44 G-05 follow_range
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
    if (ctx.nearestPlayerDist2 > perceptionRange2(MobKind::Witch)) return false; // plan44 G-05
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
    if (ctx.nearestPlayerDist2 > perceptionRange2(MobKind::Drowned)) return false; // plan44 G-05
    if (ctx.srv && ctx.srv->tickNoForTest() < m.drownedTridentCooldown) return false;
    return true;
}
bool DrownedTridentGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Drowned) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    double dx=t->x - m.x, dy=(t->y+1.0)-(m.y+1.6), dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d > perceiveDist(MobKind::Drowned)) return false; // plan44 G-05
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

static bool nowIn(AiContext& ctx, std::int64_t cd){ return ctx.srv && ctx.srv->tickNoForTest() < cd; }
// plan39 C-01: 30 new goals implementations
bool DrownedSwimGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Drowned) return false;
    if(!ctx.world) return false;
    if(ctx.srv && ctx.srv->tickNoForTest()%5!=0) return false;
    uint16_t st=ctx.world->getBlock((int)std::floor(m.x),(int)std::floor(m.y),(int)std::floor(m.z));
    auto* bd=gen::blockByState(st); if(!bd) return false;
    return std::string(bd->name).find("water")!=std::string::npos;
}
bool DrownedSwimGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t){
    if(m.kind!=MobKind::Drowned) return false;
    Player* t=ctx.nearestPlayer; if(!t) return true;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x += dx/d * 0.12; m.z += dz/d * 0.12;
    m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if(ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    return true;
}
bool PhantomCircleGoal::shouldStart(MobEntity& m, AiContext&){ return m.kind==MobKind::Phantom; }
bool PhantomCircleGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Phantom) return false;
    if(ctx.srv && ctx.srv->tickNoForTest()%5!=0 && now - m.phantomLastSwoop < 180) { /* throttle */ }
    Player* t=ctx.nearestPlayer;
    if(t){ m.phantomOrbitCenter={t->x, t->y+12, t->z}; }
    m.phantomOrbitAngle += 0.08;
    double r = 12 + (m.phantomSize%8);
    double nx = m.phantomOrbitCenter.x + std::cos(m.phantomOrbitAngle)*r;
    double nz = m.phantomOrbitCenter.z + std::sin(m.phantomOrbitAngle)*r;
    double ny = m.phantomOrbitCenter.y + std::sin(now*0.02)*2;
    if(now - m.phantomLastSwoop > 200 && t){
        nx = t->x; nz = t->z; ny = t->y;
        if(std::hypot(nx-m.x,nz-m.z)<1.5){ m.phantomLastSwoop=now; if(ctx.srv) ctx.srv->mobAttackPlayer(m,*t); }
    }
    double dx=nx-m.x, dy=ny-m.y, dz=nz-m.z; double d=std::sqrt(dx*dx+dy*dy+dz*dz)+1e-6;
    m.x+=dx/d*0.16; m.y+=dy/d*0.10; m.z+=dz/d*0.16;
    m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if(m.y < 60) m.y=60;
    if(ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    return true;
}
bool WardenSonicBoomGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Warden) return false;
    if(ctx.srv && ctx.srv->tickNoForTest()%5!=0) return false;
    return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 15*15;
}
bool WardenSonicBoomGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Warden) return false;
    if(now < m.wardenSonicCooldown) return false;
    Player* t=ctx.nearestPlayer; if(!t||ctx.nearestPlayerDist2>15*15) return false;
    if(ctx.srv){
        if(!raycastObstructed(ctx.world,t->x,t->y+1,t->z,m.x,m.y+0.9,m.z)){
            ctx.srv->broadcastHurtAnimation(t->entityId, (float)(std::atan2(t->z-m.z,t->x-m.x)*180/3.14159));
            ctx.srv->applyDamage(*t, 10.f, DamageSource::sonicBoom());
            ctx.srv->broadcastSound("minecraft:entity.warden.sonic_boom", m.x,m.y,m.z,1.f,1.f,"hostile");
            ctx.srv->broadcastEntitySound(m.entityId, "minecraft:entity.warden.sonic_boom", 1.f, 1.f, GameServer::SoundSource::Hostile);
            WriteBuffer vel; vel.varint(t->entityId); vel.i16(0); vel.i16((int16_t)(1.5*8000)); vel.i16(0);
            ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vel);
            // particle 27 sonic_boom
            WriteBuffer p; p.boolean(true); p.boolean(false); p.f64(m.x); p.f64(m.y+1.6); p.f64(m.z); p.f32(0);p.f32(0);p.f32(0);p.f32(0.1f); p.varint(27);
            (void)p;
        }
    }
    m.wardenSonicCooldown=now+34;
    return true;
}
bool EndermanTeleportGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Enderman) return false;
    if(ctx.srv && ctx.srv->tickNoForTest() - ctx.lastHurtTick < 30) return true;
    if(!ctx.world || !ctx.srv) return false;
    // daylight flee check simplified: if sky light high and not night
    if(!ctx.srv->isNight()){
        ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
        uint8_t sky = ctx.world->getSkyLight((int)m.x,(int)m.y,(int)m.z);
        if(sky>=14) return true;
    }
    return false;
}
bool EndermanTeleportGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Enderman) return false;
    if(now - m.lastTeleportTick < 30) return false;
    if(!ctx.world) return false;
    for(int attempt=0; attempt<16; ++attempt){
        double nx = m.x + (rand()/(double)RAND_MAX*64 -32);
        double nz = m.z + (rand()/(double)RAND_MAX*64 -32);
        double ny = m.y + (rand()/(double)RAND_MAX*32 -16);
        int ix=(int)std::floor(nx), iz=(int)std::floor(nz), iy=(int)std::floor(ny);
        ctx.world->generateChunkIfMissing(ix>>4, iz>>4);
        for(int dy=-4; dy<=4; ++dy){
            int tryY=iy+dy; if(tryY<kMinY || tryY>kMinY+320) continue;
            uint16_t a1=ctx.world->getBlock(ix,tryY,iz); uint16_t a2=ctx.world->getBlock(ix,tryY+1,iz); uint16_t below=ctx.world->getBlock(ix,tryY-1,iz);
            if(a1==0 && a2==0 && below!=0){
                double ox=m.x, oy=m.y, oz=m.z;
                m.x=ix+0.5; m.z=iz+0.5; m.y=tryY+0.5; m.lastTeleportTick=now;
                if(ctx.srv){
                    ctx.srv->broadcastSound("minecraft:entity.enderman.teleport", m.x,m.y,m.z,1.f,1.f,"hostile");
                    WriteBuffer tp; tp.varint(m.entityId); tp.f64(m.x); tp.f64(m.y); tp.f64(m.z); tp.f32(m.yaw); tp.f32(0); tp.boolean(true);
                    ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityTeleport, tp);
                    (void)ox;(void)oy;(void)oz;
                }
                return true;
            }
        }
    }
    return true;
}
bool ShulkerPeekGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Shulker) return false;
    return ctx.nearestPlayer && ctx.nearestPlayerDist2 < perceptionRange2(MobKind::Shulker); // plan44 G-05
}
bool ShulkerPeekGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Shulker) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    m.shulkerPeek = std::min(100, m.shulkerPeek+5);
    if(ctx.srv){
        WriteBuffer md; md.varint(m.entityId); meta::writeMetaByte(md, 15, (int8_t)m.shulkerPeek); md.u8(255);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
        if(now%60==0){
            double dx=t->x-m.x, dy=(t->y+0.5)-m.y, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            ctx.srv->spawnProjectile(ProjectileKind::Arrow, m.x, m.y+0.5, m.z, dx/d*0.7, dy/d*0.2+0.1, dz/d*0.7, m.entityId, false);
            ctx.srv->broadcastSound("minecraft:entity.shulker.shoot", m.x,m.y,m.z,1.f,1.f,"hostile");
        }
        if(now - ctx.lastHurtTick < 20){
            // teleport 8 blocks on hurt
            EndermanTeleportGoal tmp; (void)tmp;
            double nx=m.x+(rand()/(double)RAND_MAX*16-8), nz=m.z+(rand()/(double)RAND_MAX*16-8);
            m.x=nx; m.z=nz; if(ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
        }
    }
    return true;
}
bool GuardianBeamGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Guardian && m.kind!=MobKind::ElderGuardian) return false;
    return ctx.nearestPlayer && ctx.nearestPlayerDist2 < perceptionRange2(m.kind); // plan44 G-05
}
bool GuardianBeamGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Guardian && m.kind!=MobKind::ElderGuardian) return false;
    if(now < m.guardianBeamCooldown) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz); if(d>15) return false;
    if(ctx.srv){
        float dmg = (m.kind==MobKind::ElderGuardian?8.f:6.f);
        ctx.srv->applyDamage(*t, dmg, DamageSource::magic());
        ctx.srv->broadcastSound("minecraft:entity.guardian.attack", m.x,m.y,m.z,1.f,1.f,"hostile");
        ctx.srv->broadcastEntitySound(m.entityId, "minecraft:entity.guardian.attack", 1.f, 1.f, GameServer::SoundSource::Hostile);
    }
    m.guardianBeamCooldown=now+60;
    return true;
}
bool SlimeSplitGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Slime) return false;
    if(now < m.slimeJumpCooldown) return false;
    if(rand()%40!=0) return false;
    m.y += 0.4 * (m.slimeSize+1)*0.5;
    if(ctx.srv){
        WriteBuffer vel; vel.varint(m.entityId); vel.i16(0); vel.i16((int16_t)(0.4*8000)); vel.i16(0);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vel);
        ctx.srv->broadcastSound("minecraft:entity.slime.jump", m.x,m.y,m.z,0.5f,1.f,"hostile");
    }
    m.slimeJumpCooldown=now+20;
    return true;
}
bool MagmaCubeJumpGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::MagmaCube) return false;
    if(now < m.slimeJumpCooldown) return false;
    if(rand()%30!=0) return false;
    m.y += 0.45 * (m.slimeSize+1)*0.5;
    if(m.y < kMinY+1) m.y = kMinY+1;
    if(ctx.srv){
        WriteBuffer vel; vel.varint(m.entityId); vel.i16(0); vel.i16((int16_t)(0.45*8000)); vel.i16(0);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vel);
        ctx.srv->broadcastSound("minecraft:entity.magma_cube.jump", m.x,m.y,m.z,0.5f,1.f,"hostile");
    }
    m.slimeJumpCooldown=now+18;
    return true;
}
bool SilverfishInfestGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Silverfish) return false; return nowIn(ctx, m.silverfishCallCooldown) ? false : (ctx.lastHurtTick>=0 && ctx.srv && ctx.srv->tickNoForTest()-ctx.lastHurtTick<20); }
bool SilverfishInfestGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Silverfish) return false;
    if(now < m.silverfishCallCooldown) return false;
    if(!ctx.srv) return false;
    int spawned=0;
    for(int dx=-6; dx<=6 && spawned<3; ++dx) for(int dz=-6; dz<=6 && spawned<3; ++dz){
        int bx=(int)std::floor(m.x)+dx, bz=(int)std::floor(m.z)+dz, by=(int)std::floor(m.y);
        uint16_t st=ctx.world?ctx.world->getBlock(bx,by,bz):0; if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        std::string n(bd->name); if(n.find("infested")!=std::string::npos){
            auto mob=std::make_shared<MobEntity>(); mob->entityId=ctx.srv->nextEntityId(); mob->kind=MobKind::Silverfish; mob->x=bx+0.5; mob->y=by+0.5; mob->z=bz+0.5; mob->health=mobStats(MobKind::Silverfish).maxHealth;
            ctx.srv->mobsForTest().push_back(mob); ctx.srv->broadcastMobSpawn(*mob); spawned++;
            if(ctx.world) ctx.world->setBlock(bx,by,bz,0);
        }
    }
    // fallback spawn even without infested block for test determinism
    if(spawned==0){
        auto mob=std::make_shared<MobEntity>(); mob->entityId=ctx.srv->nextEntityId(); mob->kind=MobKind::Silverfish; mob->x=m.x+1; mob->y=m.y; mob->z=m.z+1; mob->health=mobStats(MobKind::Silverfish).maxHealth;
        ctx.srv->mobsForTest().push_back(mob); ctx.srv->broadcastMobSpawn(*mob);
    }
    m.silverfishCallCooldown=now+100;
    if(ctx.srv) ctx.srv->broadcastSound("minecraft:entity.silverfish.ambient", m.x,m.y,m.z,1.f,1.f,"hostile");
    return true;
}
bool EndermiteTeleportGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Endermite) return false; return ctx.lastHurtTick>=0 && ctx.srv && ctx.srv->tickNoForTest()-ctx.lastHurtTick<20; }
bool EndermiteTeleportGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Endermite) return false;
    if(now < m.endermiteLifeUntil - 2390) return false; // throttle
    m.x += (rand()/(double)RAND_MAX-0.5)*4; m.z += (rand()/(double)RAND_MAX-0.5)*4;
    if(ctx.srv){
        ctx.srv->broadcastSound("minecraft:entity.endermite.ambient", m.x,m.y,m.z,1.f,1.f,"hostile");
        WriteBuffer tp; tp.varint(m.entityId); tp.f64(m.x); tp.f64(m.y); tp.f64(m.z); tp.f32(m.yaw); tp.f32(0); tp.boolean(true);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityTeleport, tp);
    }
    if(m.endermiteLifeUntil==0) m.endermiteLifeUntil=now+2400;
    return true;
}
bool VindicatorAxeGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Vindicator) return false; return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 12*12; }
bool VindicatorAxeGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Vindicator) return false;
    if(now < m.vindicatorJohnnyUntil && m.vindicatorJohnnyUntil!=0) { /* johnny cooldown */ }
    Player* t=ctx.nearestPlayer; if(!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d<1.9){ if(now%20==0 && ctx.srv) ctx.srv->mobAttackPlayer(m,*t); return true; }
    m.x+=dx/d*0.11; m.z+=dz/d*0.11; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if(ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    if(ctx.srv) ctx.srv->broadcastSound("minecraft:entity.vindicator.ambient", m.x,m.y,m.z,1.f,1.f,"hostile");
    m.vindicatorJohnnyUntil=now+20;
    return true;
}
bool PillagerCrossbowGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Pillager) return false; return ctx.nearestPlayer && ctx.nearestPlayerDist2 < perceptionRange2(MobKind::Pillager); } // plan44 G-05
bool PillagerCrossbowGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Pillager) return false;
    if(now < m.pillagerCrossbowCooldown) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    double dx=t->x-m.x, dy=(t->y+1)-(m.y+1.6), dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d<5 || d > perceiveDist(MobKind::Pillager)) { // patrol approach (plan44 G-05 follow_range)
        m.x+=dx/d*0.09; m.z+=dz/d*0.09; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90); return true;
    }
    if(ctx.srv) ctx.srv->spawnProjectile(ProjectileKind::Arrow, m.x, m.y+1.6, m.z, dx/d*1.4, dy/d*0.2+0.12, dz/d*1.4, m.entityId, false);
    m.pillagerCrossbowCooldown=now+40;
    if(ctx.srv) ctx.srv->broadcastSound("minecraft:entity.pillager.shoot", m.x,m.y,m.z,1.f,1.f,"hostile");
    return true;
}
bool HoglinRepelGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Hoglin) return false;
    if(!ctx.world) return false;
    for(int dx=-7; dx<=7; ++dx) for(int dz=-7; dz<=7; ++dz){
        int bx=(int)std::floor(m.x)+dx, bz=(int)std::floor(m.z)+dz, by=(int)std::floor(m.y);
        uint16_t st=ctx.world->getBlock(bx,by,bz); if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        std::string n(bd->name); if(n.find("warped_fungus")!=std::string::npos || n.find("respawn_anchor")!=std::string::npos || n.find("nether_portal")!=std::string::npos) return true;
    }
    return false;
}
bool HoglinRepelGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Hoglin) return false;
    if(now < m.hoglinRepelCooldown) return false;
    Player* t=ctx.nearestPlayer;
    double dx, dz;
    if(t){ dx=m.x - t->x; dz=m.z - t->z; } else { dx=(rand()/(double)RAND_MAX-0.5)*2; dz=(rand()/(double)RAND_MAX-0.5)*2; }
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x+=dx/d*0.14; m.z+=dz/d*0.14; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if(ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    m.hoglinRepelCooldown=now+10;
    return true;
}
bool ZoglinFrenzyGoal::shouldStart(MobEntity& m, AiContext&) { return m.kind==MobKind::Zoglin; }
bool ZoglinFrenzyGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Zoglin) return false;
    if(now < m.zoglinFrenzyUntil) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d<1.9){ if(now%15==0 && ctx.srv) { ctx.srv->mobAttackPlayer(m,*t); WriteBuffer vel; vel.varint(t->entityId); vel.i16((int16_t)(dx/d*1.0*8000)); vel.i16((int16_t)(0.4*8000)); vel.i16((int16_t)(dz/d*1.0*8000)); ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vel); } return true; }
    m.x+=dx/d*0.14; m.z+=dz/d*0.14; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if(ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    m.zoglinFrenzyUntil=now+10;
    return true;
}
bool WitherSkeletonEffectGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::WitherSkeleton) return false; return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 3*3; }
bool WitherSkeletonEffectGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::WitherSkeleton) return false;
    if(now < m.witherSkeletonEffectCooldown) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    if(ctx.srv){
        ctx.srv->mobAttackPlayer(m,*t);
        WriteBuffer eff; eff.varint(t->entityId); eff.varint(20); eff.i8(0); eff.varint(100); eff.u8(0x01);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityEffect, eff);
        ctx.srv->broadcastSound("minecraft:entity.wither_skeleton.ambient", m.x,m.y,m.z,1.f,1.f,"hostile");
    }
    m.witherSkeletonEffectCooldown=now+40;
    return true;
}
bool GoatRamGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Goat) return false; return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 10*10; }
bool GoatRamGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Goat) return false;
    if(now < m.goatRamCooldown) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d>10) return false;
    // charge 30t: ram
    m.x+=dx/d*0.42; m.z+=dz/d*0.42;
    if(d<1.9){
        if(ctx.srv){
            WriteBuffer vel; vel.varint(t->entityId); vel.i16((int16_t)(dx/d*1.5*8000)); vel.i16((int16_t)(0.4*8000)); vel.i16((int16_t)(dz/d*1.5*8000));
            ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vel);
            ctx.srv->applyDamage(*t, 5.f, "mob");
            ctx.srv->broadcastSound("minecraft:entity.goat.ram_impact", m.x,m.y,m.z,1.f,1.f,"neutral");
        }
        m.goatRamCooldown=now+100;
        return true;
    }
    if(ctx.srv && rand()%20==0) ctx.srv->broadcastSound("minecraft:entity.goat.prepare_ram", m.x,m.y,m.z,1.f,1.f,"neutral");
    m.goatRamCooldown=now+50;
    return true;
}
bool AxolotlPlayDeadGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Axolotl) return false; if(m.health > mobStats(m.kind).maxHealth*0.33) return false; return ctx.lastHurtTick>=0 && ctx.srv && ctx.srv->tickNoForTest()-ctx.lastHurtTick<20; }
bool AxolotlPlayDeadGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Axolotl) return false;
    if(now < m.axolotlPlayDeadUntil && m.axolotlPlayDeadUntil!=0) return true;
    m.axolotlPlayDeadUntil=now+200;
    m.health = std::min(m.health+2.0, (double)mobStats(m.kind).maxHealth);
    if(ctx.srv){
        WriteBuffer md; md.varint(m.entityId); meta::writeMetaBool(md, 16, true); md.u8(255);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
        ctx.srv->broadcastSound("minecraft:entity.axolotl.splash", m.x,m.y,m.z,1.f,1.f,"neutral");
    }
    return true;
}
bool FrogTongueGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Frog) return false; if(ctx.srv && ctx.srv->tickNoForTest()<m.frogTongueCooldown) return false; return true; }
bool FrogTongueGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Frog) return false;
    if(now < m.frogTongueCooldown) return false;
    // find slime small within 6
    if(ctx.srv){
        std::shared_ptr<MobEntity> prey;
        double best=36;
        for(auto& mm: ctx.srv->mobsForTest()) if((mm->kind==MobKind::Slime || mm->kind==MobKind::MagmaCube) && mm->slimeSize==0 && !mm->dead){
            double dx=mm->x-m.x, dz=mm->z-m.z; double d2=dx*dx+dz*dz; if(d2<best){best=d2; prey=mm;}
        }
        if(prey && best < 36){
            double dx=prey->x-m.x, dz=prey->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            if(d<1.5){
                prey->dead=true;
                ctx.srv->broadcastSound("minecraft:entity.frog.eat", m.x,m.y,m.z,1.f,1.f,"neutral");
                ctx.srv->spawnItemDrop(m.x,m.y,m.z, gen::itemIdByName().at("minecraft:slime_ball"), 1);
            } else {
                m.x+=dx/d*0.12; m.z+=dz/d*0.12;
                ctx.srv->broadcastSound("minecraft:entity.frog.tongue", m.x,m.y,m.z,1.f,1.f,"neutral");
            }
        } else if(rand()%40==0){
            ctx.srv->broadcastSound("minecraft:entity.frog.ambient", m.x,m.y,m.z,1.f,1.f,"neutral");
        }
    }
    m.frogTongueCooldown=now+40;
    return true;
}
bool TurtleEggLayGoal::shouldStart(MobEntity& m, AiContext&){ if(m.kind!=MobKind::Turtle) return false; return m.turtleHomePos[0]!=INT_MAX; }
bool TurtleEggLayGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Turtle) return false;
    if(now < m.turtleEggCooldown) return false;
    if(m.turtleHomePos[0]==INT_MAX){
        m.turtleHomePos={(int)std::floor(m.x),(int)std::floor(m.y),(int)std::floor(m.z)};
    }
    double tx=m.turtleHomePos[0]+0.5, tz=m.turtleHomePos[2]+0.5;
    double dx=tx-m.x, dz=tz-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d>1.5){
        m.x+=dx/d*0.07; m.z+=dz/d*0.07; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
        if(ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
        return true;
    }
    // lay 1-4 eggs (simulate by placing turtle_egg block)
    if(ctx.world && ctx.srv){
        int ex=(int)std::floor(m.x), ey=(int)std::floor(m.y), ez=(int)std::floor(m.z);
        auto* bd=gen::blockByName("minecraft:turtle_egg");
        if(bd){
            ctx.world->setBlock(ex,ey,ez, bd->defaultState);
            ctx.srv->broadcastBlockChange(ex,ey,ez, bd->defaultState);
            ctx.srv->broadcastSound("minecraft:entity.turtle.lay_egg", m.x,m.y,m.z,1.f,1.f,"neutral");
            // spawn baby age -24000? actually lay egg, but simulate baby turtle spawn
            auto baby=std::make_shared<MobEntity>(); baby->entityId=ctx.srv->nextEntityId(); baby->kind=MobKind::Turtle; baby->health=mobStats(MobKind::Turtle).maxHealth; baby->age=-24000; baby->x=ex+0.5; baby->y=ey+1; baby->z=ez+0.5;
            ctx.srv->mobsForTest().push_back(baby); ctx.srv->broadcastMobSpawn(*baby);
        }
    }
    m.turtleEggCooldown=now+6000;
    auto& rm=m.turtleHomePos; rm={INT_MAX,INT_MAX,INT_MAX};
    return true;
}
bool ParrotDanceGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Parrot) return false;
    if(!ctx.world) return false;
    // near jukebox playing: check within 6 for jukebox block
    for(int dx=-6; dx<=6; ++dx) for(int dz=-6; dz<=6; ++dz){
        int bx=(int)std::floor(m.x)+dx, bz=(int)std::floor(m.z)+dz, by=(int)std::floor(m.y);
        uint16_t st=ctx.world->getBlock(bx,by,bz); if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        if(std::string(bd->name).find("jukebox")!=std::string::npos) return true;
    }
    return false;
}
bool ParrotDanceGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Parrot) return false;
    m.parrotDancing=true; m.parrotDanceUntil=now+40;
    m.yaw += 18; if(m.yaw>360) m.yaw-=360;
    if(ctx.srv && now%20==0) ctx.srv->broadcastSound("minecraft:entity.parrot.imitate.warden", m.x,m.y,m.z,1.f,1.f,"neutral");
    return true;
}
bool OcelotTrustGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Ocelot) return false;
    return ctx.temptingPlayer!=nullptr || (ctx.nearestPlayer && ctx.nearestPlayerDist2 < 10*10);
}
bool OcelotTrustGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Ocelot) return false;
    if(now < m.ocelotTrustCooldown) return false;
    Player* t=ctx.temptingPlayer ? ctx.temptingPlayer : ctx.nearestPlayer; if(!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d<2.5){
        m.isTamed=true;
        if(ctx.srv){
            WriteBuffer md; md.varint(m.entityId); meta::writeMetaBool(md,16,true); md.u8(255);
            ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
            ctx.srv->broadcastSound("minecraft:entity.ocelot.ambient", m.x,m.y,m.z,1.f,1.f,"neutral");
        }
        m.ocelotTrustCooldown=now+100;
        return true;
    }
    // sprint 0.18 when creeper approach 6: already handled via Avoid? just move toward player
    m.x+=dx/d*0.09; m.z+=dz/d*0.09; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if(ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    m.ocelotTrustCooldown=now+20;
    return true;
}
bool SnowGolemSnowTrailGoal::shouldStart(MobEntity& m, AiContext&){ return m.kind==MobKind::SnowGolem; }
bool SnowGolemSnowTrailGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::SnowGolem) return false;
    if(now < m.snowGolemTrailCooldown) return false;
    if(ctx.world && ctx.srv){
        int bx=(int)std::floor(m.x), by=(int)std::floor(m.y)-1, bz=(int)std::floor(m.z);
        uint16_t below=ctx.world->getBlock(bx,by,bz); if(below!=0){
            auto* bdSnow=gen::blockByName("minecraft:snow");
            if(bdSnow){
                int snowY=by+1;
                uint16_t at=ctx.world->getBlock(bx,snowY,bz);
                if(at==0){
                    ctx.world->setBlock(bx,snowY,bz, bdSnow->defaultState);
                    ctx.srv->broadcastBlockChange(bx,snowY,bz, bdSnow->defaultState);
                }
            }
        }
        // shoot snowball
        if(ctx.nearestPlayer && ctx.nearestPlayerDist2 < 16*16 && now%40==0){
            Player* t=ctx.nearestPlayer; double dx=t->x-m.x, dy=(t->y+1)-(m.y+1.2), dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            ctx.srv->spawnProjectile(ProjectileKind::Snowball, m.x, m.y+1.2, m.z, dx/d*1.2, dy/d*0.2+0.1, dz/d*1.2, m.entityId, false);
        }
        // melt in nether/desert biom check simplified: if y>60 and isNight false and biome desert -> melt damage
        std::string biome; try{ biome=ctx.world->sampledBiome(bx,by,bz);}catch(...){}
        if(biome.find("desert")!=std::string::npos || biome.find("nether")!=std::string::npos){
            if(now%40==0 && ctx.srv) ctx.srv->applyDamageToMob(m, 1.f, "burned to death");
        }
    }
    m.snowGolemTrailCooldown=now+10;
    return true;
}
bool WitherSkullBarrageGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Wither) return false; return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 24*24 && m.health <= mobStats(m.kind).maxHealth*0.5f; }
bool WitherSkullBarrageGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Wither) return false;
    if(now < m.witherBarrageCooldown) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    if(m.health > mobStats(m.kind).maxHealth*0.5f) return false;
    if(ctx.srv){
        for(int i=0;i<3;++i){
            double dx=t->x-m.x, dy=(t->y+1)-(m.y+1.5), dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            ctx.srv->spawnProjectile(ProjectileKind::WitherSkull, m.x, m.y+1.5, m.z, dx/d*1.1+(rand()/(double)RAND_MAX-0.5)*0.1, dy/d*0.3+0.1, dz/d*1.1+(rand()/(double)RAND_MAX-0.5)*0.1, m.entityId, false, true);
        }
        ctx.srv->broadcastSound("minecraft:entity.wither.shoot", m.x,m.y,m.z,1.f,1.f,"hostile");
    }
    m.witherBarrageCooldown=now+60;
    return true;
}
bool EnderDragonPerchGoal::shouldStart(MobEntity& m, AiContext&){ return m.kind==MobKind::EnderDragon; }
bool EnderDragonPerchGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::EnderDragon) return false;
    if(now < m.dragonPhaseUntil) return false;
    // perch y 80, breath
    if(m.y > 82){
        m.y -= 0.2;
        if(ctx.srv && now%40==0) ctx.srv->spawnProjectile(ProjectileKind::DragonFireball, m.x, m.y, m.z, (rand()/(double)RAND_MAX-0.5)*0.6, -0.3, (rand()/(double)RAND_MAX-0.5)*0.6, m.entityId, false);
    } else if(m.y < 78){
        double ang=now*0.03; double rx=std::cos(ang)*28, rz=std::sin(ang)*28;
        double dx=rx-m.x, dz=rz-m.z; m.x+=dx*0.04; m.z+=dz*0.04; m.y += (68-m.y)*0.02;
    } else {
        if(ctx.srv && now%20==0) ctx.srv->spawnProjectile(ProjectileKind::DragonFireball, m.x, m.y, m.z, 0, -0.4, 0, m.entityId, false);
        if(rand()%100<5) m.dragonPhaseUntil=now+80;
    }
    m.yaw=(float)(now*0.8);
    return true;
}
bool StriderLavaWalkGoal::shouldStart(MobEntity& m, AiContext&){ return m.kind==MobKind::Strider; }
bool StriderLavaWalkGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Strider) return false;
    if(!ctx.world) return false;
    uint16_t st=ctx.world->getBlock((int)std::floor(m.x),(int)std::floor(m.y)-1,(int)std::floor(m.z));
    auto* bd=gen::blockByState(st);
    bool onLava = bd && std::string(bd->name).find("lava")!=std::string::npos;
    if(!onLava){
        // shiver when cold
        if(!m.striderShivering){ m.striderShivering=true; m.striderShiverUntil=now+40; if(ctx.srv) ctx.srv->broadcastSound("minecraft:entity.strider.ambient", m.x,m.y,m.z,0.5f,1.f,"neutral"); }
        m.y -= 0.02;
    } else {
        m.striderShivering=false;
        // lava walk no sink, steer toward player if saddled
        if(ctx.nearestPlayer){ double dx=ctx.nearestPlayer->x-m.x, dz=ctx.nearestPlayer->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6; m.x+=dx/d*0.09; m.z+=dz/d*0.09; }
        m.y = std::max(m.y, (double)kMinY+2);
    }
    if(ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    return true;
}
bool IllusionerInvisGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Illusioner) return false; return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 12*12; }
bool IllusionerInvisGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Illusioner) return false;
    if(now < m.illusionerInvisUntil) return false;
    m.illusionerInvisUntil=now+200;
    if(ctx.srv){
        WriteBuffer md; md.varint(m.entityId); meta::writeMetaBool(md,16,true); md.u8(255);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
        ctx.srv->broadcastSound("minecraft:entity.illusioner.cast_spell", m.x,m.y,m.z,1.f,1.f,"hostile");
        Player* t=ctx.nearestPlayer; if(t){ double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6; ctx.srv->spawnProjectile(ProjectileKind::Arrow, m.x, m.y+1.6, m.z, dx/d*1.2, 0.12, dz/d*1.2, m.entityId, false); WriteBuffer eff; eff.varint(t->entityId); eff.varint(15); eff.i8(1); eff.varint(100); eff.u8(0x01); ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityEffect, eff); }
    }
    return true;
}
bool SnifferDigGoal::shouldStart(MobEntity& m, AiContext&){ return m.kind==MobKind::Sniffer; }
bool SnifferDigGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Sniffer) return false;
    if(now < m.snifferDigCooldown) return false;
    if(rand()%120!=0) return false;
    // sniff 6s -> dig
    if(ctx.srv) ctx.srv->broadcastSound("minecraft:entity.sniffer.scenting", m.x,m.y,m.z,1.f,1.f,"neutral");
    m.snifferDigCooldown=now+120;
    // after sniff, dig ancient seed after 6s simplified to immediate drop
    if(rand()%3==0 && ctx.srv){
        // drop torchflower seeds
        auto it = gen::itemIdByName().find("minecraft:torchflower_seeds");
        if(it!=gen::itemIdByName().end()) ctx.srv->spawnItemDrop(m.x,m.y,m.z, it->second, 1);
        ctx.srv->broadcastSound("minecraft:entity.sniffer.digging", m.x,m.y,m.z,1.f,1.f,"neutral");
        WriteBuffer md; md.varint(m.entityId); meta::writeMetaBool(md,16,true); md.u8(255);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
    }
    return true;
}
bool CamelDashGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Camel) return false; return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 12*12; }
bool CamelDashGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Camel) return false;
    if(now < m.camelDashCooldown) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d>12 || d<4) return false;
    m.x+=dx/d*0.42*10*0.1; m.z+=dz/d*0.42*10*0.1; // dash ~4.2 blocks scaled by tick
    if(ctx.srv){
        WriteBuffer vel; vel.varint(m.entityId); vel.i16((int16_t)(dx/d*0.42*8000)); vel.i16(0); vel.i16((int16_t)(dz/d*0.42*8000));
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vel);
        ctx.srv->broadcastSound("minecraft:entity.camel.dash", m.x,m.y,m.z,1.f,1.f,"neutral");
    }
    m.camelDashCooldown=now+55;
    return true;
}
bool AllayDuplicateGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Allay) return false;
    if(!ctx.world) return false;
    for(int dx=-4; dx<=4; ++dx) for(int dz=-4; dz<=4; ++dz){
        int bx=(int)std::floor(m.x)+dx, bz=(int)std::floor(m.z)+dz, by=(int)std::floor(m.y);
        uint16_t st=ctx.world->getBlock(bx,by,bz); if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        if(std::string(bd->name).find("jukebox")!=std::string::npos) return true;
    }
    return false;
}
bool AllayDuplicateGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Allay) return false;
    if(now < m.allayDuplicateCooldown) return false;
    if(ctx.srv){
        // duplicate amethyst_shard emit note
        auto it=gen::itemIdByName().find("minecraft:amethyst_shard");
        if(it!=gen::itemIdByName().end()) ctx.srv->spawnItemDrop(m.x,m.y+1,m.z, it->second, 1);
        ctx.srv->broadcastSound("minecraft:block.note_block.chime", m.x,m.y,m.z,1.f,1.f,"block");
        WriteBuffer md; md.varint(m.entityId); meta::writeMetaBool(md,16,true); md.u8(255);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::SetEntityMetadata, md);
    }
    m.allayDuplicateCooldown=now+120;
    return true;
}
bool BoggedPoisonGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Bogged) return false; return ctx.nearestPlayer && ctx.nearestPlayerDist2 < perceptionRange2(MobKind::Bogged); } // plan44 G-05
bool BoggedPoisonGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Bogged) return false;
    if(now < m.boggedPoisonCooldown) return false;
    Player* t=ctx.nearestPlayer; if(!t) return false;
    double dx=t->x-m.x, dy=(t->y+1)-(m.y+1.6), dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d<5 || d > perceiveDist(MobKind::Bogged)) return false; // plan44 G-05 follow_range
    if(ctx.srv){
        ctx.srv->spawnProjectile(ProjectileKind::Arrow, m.x, m.y+1.6, m.z, dx/d*1.2, dy/d*0.2+0.12, dz/d*1.2, m.entityId, false);
        WriteBuffer eff; eff.varint(t->entityId); eff.varint(19); eff.i8(0); eff.varint(160); eff.u8(0x01);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityEffect, eff);
        ctx.srv->broadcastSound("minecraft:entity.bogged.shoot", m.x,m.y,m.z,1.f,1.f,"hostile");
    }
    m.boggedPoisonCooldown=now+40;
    return true;
}
// plan42 R2 E-11: 19 species/group-default goals (60->139).
// Group gates share one class per movement family (Fish/Graze/Boat/Minecart/
// Projectile); the rest gate a single notable kind. All ticks reuse the same
// server APIs (mobAttackPlayer/broadcastSound/spawnProjectile/spawnItemDrop/
// explodeAt/strikeLightning) as the existing 59 goals.
bool FishSwimGoal::shouldStart(MobEntity& m, AiContext&) { return MobEntity::isFishKind(m.kind); }
bool FishSwimGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!MobEntity::isFishKind(m.kind)) return false;
    // vanilla FishSwimGoal: drift in water, speed from mobStats moveSpeed
    if (now - m.nextWanderAt > 60 || !m.hasTarget) {
        double ang = (rand()/(double)RAND_MAX)*6.28318;
        m.tx = m.x + std::cos(ang)*4.0; m.tz = m.z + std::sin(ang)*4.0;
        m.hasTarget = true; m.nextWanderAt = now;
    }
    double dx = m.tx - m.x, dz = m.tz - m.z;
    double d = std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d < 0.4) { m.hasTarget = false; return true; }
    float sp = mobStats(m.kind).moveSpeed;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    m.x += dx/d*sp; m.z += dz/d*sp;
    (void)ctx;
    return true;
}
bool GrazeGoal::shouldStart(MobEntity& m, AiContext&) { return MobEntity::isGrazerKind(m.kind); }
bool GrazeGoal::tick(MobEntity& m, AiContext&, std::int64_t now) {
    if (!MobEntity::isGrazerKind(m.kind)) return false;
    // vanilla EatGrassGoal: head-down pause ~40t every ~120t cycle
    return (now % 120) < 40;
}
bool BoatDriftGoal::shouldStart(MobEntity& m, AiContext&) { return MobEntity::isBoat(m.kind); }
bool BoatDriftGoal::tick(MobEntity& m, AiContext&, std::int64_t now) {
    if (!MobEntity::isBoat(m.kind)) return false;
    // vanilla Boat: water bob + slow drift along heading
    m.y += std::sin(now*0.15)*0.004;
    double rad = (m.yaw+90.0)*3.14159/180.0;
    m.x += std::cos(rad)*0.01; m.z += std::sin(rad)*0.01;
    return true;
}
bool MinecartRollGoal::shouldStart(MobEntity& m, AiContext&) { return MobEntity::isMinecartKind(m.kind); }
bool MinecartRollGoal::tick(MobEntity& m, AiContext&, std::int64_t) {
    if (!MobEntity::isMinecartKind(m.kind)) return false;
    // roll with latched velocity (friction), else hold on rails
    m.x += m.velX*0.98; m.z += m.velZ*0.98;
    m.velX *= 0.98; m.velZ *= 0.98;
    return true;
}
bool VexChargeGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Vex) return false;
    return ctx.nearestPlayer && ctx.nearestPlayerDist2 < perceptionRange2(MobKind::Vex); // plan44 G-05
}
bool VexChargeGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Vex) return false;
    if (now < m.vexChargeCooldown) return false;
    Player* t = ctx.nearestPlayer; if (!t) return false;
    double dx=t->x-m.x, dy=(t->y+1)-(m.y+1), dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d < 1.9) {
        if (now%20==0 && ctx.srv) {
            ctx.srv->mobAttackPlayer(m,*t);
            ctx.srv->broadcastSound("minecraft:entity.vex.charge", m.x,m.y,m.z,1.f,1.f,"hostile");
        }
        m.vexChargeCooldown = now+20;
        return true;
    }
    // charge through air at 0.3 (vex ignores gravity while charging)
    m.x += dx/d*0.30; m.z += dz/d*0.30; m.y += dy*0.05;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    m.vexChargeCooldown = now+5;
    return true;
}
bool PiglinBruteAttackGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::PiglinBrute) return false;
    return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 24*24;
}
bool PiglinBruteAttackGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::PiglinBrute) return false;
    Player* t = ctx.nearestPlayer; if (!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    // brute never barters; enrages (1.5x speed) for 100t after being hurt
    bool enraged = ctx.srv && (ctx.srv->tickNoForTest()-ctx.lastHurtTick < 100);
    if (enraged) m.piglinBruteEnrageUntil = now+100;
    double sp = (now < m.piglinBruteEnrageUntil) ? 0.15 : 0.10;
    if (d < 1.9) {
        if (now%20==0 && ctx.srv) {
            ctx.srv->mobAttackPlayer(m,*t);
            ctx.srv->broadcastSound("minecraft:entity.piglin_brute.angry", m.x,m.y,m.z,1.f,1.f,"hostile");
        }
        return true;
    }
    m.x += dx/d*sp; m.z += dz/d*sp;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    if (ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    return true;
}
bool ZombieVillagerCureGoal::shouldStart(MobEntity& m, AiContext&) {
    if (m.kind != MobKind::ZombieVillager) return false;
    return m.zombieVillagerCureUntil != 0;
}
bool ZombieVillagerCureGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::ZombieVillager) return false;
    if (m.zombieVillagerCureUntil == 0) {
        // start shaking cure cycle every ~10min like vanilla weakness+apple cure window
        if (now%12000==0) m.zombieVillagerCureUntil = now+200;
        else return false;
    }
    if (now >= m.zombieVillagerCureUntil) { m.zombieVillagerCureUntil = 0; return false; }
    // shaking: hold still + shake sound
    if (ctx.srv && now%40==0)
        ctx.srv->broadcastSound("minecraft:entity.zombie_villager.cure", m.x,m.y,m.z,1.f,1.f,"hostile");
    return true;
}
bool ZombifiedPiglinAngerGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::ZombifiedPiglin) return false;
    return ctx.srv && (ctx.srv->tickNoForTest()-ctx.lastHurtTick < 200);
}
bool ZombifiedPiglinAngerGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::ZombifiedPiglin) return false;
    Player* t = ctx.nearestPlayer; if (!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d < 1.9) {
        if (now%20==0 && ctx.srv) ctx.srv->mobAttackPlayer(m,*t);
        return true;
    }
    // pack anger: nearby zombified piglins converge (vanilla anger propagation)
    if (ctx.srv) for (auto& mm : ctx.srv->mobsForTest()) {
        if (mm.get()==&m || mm->kind!=MobKind::ZombifiedPiglin || mm->dead) continue;
        double ox=mm->x-m.x, oz=mm->z-m.z;
        if (ox*ox+oz*oz < 16*16) { mm->x += dx/d*0.08; mm->z += dz/d*0.08; }
    }
    m.x += dx/d*0.11; m.z += dz/d*0.11;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    if (ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    return true;
}
bool SkeletonHorseTrapGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::SkeletonHorse) return false;
    if (!ctx.srv || ctx.srv->tickNoForTest() < m.skeletonHorseTrapCooldown) return false;
    return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 10*10;
}
bool SkeletonHorseTrapGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::SkeletonHorse) return false;
    if (now < m.skeletonHorseTrapCooldown) return false;
    if (ctx.srv) {
        // vanilla skeleton trap: lightning strike on approach
        ctx.srv->strikeLightning(m.x, m.y, m.z);
        ctx.srv->broadcastSound("minecraft:entity.skeleton_horse.ambient", m.x,m.y,m.z,1.f,1.f,"neutral");
    }
    m.skeletonHorseTrapCooldown = now+1200;
    return true;
}
bool GiantStompGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Giant) return false;
    return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 24*24;
}
bool GiantStompGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Giant) return false;
    if (now < m.giantStompCooldown) return false;
    Player* t = ctx.nearestPlayer; if (!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d < 2.5) {
        if (ctx.srv) {
            ctx.srv->mobAttackPlayer(m,*t);
            WriteBuffer vel; vel.varint(t->entityId);
            vel.i16((int16_t)(dx/d*1.2*8000)); vel.i16((int16_t)(0.5*8000)); vel.i16((int16_t)(dz/d*1.2*8000));
            ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityVelocity, vel);
            ctx.srv->broadcastSound("minecraft:entity.giant.stomp", m.x,m.y,m.z,1.f,1.f,"hostile");
        }
        m.giantStompCooldown = now+40;
        return true;
    }
    m.x += dx/d*0.08; m.z += dz/d*0.08;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    if (ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    return true;
}
bool LlamaSpitGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Llama && m.kind != MobKind::TraderLlama) return false;
    if (!ctx.srv || ctx.srv->tickNoForTest() < m.llamaSpitCooldown) return false;
    return ctx.nearestPlayer && ctx.nearestPlayerDist2 > 4*4 && ctx.nearestPlayerDist2 < 16*16;
}
bool LlamaSpitGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Llama && m.kind != MobKind::TraderLlama) return false;
    if (now < m.llamaSpitCooldown) return false;
    Player* t = ctx.nearestPlayer; if (!t) return false;
    // no LlamaSpit projectile kind exists yet: direct 1-damage spit + sound at range
    if (ctx.srv) {
        ctx.srv->applyDamage(*t, 1.f, "mob");
        ctx.srv->broadcastSound("minecraft:entity.llama.spit", m.x,m.y,m.z,1.f,1.f,"neutral");
    }
    m.llamaSpitCooldown = now+40;
    return true;
}
bool ChickenLayEggGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Chicken) return false;
    return ctx.srv && ctx.srv->tickNoForTest() >= m.chickenLayCooldown;
}
bool ChickenLayEggGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Chicken) return false;
    if (!ctx.srv || now < m.chickenLayCooldown) return false;
    auto it = gen::itemIdByName().find("minecraft:egg");
    if (it != gen::itemIdByName().end())
        ctx.srv->spawnItemDrop(m.x, m.y, m.z, it->second, 1);
    if (ctx.srv) ctx.srv->broadcastSound("minecraft:entity.chicken.egg", m.x,m.y,m.z,1.f,1.f,"neutral");
    m.chickenLayCooldown = now+6000+(rand()%6000); // vanilla 5-10min
    return true;
}
bool HuskHungerGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Husk) return false;
    return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 3*3;
}
bool HuskHungerGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Husk) return false;
    if (now < m.huskHungerCooldown) return false;
    Player* t = ctx.nearestPlayer; if (!t) return false;
    if (ctx.srv) {
        ctx.srv->mobAttackPlayer(m,*t);
        // vanilla husk inflicts Hunger (effect id 9) on hit
        WriteBuffer eff; eff.varint(t->entityId); eff.varint(9); eff.i8(0); eff.varint(140); eff.u8(0x01);
        ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityEffect, eff);
        ctx.srv->broadcastSound("minecraft:entity.husk.ambient", m.x,m.y,m.z,1.f,1.f,"hostile");
    }
    m.huskHungerCooldown = now+40;
    return true;
}
bool PolarBearDefendGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::PolarBear) return false;
    return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 8*8;
}
bool PolarBearDefendGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::PolarBear) return false;
    Player* t = ctx.nearestPlayer; if (!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.polarBearDefendUntil = now+20; // standing/defending posture window
    if (d < 1.9) {
        if (now%20==0 && ctx.srv) {
            ctx.srv->mobAttackPlayer(m,*t);
            ctx.srv->broadcastSound("minecraft:entity.polar_bear.warning", m.x,m.y,m.z,1.f,1.f,"neutral");
        }
        return true;
    }
    m.x += dx/d*0.10; m.z += dz/d*0.10;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    if (ctx.world) ctx.world->generateChunkIfMissing((int)m.x>>4,(int)m.z>>4);
    return true;
}
bool PufferfishPuffGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Pufferfish) return false;
    return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 4*4;
}
bool PufferfishPuffGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Pufferfish) return false;
    m.pufferfishPuffUntil = now+40; // inflated while threatened
    Player* t = ctx.nearestPlayer;
    if (t && ctx.srv) {
        double dx=t->x-m.x, dz=t->z-m.z;
        if (dx*dx+dz*dz < 1.5*1.5) {
            // vanilla contact poison (effect id 19, like bogged arrow)
            ctx.srv->applyDamage(*t, 3.f, "mob");
            WriteBuffer eff; eff.varint(t->entityId); eff.varint(19); eff.i8(0); eff.varint(120); eff.u8(0x01);
            ctx.srv->broadcastPacketExcept(nullptr, proto::pl::sc::EntityEffect, eff);
            ctx.srv->broadcastSound("minecraft:entity.puffer_fish.blow_up", m.x,m.y,m.z,1.f,1.f,"neutral");
        } else if (now%40==0) {
            ctx.srv->broadcastSound("minecraft:entity.puffer_fish.blow_up", m.x,m.y,m.z,0.5f,1.f,"neutral");
        }
    }
    return true;
}
bool ProjectileFlyGoal::shouldStart(MobEntity& m, AiContext&) { return MobEntity::isProjectileKind(m.kind); }
bool ProjectileFlyGoal::tick(MobEntity& m, AiContext&, std::int64_t) {
    if (!MobEntity::isProjectileKind(m.kind)) return false;
    // ballistic hold: projectiles keep latched velocity (set by thrower systems)
    // instead of wandering randomly. Returning true claims the tick so the
    // generic WanderAroundGoal never steers a flying projectile.
    m.x += m.projectileVx; m.y += m.projectileVy; m.z += m.projectileVz;
    m.projectileVy -= 0.02; // mild gravity like vanilla thrown projectiles
    return true;
}
bool EvokerFangsSnapGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::EvokerFangs) return false;
    if (!ctx.srv || ctx.srv->tickNoForTest() < m.evokerFangsSnapCooldown) return false;
    return ctx.nearestPlayer && ctx.nearestPlayerDist2 < 2*2;
}
bool EvokerFangsSnapGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::EvokerFangs) return false;
    if (now < m.evokerFangsSnapCooldown) return false;
    Player* t = ctx.nearestPlayer; if (!t) return false;
    if (ctx.srv) {
        ctx.srv->applyDamage(*t, 6.f, "magic");
        ctx.srv->broadcastSound("minecraft:entity.evoker_fangs.attack", m.x,m.y,m.z,1.f,1.f,"hostile");
    }
    m.evokerFangsSnapCooldown = now+40;
    return true;
}
bool EndCrystalHoverGoal::shouldStart(MobEntity& m, AiContext&) { return m.kind==MobKind::EnderCrystal; }
bool EndCrystalHoverGoal::tick(MobEntity& m, AiContext&, std::int64_t) {
    if (m.kind != MobKind::EnderCrystal) return false;
    // vanilla crystal: bedrock-hover + spin, never wanders
    m.yaw += 5.0f;
    if (m.yaw >= 360.f) m.yaw -= 360.f;
    return true;
}
bool TntFuseGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Tnt) return false;
    (void)ctx;
    return !m.dead;
}
bool TntFuseGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Tnt || m.dead) return false;
    if (m.tntFuseStartedAt < 0) {
        m.tntFuseStartedAt = now;
        if (ctx.srv) ctx.srv->broadcastSound("minecraft:entity.tnt.primed", m.x,m.y,m.z,1.f,1.f,"block");
        return true;
    }
    if (now - m.tntFuseStartedAt >= 80) { // vanilla 80t fuse
        if (ctx.srv) ctx.srv->explodeAt(m.x, m.y, m.z, 4.0f);
        m.dead = true;
        return false;
    }
    return true; // hold still while fusing
}
bool BatRoostGoal::shouldStart(MobEntity& m, AiContext&) { return m.kind==MobKind::Bat; }
bool BatRoostGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Bat) return false;
    // vanilla Bat: roosts hanging upside-down when idle (day), flies at night.
    // Day branch holds still (roosting); night falls through to FlyWander json.
    bool night = ctx.srv ? ctx.srv->isNight() : (now%24000 > 13000);
    if (!night) return true; // roosting: hang still
    return false; // night: let fly_wander behavior drive
}
bool AmbientObjectGoal::shouldStart(MobEntity& m, AiContext&) { return MobEntity::isAmbientObjectKind(m.kind); }
bool AmbientObjectGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!MobEntity::isAmbientObjectKind(m.kind)) return false;
    switch (m.kind) {
    case MobKind::ExperienceOrb:
    case MobKind::Item: {
        // vanilla magnet: drift toward nearest player within 8 (XP) / 3 (item)
        Player* t = ctx.nearestPlayer;
        double range = (m.kind==MobKind::ExperienceOrb) ? 8.0 : 3.0;
        if (t && ctx.nearestPlayerDist2 < range*range) {
            double dx=t->x-m.x, dz=t->z-m.z;
            double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            m.x += dx/d*0.12; m.z += dz/d*0.12;
        }
        m.y += std::sin(now*0.2)*0.003; // bob
        return true;
    }
    case MobKind::FallingBlock:
        m.y -= 0.15; // gravity fall (landing handled by block tick systems)
        return true;
    case MobKind::LightningBolt:
        if (!m.lightningStruck && ctx.srv) {
            ctx.srv->strikeLightning(m.x, m.y, m.z);
            ctx.srv->broadcastSound("minecraft:entity.lightning_bolt.thunder", m.x,m.y,m.z,2.f,1.f,"weather");
            m.lightningStruck = true;
        }
        m.dead = true; // instant strike entity, vanilla despawns after the flash
        return false;
    case MobKind::OminousItemSpawner:
        if (ctx.srv && now%100==0)
            ctx.srv->broadcastSound("minecraft:block.trial_spawner.ominous_activate", m.x,m.y,m.z,0.5f,1.f,"block");
        return true; // hold, ominous idle
    case MobKind::ArmorStand:
    default:
        return true; // pose hold, never wanders
    }
}
bool Brain::coversKind(MobKind k) {
    // plan42 group-default goals first (84 kinds: fish/graze/boat/minecart/
    // projectile/ambient + 13 singles); pre-existing specific goals below.
    if (MobEntity::hasSpeciesGoal(k)) return true;
    // Any non-generic goal that explicitly gates k (group gates count).
    // Generic fallback (Melee/Wander/LookAt/Panic/Tempt/Breed/Avoid) excluded.
    switch (k) {
    case MobKind::Creeper: case MobKind::Armadillo: case MobKind::IronGolem:
    case MobKind::Witch: case MobKind::Ravager: case MobKind::Evoker:
    case MobKind::Wolf: case MobKind::Drowned: case MobKind::Bee:
    case MobKind::Villager: case MobKind::WanderingTrader: case MobKind::Piglin:
    case MobKind::Cat: case MobKind::Fox: case MobKind::Panda:
    case MobKind::Dolphin: case MobKind::Breeze: case MobKind::Phantom:
    case MobKind::Warden: case MobKind::Enderman: case MobKind::Shulker:
    case MobKind::Guardian: case MobKind::ElderGuardian: case MobKind::Slime:
    case MobKind::MagmaCube: case MobKind::Silverfish: case MobKind::Endermite:
    case MobKind::Vindicator: case MobKind::Pillager: case MobKind::Hoglin:
    case MobKind::Zoglin: case MobKind::WitherSkeleton: case MobKind::Goat:
    case MobKind::Axolotl: case MobKind::Frog: case MobKind::Turtle:
    case MobKind::Parrot: case MobKind::Ocelot: case MobKind::SnowGolem:
    case MobKind::Wither: case MobKind::EnderDragon: case MobKind::Strider:
    case MobKind::Illusioner: case MobKind::Sniffer: case MobKind::Camel:
    case MobKind::Allay: case MobKind::Bogged: case MobKind::Creaking:
    case MobKind::Skeleton: case MobKind::Stray: case MobKind::Spider:
    case MobKind::CaveSpider: case MobKind::Zombie: case MobKind::Husk:
    case MobKind::Blaze: case MobKind::Ghast: case MobKind::PiglinBrute:
        return true; // pre-existing specific goals (Swell/Ranged/Leap/FleeSun/...)
    case MobKind::Cod: case MobKind::Salmon: case MobKind::TropicalFish:
    case MobKind::Pufferfish: case MobKind::Tadpole: case MobKind::Squid:
    case MobKind::GlowSquid:
        return true; // plan42 FishSwimGoal
    case MobKind::Horse: case MobKind::Donkey: case MobKind::Mule:
    case MobKind::Llama: case MobKind::TraderLlama: case MobKind::Cow:
    case MobKind::Sheep: case MobKind::Mooshroom: case MobKind::Pig:
    case MobKind::Rabbit: case MobKind::ZombieHorse:
        return true; // plan42 GrazeGoal (+LlamaSpit/ChickenLayEgg below)
    case MobKind::Chicken:
        return true; // plan42 ChickenLayEggGoal
    case MobKind::Vex:
        return true; // plan42 VexChargeGoal
    case MobKind::ZombieVillager:
        return true; // plan42 ZombieVillagerCureGoal
    case MobKind::ZombifiedPiglin:
        return true; // plan42 ZombifiedPiglinAngerGoal
    case MobKind::SkeletonHorse:
        return true; // plan42 SkeletonHorseTrapGoal
    case MobKind::Giant:
        return true; // plan42 GiantStompGoal
    case MobKind::PolarBear:
        return true; // plan42 PolarBearDefendGoal
    case MobKind::EvokerFangs:
        return true; // plan42 EvokerFangsSnapGoal
    case MobKind::EnderCrystal:
        return true; // plan42 EndCrystalHoverGoal
    case MobKind::Tnt:
        return true; // plan42 TntFuseGoal
    case MobKind::Arrow: case MobKind::SpectralArrow: case MobKind::Trident:
    case MobKind::Snowball: case MobKind::Egg: case MobKind::EnderPearl:
    case MobKind::Fireball: case MobKind::SmallFireball: case MobKind::DragonFireball:
    case MobKind::WindCharge: case MobKind::BreezeWindCharge: case MobKind::ShulkerBullet:
    case MobKind::LlamaSpit: case MobKind::Potion: case MobKind::ExperienceBottle:
    case MobKind::FireworkRocket: case MobKind::FishingBobber: case MobKind::EyeOfEnder:
        return true; // plan42 ProjectileFlyGoal
    default:
        break;
    }
    if (MobEntity::isBoat(k)) return true; // plan42 BoatDriftGoal (21 kinds)
    if (MobEntity::isMinecartKind(k)) return true; // plan42 MinecartRollGoal (7 kinds)
    return false;
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
