// AI goal/sensor implementations. Movement uses the A* pathfinder when the
// target is far and direct steering when close (vanilla hybrid behaviour).
#include "AiBrain.hpp"
#include "AiBrainInternal.hpp"
#include "BehaviorTree.hpp"
#include "GameServer.hpp"
#include "AiBehaviorSupport.hpp"
#include "MobGrounding.hpp"
#include "MetadataTypes.hpp"
#include "MobBehaviorSpec.hpp"
#include "Particles.hpp"
#include "GameServerHelpers.hpp"
#include "../proto/Ids.hpp"
#include "../generated/BlockStates.hpp"
#include <optional>
#include <string_view>
#include <utility>

namespace cppfm {

namespace {
using namespace ai_detail;
thread_local const MobEntity* activeBrainMob = nullptr;

class ActiveBrainMobScope final {
public:
    explicit ActiveBrainMobScope(const MobEntity& mob) noexcept
        : previousMob_(activeBrainMob) {
        activeBrainMob = &mob;
    }

    ~ActiveBrainMobScope() {
        activeBrainMob = previousMob_;
    }

    ActiveBrainMobScope(const ActiveBrainMobScope&) = delete;
    ActiveBrainMobScope& operator=(const ActiveBrainMobScope&) = delete;

private:
    const MobEntity* previousMob_;
};

std::optional<AiPlayerSnapshot> playerView(const AiContext& ctx,
                                           const MobEntity& mob,
                                           Player* candidate) {
    AiPlayerSnapshot view;
    return snapshotPlayer(mob, ctx, candidate, view)
        ? std::optional<AiPlayerSnapshot>(view)
        : std::nullopt;
}

std::optional<AiPlayerSnapshot> temptingPlayerView(AiContext& ctx,
                                                  const MobEntity& mob) {
    return playerView(ctx, mob, ctx.temptingPlayer);
}

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
} // namespace

namespace ai_brain_detail {

std::optional<AiPlayerSnapshot> nearestPlayerView(AiContext& ctx,
                                                  const MobEntity& mob) {
    return playerView(ctx, mob, ctx.nearestPlayer);
}

std::optional<AiPlayerSnapshot> dimensionPlayer(const AiContext& ctx,
                                                const MobEntity& mob,
                                                Player* candidate) {
    return playerView(ctx, mob, candidate);
}

} // namespace ai_brain_detail

using ai_brain_detail::dimensionPlayer;
using ai_brain_detail::nearestPlayerView;
using ai_brain_detail::raycastObstructed;

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
    ctx.resetPerception();
    if (!ctx.srv) return;

    // Do not hold the mob lock while taking Player::stateMtx.  The session
    // path can legitimately hold a player lock before it queues a mob
    // mutation.  Capture the mob observation first, then release it before
    // walking the player snapshot; this also makes the distance calculation
    // internally consistent for this sensor pass.
    const MobSnapshot mobView = snapshotMob(m);
    ctx.playerOwners = ctx.srv->playersSnapshot();
    ctx.playerViews.reserve(ctx.playerOwners.size());
    for (const auto& p : ctx.playerOwners) {
        if (!p) continue;

        AiPlayerSnapshot view;
        view.player = p.get();
        {
            // Session handlers may update movement, inventory, and game mode
            // concurrently.  Copy the complete target observation under one
            // lock so no goal ever combines fields from different moments.
            std::lock_guard lock(p->stateMtx);
            view.dimension = p->dimension;
            view.entityId = p->entityId;
            view.gamemode = p->gamemode;
            view.inPlay = p->inPlay;
            view.dead = p->dead;
            view.isSprinting = p->isSprinting;
            view.heldSlot = p->heldSlot;
            view.x = p->x;
            view.y = p->y;
            view.z = p->z;
            view.yaw = p->yaw;
            view.pitch = p->pitch;
            if (view.heldSlot >= 0 && view.heldSlot < 9) {
                const auto& held = p->inv[36 + view.heldSlot];
                view.heldItemId = held.itemId;
                view.heldItemEmpty = held.empty();
            }
            for (int i = 5; i <= 8 && i < static_cast<int>(p->inv.size()); ++i) {
                if (!p->inv[i].empty() &&
                    p->inv[i].name() == "minecraft:carved_pumpkin") {
                    view.hasPumpkin = true;
                    break;
                }
            }
        }
        const auto viewIndex = ctx.playerViews.size();
        ctx.playerViews.push_back(view);

        if (canonicalDimension(mobView.dimension) !=
                canonicalDimension(view.dimension) ||
            !view.inPlay || view.dead ||
            view.gamemode == 1 || view.gamemode == 3)
            continue;
        const double dx = view.x - mobView.x, dz = view.z - mobView.z;
        const double d2 = dx * dx + dz * dz;
        if (d2 < ctx.nearestPlayerDist2) {
            ctx.nearestPlayerDist2 = d2;
            ctx.nearestPlayer = view.player;
            ctx.nearestPlayerViewIndex = viewIndex;
        }
        // temptation: player holds breeding item for this mob kind
        const auto foodId = MobEntity::breedingItemFor(mobView.kind);
        if (foodId && d2 < 12 * 12 && !view.heldItemEmpty &&
            view.heldItemId == foodId) {
            ctx.temptingPlayer = view.player;
            ctx.temptingPlayerViewIndex = viewIndex;
        }
    }
}

bool PanicGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (!ctx.srv) return false;
    const std::int64_t now = ctx.srv->tickNoForTest();
    return m.health > 0 &&
        now - ctx.lastHurtTick.load(std::memory_order_acquire) < 100;
}

bool PanicGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    // run away from nearest player at 1.6x speed
    const auto target = nearestPlayerView(ctx, m);
    if (!target) return false;
    const double dx = m.x - target->x;
    const double dz = m.z - target->z;
    const double d = std::sqrt(dx * dx + dz * dz) + 1e-6;
    m.yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / 3.14159 - 90.0);
    m.x += dx / d * 0.14;
    m.z += dz / d * 0.14;
    groundSnap(ctx, m);
    return true;
}

bool MeleeAttackGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    const auto target = nearestPlayerView(ctx, m);
    const auto* tgt = target ? &*target : nullptr;
    if (!tgt) return false;
    const double dx = tgt->x - m.x, dz = tgt->z - m.z;
    const double dist = std::sqrt(dx * dx + dz * dz);
    if (dist > std::max(24.0, perceiveDist(m.kind))) return false;
    if (dist < 1.9) {
        if (now % 20 == 0 && ctx.srv)
            withoutMobStateLock(m, [&] {
                ctx.srv->mobAttackPlayer(m, *tgt->player);
            });
        return true;
    }
    // pathfind occasionally, follow path otherwise
    if (ctx.pathIdx >= ctx.path.size() ||
        std::abs(ctx.path.back().x - static_cast<std::int32_t>(tgt->x)) > 3 ||
        std::abs(ctx.path.back().z - static_cast<std::int32_t>(tgt->z)) > 3) {
        World* world = dimensionWorld(ctx, m);
        if (!world) return false;
        ai::Pathfinder::Result res;
        const double mobX = m.x, mobY = m.y, mobZ = m.z;
        withoutMobStateLock(m, [&] {
            ai::Pathfinder pf(*world);
            res = pf.find(static_cast<std::int32_t>(std::floor(mobX)),
                          static_cast<std::int32_t>(std::floor(mobY)),
                          static_cast<std::int32_t>(std::floor(mobZ)),
                          static_cast<std::int32_t>(std::floor(tgt->x)),
                          static_cast<std::int32_t>(std::floor(tgt->y)),
                          static_cast<std::int32_t>(std::floor(tgt->z)), 800);
        });
        ctx.path = std::move(res.points);
        ctx.pathIdx = res.found ? 1 : 0;
        if (!res.found) {
            // fall back to straight steering
            m.tx = tgt->x; m.tz = tgt->z; m.hasTarget = true;
            const double inv = 1.0 / dist;
            m.x += dx * inv * 0.09;
            m.z += dz * inv * 0.09;
            groundSnap(ctx, m);
            return true;
        }
    }
    stepAlongPath(m, ctx, 0.10);
    groundSnap(ctx, m);
    return true;
}

bool TemptGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    const auto target = temptingPlayerView(ctx, m);
    const auto* t = target ? &*target : nullptr;
    if (!t) return false;
    const double dx = t->x - m.x, dz = t->z - m.z;
    const double d = std::sqrt(dx * dx + dz * dz);
    if (d < 2.5) { m.hasTarget = false; return true; }   // sit near player
    const double inv = 1.0 / (d + 1e-6);
    m.yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / 3.14159 - 90.0);
    m.x += dx * inv * 0.07;
    m.z += dz * inv * 0.07;
    groundSnap(ctx, m);
    return true;
}

bool BreedGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (!m.inLove || ctx.srv==nullptr) return false;
    if (ctx.srv->tickNoForTest() < m.breedCooldownUntil) return false;
    if (MobEntity::isBaby(m)) return false;
    std::shared_ptr<MobEntity> partner;
    if (!withoutMobStateLock(m, [&] {
            partner = ctx.srv->findLovePartner(m);
        })) return false;
    if (!partner) return false;
    MobSnapshot partnerState;
    if (!withoutMobStateLock(m, [&] {
            partnerState = snapshotMob(*partner);
        })) return false;
    return sameDimension(m, partnerState);
}

bool BreedGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!ctx.srv) return false;
    GameServer& srv = *ctx.srv;
    if (!m.inLove || now < m.breedCooldownUntil) return false;
    if (MobEntity::isBaby(m)) { m.inLove=false; return false; }
    // wait 30 ticks after entering love (love 600t -> hearts)
    if (now < m.loveUntilTick - 30*20 + 30) return true;
    std::shared_ptr<MobEntity> partner;
    if (!withoutMobStateLock(m, [&] {
            partner = srv.findLovePartner(m);
        })) return true;
    if (!partner) return true;
    if (partner.get() == &m) return true;
    MobSnapshot partnerState;
    if (!withoutMobStateLock(m, [&] {
            partnerState = snapshotMob(*partner);
        })) return true;
    if (!sameDimension(m, partnerState)) return true; // keep waiting for a same-dimension partner
    double dx = partnerState.x - m.x, dz = partnerState.z - m.z;
    double d2 = dx*dx + dz*dz;
    if (d2 > 4.0) { // >2 blocks: move towards partner (plan14 §3 moveTo)
        double d = std::sqrt(d2) + 1e-6;
        m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159 -90.0);
        m.x += dx/d * 0.09;
        m.z += dz/d * 0.09;
        groundSnap(ctx, m);
        const auto mobId = m.entityId;
        const auto mobDimension = canonicalDimension(m.dimension);
        if (!withoutMobStateLock(m, [&] {
                // Pair operations must start with no current-Mob lock.  Two
                // concurrent brains can then use std::scoped_lock's
                // deadlock-free acquisition instead of each holding its own
                // Mob lock while waiting for the other.
                std::scoped_lock pairLock(*m.stateMtx, *partner->stateMtx);
                if (m.entityId == mobId &&
                    canonicalDimension(m.dimension) == mobDimension &&
                    canonicalDimension(partner->dimension) == mobDimension &&
                    partner->entityId == partnerState.entityId &&
                    !partner->dead && partner->inLove) {
                    partner->x += -dx/d * 0.04;
                    partner->z += -dz/d * 0.04;
                }
            })) return false;
        return true;
    }

    // Reserve the pair atomically before invoking the spawn callback.  The
    // callback can re-enter the server and another thread must not be able
    // to observe both animals as available and create a second baby.
    double partnerX = 0.0, partnerZ = 0.0;
    std::int32_t partnerId = 0;
    const std::int8_t breedDimension = canonicalDimension(m.dimension);
    bool pairReserved = false;
    const auto mobId = m.entityId;
    if (!withoutMobStateLock(m, [&] {
            std::scoped_lock pairLock(*m.stateMtx, *partner->stateMtx);
            if (m.entityId == mobId &&
                canonicalDimension(m.dimension) == breedDimension &&
                canonicalDimension(partner->dimension) == breedDimension &&
                partner->entityId == partnerState.entityId &&
                !partner->dead && partner->inLove && !m.dead && m.inLove &&
                now >= m.breedCooldownUntil &&
                now >= partner->breedCooldownUntil) {
                partnerX = partner->x;
                partnerZ = partner->z;
                partnerId = partner->entityId;
                m.inLove = false;
                partner->inLove = false;
                m.breedCooldownUntil = now + 6000;
                partner->breedCooldownUntil = now + 6000;
                pairReserved = true;
            }
        })) return true;
    if (!pairReserved) return true;

    const double bx = (m.x + partnerX) / 2.0;
    const double bz = (m.z + partnerZ) / 2.0;
    const double breedY = m.y;
    auto baby = std::make_shared<MobEntity>();
    baby->entityId = ctx.srv->nextEntityId();
    baby->kind = m.kind;
    baby->health = mobStats(m.kind).maxHealth;
    baby->age = -24000; // 20 min vanilla
    baby->x = bx; baby->y = m.y; baby->z = bz;
    baby->dimension = breedDimension;
    jvm::MobSpawnAdmission admission(
        srv.jvmRuntime(), *baby, &cancelMobSpawnOutsideMobStateLock);
    bool spawnAllowed = false;
    if (!withoutMobStateLock(m, [&] {
            spawnAllowed = admission.approve(baby->x, baby->y, baby->z);
        })) return false;
    if (!spawnAllowed) {
        // A cancelled spawn consumes no breeding transaction.  Restore only
        // the reservation we made; do not overwrite a callback's unrelated
        // state changes.
        if (!withoutMobStateLock(m, [&] {
                std::scoped_lock pairLock(*m.stateMtx, *partner->stateMtx);
                if (m.entityId == mobId &&
                    canonicalDimension(m.dimension) == breedDimension &&
                    partner->entityId == partnerId &&
                    !m.inLove && !partner->inLove &&
                    m.breedCooldownUntil == now + 6000 &&
                    partner->breedCooldownUntil == now + 6000) {
                    m.inLove = true;
                    partner->inLove = true;
                    m.breedCooldownUntil = 0;
                    partner->breedCooldownUntil = 0;
                }
            })) return false;
        return false;
    }
    bool reservationValid = false;
    if (!withoutMobStateLock(m, [&] {
            std::scoped_lock pairLock(*m.stateMtx, *partner->stateMtx);
            reservationValid = m.entityId == mobId && !m.dead &&
                partner->entityId == partnerId && !partner->dead &&
                canonicalDimension(m.dimension) == breedDimension &&
                canonicalDimension(partner->dimension) == breedDimension &&
                !m.inLove && !partner->inLove &&
                m.breedCooldownUntil == now + 6000 &&
                partner->breedCooldownUntil == now + 6000;
        })) return false;
    if (!reservationValid) return false;
    bool added = false;
    if (!withoutMobStateLock(m, [&] {
            added = srv.addMob(baby);
            if (!added) return;
            admission.commit();
            srv.broadcastMobSpawn(*baby);
        })) return false;
    if (!added) return false;
    // xp 1-7
    const auto xp = static_cast<std::int32_t>(1 + (nextRandom()%7));
    withoutMobStateLock(m, [&] {
        srv.spawnXpOrbsFor(breedDimension, bx, breedY + 0.5, bz, xp, nullptr);
    });
    // hearts already via EntityEvent 18 in tryBreedFeed, but also broadcast here
    {
        WriteBuffer st; st.i32(m.entityId); st.i8(18);
        WriteBuffer st2; st2.i32(partnerId); st2.i8(18);
        withoutMobStateLock(m, [&] {
            srv.broadcastPacketExceptInDimension(
                breedDimension, nullptr, proto::pl::sc::EntityEvent, st);
            srv.broadcastPacketExceptInDimension(
                breedDimension, nullptr, proto::pl::sc::EntityEvent, st2);
        });
    }
    {
        // The sensor already captured all players under Player::stateMtx in
        // this tick.  Reusing those views avoids taking Player locks in the
        // Mob -> Player order during the callback fan-out.
        Player* best = nullptr; double bestDist=64;
        for (const auto& view : ctx.playerViews) {
            if (!sameDimension(m, view) || !view.inPlay || view.dead) continue;
            const double viewDx=view.x-bx, viewDz=view.z-bz;
            const double viewDistance2=viewDx*viewDx+viewDz*viewDz;
            if (viewDistance2<bestDist*bestDist) {
                bestDist=std::sqrt(viewDistance2);
                best=view.player;
            }
        }
        if (best) withoutMobStateLock(m, [&] { srv.onBredAnimals(best); });
    }
    return false;
}

bool WanderAroundGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!m.hasTarget) {
        const double ang = (nextRandom() / double(RAND_MAX)) * 6.28318;
        const double dist = 4 + (nextRandom() % 8);
        m.tx = m.x + std::cos(ang) * dist;
        m.tz = m.z + std::sin(ang) * dist;
        m.hasTarget = true;
        m.nextWanderAt = now + 3000 + nextRandom() % 4000;
        // build a short path
        World* world = dimensionWorld(ctx, m);
        if (!world) return false;
        const auto sx = static_cast<std::int32_t>(std::floor(m.x));
        const auto sy = static_cast<std::int32_t>(std::floor(m.y));
        const auto sz = static_cast<std::int32_t>(std::floor(m.z));
        const auto gx = static_cast<std::int32_t>(std::floor(m.tx));
        const auto gz = static_cast<std::int32_t>(std::floor(m.tz));
        ai::Pathfinder::Result res;
        withoutMobStateLock(m, [&] {
            ai::Pathfinder pf(*world);
            res = pf.find(sx, sy, sz, gx, sy, gz, 300);
        });
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
    groundSnap(ctx, m);
    return true;
}

bool LookAtPlayerGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    const auto target = nearestPlayerView(ctx, m);
    if (!target) return false;
    const double dx = target->x - m.x;
    const double dz = target->z - m.z;
    m.yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / 3.14159 - 90.0);
    return ctx.nearestPlayerDist2 < 8 * 8;
}

static float wrapDegrees(float v) {
    while (v <= -180) v += 360;
    while (v > 180) v -= 360;
    return v;
}
bool ai_brain_detail::raycastObstructed(
    World* w, double x0, double y0, double z0,
    double x1, double y1, double z1) {
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
static bool isPlayerLookingAtCreaking(const AiPlayerSnapshot& p,
                                      MobEntity& cr, World* w) {
    const MobBehaviorSpec* spec = mobBehaviorSpec(MobKind::Creaking);
    if (!spec) return false;
    if (p.gamemode==1 || p.gamemode==3 || p.hasPumpkin) return false;
    double dx = cr.x - p.x;
    double dy = (cr.y+0.9) - (p.y+1.62);
    double dz = cr.z - p.z;
    double dist = std::sqrt(dx*dx+dy*dy+dz*dz);
    const double gazeMinimumDistance = spec->gazeMinimumDistance();
    if (dist <= 0.0 || !spec->withinAuxiliaryRange(dist) || dist < gazeMinimumDistance) return false;
    double yawToMob = std::atan2(dz,dx)*180.0/3.1415926535 - 90.0;
    double pitchToMob = -std::asin(dy/dist)*180.0/3.1415926535;
    double dYaw = std::abs(wrapDegrees((float)(yawToMob - p.yaw)));
    double dPitch = std::abs((float)(pitchToMob - p.pitch));
    const double gazeAngle = spec->gazeAngle();
    if (gazeAngle == 0.0 || dYaw > gazeAngle || dPitch > gazeAngle) return false;
    bool obstructed = false;
    const double mobX = cr.x, mobY = cr.y, mobZ = cr.z;
    withoutMobStateLock(cr, [&] {
        obstructed = raycastObstructed(w, p.x, p.y+1.62, p.z,
                                       mobX, mobY+0.9, mobZ);
    });
    if (obstructed) return false;
    return true;
}
bool CreakingGoal::shouldStart(MobEntity& m, AiContext&) {
    return m.kind == MobKind::Creaking && mobBehaviorSpec(m.kind) != nullptr;
}
bool CreakingGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Creaking) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec || !ctx.srv || !dimensionWorld(ctx, m)) return false;
    // check any player looking => frozen
    bool frozen = false;
    for (const auto& pp : ctx.playerViews) {
        if (!sameDimension(m, pp) || !pp.inPlay || pp.dead) continue;
        if (isPlayerLookingAtCreaking(pp, m, dimensionWorld(ctx, m))) {
            frozen = true;
            break;
        }
    }
    m.creakingFrozen = frozen;
    const auto target = nearestPlayerView(ctx, m);
    m.creakingAlerted = target.has_value() &&
        spec->withinSecondaryThresholdSquared(ctx.nearestPlayerDist2);
    if (frozen) {
        // immobile, cannot be pushed/knocked; also do not attack
        return true;
    }
    if (!target) return false;
    const double dx = target->x - m.x, dz = target->z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    const double attackThreshold = spec->actionThreshold();
    if (attackThreshold != 0.0 && d < attackThreshold) {
        if (now % spec->actionCooldown() == 0)
            withoutMobStateLock(m, [&] {
                ctx.srv->mobAttackPlayer(m, *target->player);
            });
        return true;
    }
    if (!spec->withinActionRange(d)) return false;
    // pathfind occasionally
    if (ctx.pathIdx >= ctx.path.size() ||
        std::abs(ctx.path.back().x - (int)std::floor(target->x)) > 3 ||
        std::abs(ctx.path.back().z - (int)std::floor(target->z)) > 3) {
        World* world = dimensionWorld(ctx, m);
        if (!world) return false;
        ai::Pathfinder::Result res;
        const double mobX = m.x, mobY = m.y, mobZ = m.z;
        withoutMobStateLock(m, [&] {
            ai::Pathfinder pf(*world);
            res = pf.find((int)std::floor(mobX),(int)std::floor(mobY),
                          (int)std::floor(mobZ),
                          (int)std::floor(target->x),
                          (int)std::floor(target->y),
                          (int)std::floor(target->z),800);
        });
        ctx.path = std::move(res.points);
        ctx.pathIdx = res.found ? 1 : 0;
        if (!res.found) {
            m.yaw = (float)(std::atan2(dz,dx)*180.0/3.1415926535 - 90.0);
            m.x += dx/d * 0.14;
            m.z += dz/d * 0.14;
            groundSnap(ctx, m);
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
    groundSnap(ctx, m);
    return true;
}


Brain::~Brain() = default;
void Brain::setBehaviorTree(std::unique_ptr<BehaviorTree> t) {
    std::shared_ptr<BehaviorTree> replacement;
    if (t) replacement = std::shared_ptr<BehaviorTree>(std::move(t));
    std::lock_guard lock(behaviorTreeMtx_);
    behaviorTree_ = std::move(replacement);
}
bool Brain::hasBehaviorTree() const {
    std::lock_guard lock(behaviorTreeMtx_);
    return behaviorTree_ != nullptr;
}
void Brain::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    // A callback from a network/JVM operation can re-enter the server on the
    // same thread.  Re-entering any Brain would corrupt active_ and the
    // tick-owned AiContext, and re-entering a different mob would use the
    // outer mob's installed lock.  Let the outer tick finish instead.
    if (activeBrainMob != nullptr) return;
    ActiveBrainMobScope brainScope(m);

    if (ctx.srv) {
        // The tick loop currently seeds AiContext with the Overworld.  Make
        // the brain authoritative for the dimension before either the
        // behavior tree or goal code performs a world query.
        ctx.world = &ctx.srv->worldFor(m.dimension);
    }
    withoutMobStateLock(m, [&] {
        NearestPlayerSensor::update(m, ctx);
    });
    if (m.riderEntityId != -1) {
        // A ridden mob is controlled by its passenger; autonomous goals must
        // not keep applying stale navigation in parallel with vehicle input.
        // Stop the active goal on the transition into the mounted state, then
        // discard its path so dismounting starts from a fresh goal decision.
        if (active_ && running_) active_->stop(m, ctx);
        active_ = nullptr;
        running_ = false;
        ctx.path.clear();
        ctx.pathIdx = 0;
        m.hasTarget = false;
        return;
    }
    if (m.kind == MobKind::Armadillo) {
        const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
        if (spec && now - m.armadilloLastScanTick >= spec->scanInterval()) {
            m.armadilloLastScanTick = now;
            bool danger = false;
            const auto target = dimensionPlayer(ctx, m, ctx.nearestPlayer);
            const bool playerInRange = target &&
                spec->withinActionRangeSquared(ctx.nearestPlayerDist2);
            const bool recentlyHurt = spec->alertDuration() != 0 &&
                now - ctx.lastHurtTick.load(std::memory_order_acquire) <
                    spec->alertDuration();
            if (playerInRange) {
                if (target->isSprinting) danger = true;
                else if (recentlyHurt) danger = true;
                else if (m.health < mobStats(m.kind).maxHealth) danger = true;
                else if (spec->withinSecondaryThresholdSquared(ctx.nearestPlayerDist2)) danger = true;
            } else if (recentlyHurt) danger = true;
            if (danger && spec->alertDuration() != 0)
                m.armadilloDangerDetectedUntil = now + spec->alertDuration();
        }
        ctx.dangerDetectedRecently = now < m.armadilloDangerDetectedUntil;
        // also water immediate danger clear handled in goal tick, but keep TTL
    }
    std::shared_ptr<BehaviorTree> tree;
    {
        std::lock_guard lock(behaviorTreeMtx_);
        tree = behaviorTree_;
    }
    if (tree) {
        BTStatus s = tree->tick(m, ctx, now);
        if (s == BTStatus::Running || s == BTStatus::Success) {
            // A tree owns this tick.  Stop a legacy goal before returning so
            // a later tree failure cannot resume stale state unexpectedly.
            if (active_) {
                active_->stop(m, ctx);
                active_ = nullptr;
                running_ = false;
            }
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
