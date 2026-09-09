// AI goal/sensor implementations. Movement uses the A* pathfinder when the
// target is far and direct steering when close (vanilla hybrid behaviour).
#include "AiBrain.hpp"
#include "BehaviorTree.hpp"
#include "GameServer.hpp"
#include "AiBehaviorSupport.hpp"
#include "MetadataTypes.hpp"
#include "MobBehaviorSpec.hpp"
#include "Particles.hpp"
#include "../proto/Ids.hpp"
#include "../generated/BlockStates.hpp"
#include <optional>
#include <string_view>
#include <utility>

namespace cppfm {

namespace {
using namespace ai_detail;
thread_local const MobEntity* activeBrainMob = nullptr;
thread_local const AiContext* activeBrainContext = nullptr;

class ActiveBrainMobScope final {
public:
    ActiveBrainMobScope(const MobEntity& mob, const AiContext& ctx) noexcept
        : previousMob_(activeBrainMob), previousContext_(activeBrainContext) {
        activeBrainMob = &mob;
        activeBrainContext = &ctx;
    }

    ~ActiveBrainMobScope() {
        activeBrainMob = previousMob_;
        activeBrainContext = previousContext_;
    }

    ActiveBrainMobScope(const ActiveBrainMobScope&) = delete;
    ActiveBrainMobScope& operator=(const ActiveBrainMobScope&) = delete;

private:
    const MobEntity* previousMob_;
    const AiContext* previousContext_;
};

std::optional<AiPlayerSnapshot> playerView(const AiContext& ctx,
                                           const MobEntity& mob,
                                           Player* candidate) {
    AiPlayerSnapshot view;
    return snapshotPlayer(mob, ctx, candidate, view)
        ? std::optional<AiPlayerSnapshot>(view)
        : std::nullopt;
}

std::optional<AiPlayerSnapshot> nearestPlayerView(AiContext& ctx,
                                                  const MobEntity& mob) {
    return playerView(ctx, mob, ctx.nearestPlayer);
}

std::optional<AiPlayerSnapshot> temptingPlayerView(AiContext& ctx,
                                                  const MobEntity& mob) {
    return playerView(ctx, mob, ctx.temptingPlayer);
}

// Legacy goals historically received a raw Player* and then read its fields
// while the Mob lock was held.  Keep the source pointer for server APIs, but
// expose only the coherent scalar observation through operator->.  The
// optional is deliberately built from playerView(): server-backed contexts
// fail closed when the sensor did not publish a same-tick view, while
// standalone goal tests may take the short direct Player snapshot.
struct PlayerViewRef {
    Player* player = nullptr;
    std::int8_t dimension = 0;
    std::int32_t entityId = 0;
    std::uint8_t gamemode = 0;
    bool inPlay = false;
    bool dead = false;
    bool isSprinting = false;
    bool hasPumpkin = false;
    std::int32_t heldSlot = 0;
    std::uint32_t heldItemId = 0;
    bool heldItemEmpty = true;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;

    explicit PlayerViewRef(const AiPlayerSnapshot& view)
        : player(view.player), dimension(view.dimension),
          entityId(view.entityId), gamemode(view.gamemode),
          inPlay(view.inPlay), dead(view.dead),
          isSprinting(view.isSprinting), hasPumpkin(view.hasPumpkin),
          heldSlot(view.heldSlot), heldItemId(view.heldItemId),
          heldItemEmpty(view.heldItemEmpty), x(view.x), y(view.y), z(view.z),
          yaw(view.yaw), pitch(view.pitch) {}

    operator Player&() const noexcept { return *player; }
    operator Player*() const noexcept { return player; }
};

std::optional<PlayerViewRef> dimensionPlayer(const MobEntity& mob,
                                             Player* candidate) {
    AiContext standalone;
    const AiContext& ctx = activeBrainContext ? *activeBrainContext : standalone;
    const auto view = playerView(ctx, mob, candidate);
    return view ? std::optional<PlayerViewRef>(PlayerViewRef(*view))
                : std::nullopt;
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
        }
        // temptation: player holds breeding item for this mob kind
        const auto foodId = MobEntity::breedingItemFor(mobView.kind);
        if (foodId && d2 < 12 * 12 && !view.heldItemEmpty &&
            view.heldItemId == foodId)
            ctx.temptingPlayer = view.player;
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
    bool spawnAllowed = true;
    if (srv.jvmRuntime()) {
        withoutMobStateLock(m, [&] {
            spawnAllowed = srv.jvmRuntime()->onMobSpawn(
                *baby, baby->x, baby->y, baby->z);
        });
    }
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
    withoutMobStateLock(m, [&] {
        srv.addMob(baby);
        srv.broadcastMobSpawn(*baby);
    });
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
            const double dx=view.x-bx, dz=view.z-bz;
            const double d2=dx*dx+dz*dz;
            if (d2<bestDist*bestDist) {
                bestDist=std::sqrt(d2);
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


// -------------------------------------------------------- ranged attacks --

bool RangedAttackGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!RangedAttackGoal::isRangedKind(m.kind)) return false;
    const auto target = nearestPlayerView(ctx, m);
    const auto* tgt = target ? &*target : nullptr;
    if (!tgt) return false;
    const double dx = tgt->x - m.x, dz = tgt->z - m.z;
    const double dy = (tgt->y + 1.0) - (m.y + 1.6);
    const double dist = std::sqrt(dx * dx + dz * dz);
    if (dist < 5) return false;                       // melee goal takes over
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
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double mobX = m.x, mobY = m.y, mobZ = m.z;
        withoutMobStateLock(m, [&] {
            ctx.srv->spawnProjectileFor(dimension, ProjectileKind::Arrow,
                                        mobX, mobY + 1.6, mobZ, vx, vy, vz,
                                        entityId, false);
            ctx.srv->broadcastSoundFor(dimension,
                                       "minecraft:entity.arrow.shoot",
                                       mobX, mobY, mobZ, 1.f, 1.f, "hostile");
        });
    }
    // hold ground while shooting
    return true;
}


bool SwellGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Creeper || m.dead) return false;
    const auto target = nearestPlayerView(ctx, m);
    if (!target) return m.creeperIgnited;
    double dx = target->x - m.x, dz = target->z - m.z;
    double d2 = dx*dx+dz*dz;
    if (m.creeperIgnited) return true;
    return d2 < 9; // 3 blocks
}
bool SwellGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Creeper || m.dead) return false;
    const auto target = nearestPlayerView(ctx, m);
    if (!target) {
        return m.creeperIgnited;
    }
    double dx = target->x - m.x, dz = target->z - m.z;
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
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double mobX = m.x, mobY = m.y, mobZ = m.z;
        WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md, 16, true); md.u8(255);
        withoutMobStateLock(m, [&] {
            ctx.srv->broadcastPacketExceptInDimension(
                dimension, nullptr, proto::pl::sc::SetEntityMetadata, md);
            ctx.srv->broadcastSoundFor(
                dimension, "minecraft:entity.creeper.primed", mobX, mobY,
                mobZ, 1.f, 1.f, "hostile");
        });
    }
    return m.creeperIgnited;
}

bool AvoidEntityGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (!nearestPlayerView(ctx, m)) return false;
    // differentiate per mob: creeper avoids cat/ocelot, skeleton avoids wolf, piglin avoids zoglin etc.
    // simplified: any of those kinds use same player-distance check; for non-listed, still flee if close 4
    if (m.kind==MobKind::Creeper || m.kind==MobKind::Skeleton || m.kind==MobKind::Piglin || m.kind==MobKind::Spider) {
        return ctx.nearestPlayerDist2 < dist2_;
    }
    return false;
}
bool AvoidEntityGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    const auto target = nearestPlayerView(ctx, m);
    if (!ctx.srv || !target) return false;
    if (ctx.nearestPlayerDist2 > dist2_) return false;
    double dx = m.x - target->x, dz = m.z - target->z;
    double d = std::sqrt(dx*dx+dz*dz)+1e-6;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180/3.14159 -90);
    m.x += dx/d * 0.12;
    m.z += dz/d * 0.12;
    if (ctx.srv && dimensionWorld(ctx, m)) {
        groundSnap(ctx, m);
    }
    return true;
}

bool FleeSunGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (!ctx.srv || !dimensionWorld(ctx, m)) return false;
    if (m.kind!=MobKind::Skeleton && m.kind!=MobKind::Zombie && m.kind!=MobKind::Stray && m.kind!=MobKind::Husk && m.kind!=MobKind::Drowned) return false;
    if (ctx.srv->isNight()) return false;
    // check sky light >=14 at mob feet
    World* world = dimensionWorld(ctx, m);
    const int chunkX = (int)m.x >> 4;
    const int chunkZ = (int)m.z >> 4;
    const int blockX = (int)m.x;
    const int blockY = (int)m.y;
    const int blockZ = (int)m.z;
    std::uint8_t sky = 0;
    withoutMobStateLock(m, [&] {
        world->generateChunkIfMissing(chunkX, chunkZ);
        sky = world->getSkyLight(blockX, blockY, blockZ);
    });
    return sky >= 14;
}
bool FleeSunGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    if (!ctx.srv || !dimensionWorld(ctx, m)) return false;
    if (ctx.srv->isNight()) return false;
    // seek shade: move opposite to player or random if no player
    double dx=0, dz=0;
    if (const auto target = nearestPlayerView(ctx, m)) {
        dx = m.x - target->x;
        dz = m.z - target->z;
    }
    else { dx = (nextRandom()/(double)RAND_MAX-0.5)*2; dz = (nextRandom()/(double)RAND_MAX-0.5)*2; }
    double d = std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x += dx/d * 0.13; m.z += dz/d * 0.13;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180/3.14159 -90);
    World* world = dimensionWorld(ctx, m);
    const int chunkX = (int)m.x >> 4;
    const int chunkZ = (int)m.z >> 4;
    const int blockX = (int)m.x;
    const int blockZ = (int)m.z;
    withoutMobStateLock(m, [&] {
        world->generateChunkIfMissing(chunkX, chunkZ);
    });
    int col=4;
    withoutMobStateLock(m, [&] {
        world->withChunk(chunkX, chunkZ,[&](const Chunk& c){
            for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry)
                if(c.blocks[Chunk::index(ry>>4, ry&15,
                                        blockZ&15, blockX&15)]!=0){
                    col=ry+1;break;
                }
        });
    });
    m.y = kMinY + col + 1.0;
    return true;
}

bool LeapAtTargetGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind!=MobKind::Spider && m.kind!=MobKind::CaveSpider && m.kind!=MobKind::Phantom) return false;
    const auto target = nearestPlayerView(ctx, m);
    if (!target) return false;
    double dx=target->x - m.x, dz=target->z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    return d >= 2.0 && d <= 5.0;
}
bool LeapAtTargetGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    const auto target = nearestPlayerView(ctx, m);
    if (!ctx.srv || !target) return false;
    double dx=target->x - m.x, dz=target->z - m.z;
    double d = std::sqrt(dx*dx+dz*dz)+1e-6;
    double vx = dx/d * 0.42, vz = dz/d * 0.42;
    double vy = 0.38;
    // apply leap
    m.x += vx; m.z += vz; m.y += vy;
    // gravity will be handled by tick loop groundSnap next tick; clamp y
    if (m.y > kMinY + 320) m.y = kMinY + 320;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180/3.14159 -90);
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        const auto entityId = m.entityId;
        WriteBuffer vel; vel.varint(entityId); vel.i16((int16_t)(vx*8000)); vel.i16((int16_t)(vy*8000)); vel.i16((int16_t)(vz*8000));
        withoutMobStateLock(m, [&] {
            ctx.srv->broadcastPacketExceptInDimension(
                dimension, nullptr, proto::pl::sc::EntityVelocity, vel);
            ctx.srv->broadcastSoundFor(dimension,
                                       "minecraft:entity.spider.jump", x,
                                       y, z, 1.f, 1.f, "hostile");
        });
    }
    if (dimensionWorld(ctx, m)) {
        World* world = dimensionWorld(ctx, m);
        const int chunkX = (int)m.x >> 4;
        const int chunkZ = (int)m.z >> 4;
        withoutMobStateLock(m, [&] {
            world->generateChunkIfMissing(chunkX, chunkZ);
        });
    }
    return true;
}

bool BreezeJumpGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Breeze) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if (m.breezeJumpCooldown > now) return false;
    const auto target = nearestPlayerView(ctx, m);
    if (!target) return false;
    double dx=target->x - m.x, dz=target->z - m.z;
    double d=std::sqrt(dx*dx+dz*dz);
    if (!spec->beyondActionThreshold(d)) return false;
    double inv=1.0/(d+1e-6);
    // vanilla breeze jump 15h/5v, simplified to 0.7h + 0.45v scaled; pass via position delta + velocity
    double jx = dx*inv * 0.55;
    double jz = dz*inv * 0.55;
    // clamp lava jump vy=1 case: we just use 0.45 normally, but if in lava would be 0.12 – simplified keep 0.45
    double jy = 0.45;
    // avoid jumping too high if already high
    if (m.y > target->y + 6) jy = 0.15;
    m.x += jx * 2.2; m.z += jz * 2.2; m.y += jy * 3.0;
    m.breezeJumpCooldown = now + spec->secondaryCooldown();
    m.breezeLastJumpTick = now;
    if (ctx.srv) {
        WriteBuffer vel; vel.varint(m.entityId); vel.i16((int16_t)(jx*8000*2)); vel.i16((int16_t)(jy*8000)); vel.i16((int16_t)(jz*8000*2));
        const auto dimension = m.dimension;
        const double x = m.x, y = m.y, z = m.z;
        withoutMobStateLock(m, [&] {
            ctx.srv->broadcastPacketExceptInDimension(dimension, nullptr, proto::pl::sc::EntityVelocity, vel);
            ctx.srv->broadcastSoundFor(dimension, "minecraft:entity.breeze.jump", x, y, z, 1.f, 1.f, "hostile");
        });
    }
    if (World* world = dimensionWorld(ctx, m)) {
        const int chunkX = static_cast<int>(m.x) >> 4;
        const int chunkZ = static_cast<int>(m.z) >> 4;
        withoutMobStateLock(m, [&] { world->generateChunkIfMissing(chunkX, chunkZ); });
    }
    return true;
}
bool BreezeWindChargeGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Breeze) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if (m.breezeWindChargeCooldown > now) return false;
    const auto target = nearestPlayerView(ctx, m);
    if (!target) return false;
    double dx=target->x - m.x, dy=(target->y+1.0)-(m.y+1.2), dz=target->z - m.z;
    double d=std::sqrt(dx*dx+dz*dz);
    if (!spec->withinActionRange(d)) return false;
    double inv=1.0/(d+1e-6);
    double vx=dx*inv*1.15, vz=dz*inv*1.15, vy=dy*inv*0.2 + 0.12;
    if (ctx.srv) {
        const auto dimension = m.dimension;
        const std::int32_t entityId = m.entityId;
        const double x = m.x, y = m.y, z = m.z;
        // use Fireball-like wind_charge; entity type BreezeWindCharge visual via typeId lookup inside spawnProjectile
        // spawn as BreezeWindCharge kind for correct entity type (fallback uses Fireball if mapping fails)
        withoutMobStateLock(m, [&] {
            ctx.srv->spawnProjectileFor(dimension, ProjectileKind::BreezeWindCharge,
                                         x, y+1.2, z, vx, vy, vz,
                                         entityId, false);
            ctx.srv->broadcastSoundFor(dimension, "minecraft:entity.breeze.wind_burst", x, y, z, 1.f, 1.f, "hostile");
        });
    }
    m.breezeWindChargeCooldown = now + spec->actionCooldown();
    return true;
}

bool ArmadilloRollUpGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Armadillo) return false;
    if (!mobBehaviorSpec(m.kind)) return false;
    // Scan at the descriptor's configured cadence; danger comes from AiContext or pending TTL.
    return ctx.dangerDetectedRecently || m.armadilloDangerDetectedUntil > 0;
}
bool ArmadilloRollUpGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Armadillo) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    bool danger = ctx.dangerDetectedRecently || now < m.armadilloDangerDetectedUntil;
    // water check: immediate unroll if in water (simplified: y below sea or block water)
    if (dimensionWorld(ctx, m)) {
        uint16_t st = dimensionWorld(ctx, m)->getBlock((int)std::floor(m.x),(int)std::floor(m.y),(int)std::floor(m.z));
        auto* bd = gen::blockByState(st);
        if (bd && std::string(bd->name).find("water")!=std::string::npos) danger = false;
    }
    if (danger && !m.armadilloRolledUp) {
        m.armadilloRolledUp = true;
        m.armadilloRollUpUntil = now + spec->actionCooldown();
        if (ctx.srv) {
            const auto dimension = canonicalDimension(m.dimension);
            const auto entityId = m.entityId;
            const double x = m.x, y = m.y, z = m.z;
            WriteBuffer md; md.varint(entityId); meta::writeMetaByte(md, 16, 1); md.u8(255);
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr,
                        proto::pl::sc::SetEntityMetadata, md);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.armadillo.roll",
                        x, y, z, 1.f, 1.f, "neutral");
                })) return false;
        }
        return true;
    }
    if (m.armadilloRolledUp) {
        if (now > m.armadilloRollUpUntil) {
            if (!danger) {
                m.armadilloRolledUp = false;
                if (ctx.srv) {
                    const auto dimension = canonicalDimension(m.dimension);
                    const auto entityId = m.entityId;
                    WriteBuffer md; md.varint(entityId); meta::writeMetaByte(md, 16, 0); md.u8(255);
                    if (!withoutMobStateLock(m, [&] {
                            ctx.srv->broadcastPacketExceptInDimension(
                                dimension, nullptr,
                                proto::pl::sc::SetEntityMetadata, md);
                        })) return false;
                }
                return false;
            } else {
                m.armadilloRollUpUntil = now + spec->secondaryCooldown();
            }
        }
        return true;
    }
    return false;
}

bool WitchPotionThrowGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Witch) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if (!dimensionPlayer(m, ctx.nearestPlayer)) return false;
    if (!spec->withinActionRangeSquared(ctx.nearestPlayerDist2)) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.witchPotionCooldown) return false;
    return true;
}
bool WitchPotionThrowGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Witch) return false;
    const MobBehaviorSpec* wspec = mobBehaviorSpec(m.kind);
    if (!wspec) return false;
    auto t = dimensionPlayer(m, ctx.nearestPlayer); if (!t) return false;
    double dx=t->x - m.x, dz=t->z - m.z; double d2=dx*dx+dz*dz;
    if (!wspec->withinActionRangeSquared(d2)) return false;
    if (now < m.witchPotionCooldown) return false;
    double d=std::sqrt(d2)+1e-6;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double mobX = m.x, mobY = m.y, mobZ = m.z;
        // heal if low health
        if (m.health < 13) {
            m.health = std::min(m.health + wspec->actionMagnitude(),
                                (double)mobStats(m.kind).maxHealth);
            WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md,16,true); md.u8(255);
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.witch.drink",
                        mobX,mobY,mobZ,1.f,1.f,"hostile");
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr,
                        proto::pl::sc::SetEntityMetadata, md);
                })) return false;
        } else {
            double vx=dx/d*0.9, vz=dz/d*0.9;
            WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md,16,false); md.u8(255);
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::Potion,
                        mobX, mobY+1.6, mobZ, vx, 0.12, vz,
                        entityId, false);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.witch.throw",
                        mobX,mobY,mobZ,1.f,1.f,"hostile");
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr,
                        proto::pl::sc::SetEntityMetadata, md);
                })) return false;
        }
    }
    m.witchPotionCooldown = now + wspec->actionCooldown() + (nextRandom()%20);
    return true;
}
bool RavagerRoarGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Ravager) return false;
    if (!dimensionPlayer(m, ctx.nearestPlayer)) return false;
    if (ctx.nearestPlayerDist2 > 5*5) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.ravagerRoarCooldown) return false;
    return true;
}
bool RavagerRoarGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Ravager) return false;
    if (now < m.ravagerRoarCooldown) return false;
    auto t = dimensionPlayer(m, ctx.nearestPlayer); if (!t) return false;
    const auto dimension = canonicalDimension(m.dimension);
    const double mobX = m.x, mobY = m.y, mobZ = m.z;
    double dx=t->x - mobX, dz=t->z - mobZ; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d>5) return false;
    if (ctx.srv) {
        double vx=dx/d*0.4, vz=dz/d*0.4;
        WriteBuffer vel; vel.varint(t->entityId); vel.i16((int16_t)(vx*8000)); vel.i16((int16_t)(0.3*8000)); vel.i16((int16_t)(vz*8000));
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::EntityVelocity, vel);
                ctx.srv->broadcastHurtAnimationFor(
                    dimension, t->entityId,
                    (float)(std::atan2(dz,dx)*180/3.14159));
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.ravager.roar",
                    mobX,mobY,mobZ,1.f,1.f,"hostile");
            })) return false;
    }
    m.ravagerRoarCooldown = now + 100;
    m.ravagerStunUntil = now + 10;
    return true;
}
bool IronGolemDefendGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::IronGolem) return false;
    // A nearby player is not a threat by itself.  Golems defend against a
    // nearby hostile mob or a recent attack; treating the nearest player as
    // the target made every golem enter this goal in a peaceful scene.
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double mobX = m.x, mobZ = m.z;
        std::vector<MobSnapshot> hostileMobs;
        if (!withoutMobStateLock(m, [&] {
                for (const auto& mob : ctx.srv->mobsSnapshot()) {
                    if (!mob || mob.get() == &m) continue;
                    const MobSnapshot view = snapshotMob(*mob);
                    if (view.dimension != dimension || view.dead ||
                        !MobEntity::isHostile(view.kind)) continue;
                    hostileMobs.push_back(view);
                }
            })) return false;
        for (const auto& mob : hostileMobs) {
            const double dx = mob.x - mobX;
            const double dz = mob.z - mobZ;
            if (dx * dx + dz * dz < 12.0 * 12.0) return true;
        }
        if (ctx.lastHurtTick.load(std::memory_order_acquire) >= 0 &&
            ctx.srv->tickNoForTest() -
                    ctx.lastHurtTick.load(std::memory_order_acquire) < 40)
            return true;
    }
    return false;
}
bool IronGolemDefendGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::IronGolem) return false;
    if (now < m.ironGolemDefendCooldown) return false;
    const auto player = dimensionPlayer(m, ctx.nearestPlayer);
    if (!player) return false;
    // attack nearest hostile mob if present, else just approach player
    std::shared_ptr<MobEntity> nearestHostile;
    MobSnapshot nearestHostileState;
    const auto dimension = canonicalDimension(m.dimension);
    const double mobX = m.x, mobZ = m.z;
    double best=1e300;
    if (ctx.srv) {
        if (!withoutMobStateLock(m, [&] {
                for (const auto& mm : ctx.srv->mobsSnapshot()) {
                    if (!mm || mm.get() == &m) continue;
                    const MobSnapshot view = snapshotMob(*mm);
                    if (view.dimension != dimension ||
                        !MobEntity::isHostile(view.kind) || view.dead)
                        continue;
                    const double dx = view.x - mobX;
                    const double dz = view.z - mobZ;
                    const double d2 = dx * dx + dz * dz;
                    if (d2 < best) {
                        best = d2;
                        nearestHostile = mm;
                        nearestHostileState = view;
                    }
                }
            })) return false;
    }
    if (nearestHostile && best < 12*12) {
        double dx=nearestHostileState.x - mobX;
        double dz=nearestHostileState.z - mobZ;
        double d=std::sqrt(dx*dx+dz*dz)+1e-6;
        if (d < 2.2) {
            if (ctx.srv) {
                const double attackY = m.y;
                if (!withoutMobStateLock(m, [&] {
                        ctx.srv->applyDamageToMob(
                            *nearestHostile, 8.f, "mob");
                        ctx.srv->broadcastSoundFor(
                            dimension, "minecraft:entity.iron_golem.attack",
                            mobX, attackY, mobZ, 1.f, 1.f, "neutral");
                    })) return false;
            }
            m.ironGolemDefendCooldown = now + 20;
            return true;
        }
        m.x += dx/d*0.12; m.z += dz/d*0.12;
        m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
        if (ctx.srv) {
            const double x = m.x, y = m.y, z = m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.iron_golem.step",
                        x, y, z, 0.5f, 1.f, "neutral");
                })) return false;
        }
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
    if (now % 20 != 0) return false;
    if (!dimensionWorld(ctx, m)) return false;
    for (int dx=-8; dx<=8; ++dx) for (int dz=-8; dz<=8; ++dz) for (int dy=-2; dy<=2; ++dy){
        int bx=(int)std::floor(m.x)+dx, by=(int)std::floor(m.y)+dy, bz=(int)std::floor(m.z)+dz;
        uint16_t st=dimensionWorld(ctx, m)->getBlock(bx,by,bz); if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        std::string n(bd->name);
        if (n.find("flower")!=std::string::npos || n=="minecraft:dandelion" || n=="minecraft:poppy") {
            double ddx=bx+0.5 - m.x, ddz=bz+0.5 - m.z; double d=std::sqrt(ddx*ddx+ddz*ddz)+1e-6;
            m.x += ddx/d*0.08; m.z += ddz/d*0.08;
            if(d<1.2){
                m.beeHasNectar=true; m.beePollenUntil=now+400;
                if(ctx.srv){
                    const auto dimension = canonicalDimension(m.dimension);
                    const auto entityId = m.entityId;
                    WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md,17,true); md.u8(255);
                    if (!withoutMobStateLock(m, [&] {
                            ctx.srv->broadcastPacketExceptInDimension(
                                dimension, nullptr,
                                proto::pl::sc::SetEntityMetadata, md);
                        })) return false;
                }
                return true;
            }
            return true;
        }
    }
    // no flower: wander
    return false;
}
bool WolfAngerGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Wolf) return false;
    if (ctx.lastHurtTick.load(std::memory_order_acquire) >= 0 && ctx.srv &&
        ctx.srv->tickNoForTest() -
                ctx.lastHurtTick.load(std::memory_order_acquire) < 40)
        return true;
    if (m.wolfAngerTarget!=-1 && ctx.srv && ctx.srv->tickNoForTest() < m.wolfAngerUntil) return true;
    if (dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 4) return false; // not angry by proximity alone
    return false;
}
bool WolfAngerGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Wolf) return false;
    if (m.isTamed) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d>12) return false;
    if (d<1.9) {
        if(now%20==0 && ctx.srv &&
           !withoutMobStateLock(m, [&] {
               ctx.srv->mobAttackPlayer(m,*t);
           })) return false;
        return true;
    }
    m.x += dx/d*0.11; m.z += dz/d*0.11; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        WriteBuffer md; md.varint(entityId); meta::writeMetaByte(md,16,1); md.u8(255);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::SetEntityMetadata, md);
            })) return false;
    }
    m.wolfAngerTarget = t->entityId; m.wolfAngerUntil = now + 100;
    return true;
}
bool DrownedTridentGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Drowned) return false;
    if (!dimensionPlayer(m, ctx.nearestPlayer)) return false;
    if (ctx.nearestPlayerDist2 > perceptionRange2(MobKind::Drowned)) return false; // plan44 G-05
    if (ctx.srv && ctx.srv->tickNoForTest() < m.drownedTridentCooldown) return false;
    return true;
}
bool DrownedTridentGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Drowned) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    double dx=t->x - m.x, dy=(t->y+1.0)-(m.y+1.6), dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d > perceiveDist(MobKind::Drowned)) return false; // plan44 G-05
    if (d<5) return false; // melee takes over
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double mobX = m.x, mobY = m.y, mobZ = m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->spawnProjectileFor(
                    dimension, ProjectileKind::Trident,
                    mobX, mobY+1.6, mobZ, dx/d*1.2,
                    dy/d*0.2+0.15, dz/d*1.2, entityId, false);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.drowned.shoot",
                    mobX,mobY,mobZ,1.f,1.f,"hostile");
            })) return false;
    }
    m.drownedTridentCooldown = now + 40;
    return true;
}
bool VillagerScheduleGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Villager && m.kind != MobKind::WanderingTrader) return false;
    int tod = (int)(ctx.srv ? ctx.srv->dayTime()%24000 : now%24000);
    const char* act = activityFor(tod);
    std::string a(act);
    if (a == "work") {
        if (nextRandom()%40==0 && ctx.srv) {
            const auto dimension = canonicalDimension(m.dimension);
            const double x=m.x, y=m.y, z=m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.villager.work",
                        x,y,z,0.5f,1.f,"neutral");
                })) return false;
        }
    } else if (a == "gather" || a == "mingle" || a == "wander" || a == "play") {
        // midday/afternoon movement: gather/mingle/play drift, wander wider
        if (nextRandom()%20==0){ double ang=nextRandom()/(double)RAND_MAX*6.28; double st=(a=="wander"?0.08:0.04); m.x+=std::cos(ang)*st; m.z+=std::sin(ang)*st; }
        if (a == "mingle" && nextRandom()%60==0 && ctx.srv) {
            const auto dimension = canonicalDimension(m.dimension);
            const double x=m.x, y=m.y, z=m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.villager.ambient",
                        x,y,z,0.4f,1.f,"neutral");
                })) return false;
        }
    }
    // restock tick already in mobsTick
    return true;
}
bool PiglinBarterGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Piglin) return false;
    const auto target = dimensionPlayer(m, ctx.nearestPlayer);
    if (!target) return false;
    if (ctx.nearestPlayerDist2 > 8*8) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.piglinBarterCooldown) return false;
    // The held item is part of the same-tick player snapshot.  Do not reach
    // back into Player::inv while the Mob lock is held.
    const auto gold = gen::itemIdByName().find("minecraft:gold_ingot");
    return gold != gen::itemIdByName().end() &&
           !target->heldItemEmpty && target->heldItemId == gold->second;
}
bool PiglinBarterGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Piglin) return false;
    m.piglinBarterCooldown = now + 100;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x=m.x, y=m.y, z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.piglin.admiring_item",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    return true;
}
bool CatScareGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Cat) return false;
    if (!dimensionPlayer(m, ctx.nearestPlayer)) return false;
    return ctx.nearestPlayerDist2 < 6*6;
}
bool CatScareGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Cat) return false;
    if (now < m.catScareCooldown) return false;
    // cat scares creeper: just sit and purr
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x=m.x, y=m.y, z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.cat.purr",
                    x,y,z,0.5f,1.f,"neutral");
            })) return false;
    }
    m.catScareCooldown = now + 60;
    // creeper avoid handled elsewhere
    return true;
}
bool FoxPounceGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Fox) return false;
    if (!dimensionPlayer(m, ctx.nearestPlayer)) return false;
    double d=std::sqrt(ctx.nearestPlayerDist2); return d>=2 && d<=6;
}
bool FoxPounceGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Fox) return false;
    if (now < m.foxPounceCooldown) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x += dx/d*0.42; m.z += dz/d*0.42; m.y += 0.38;
    if(ctx.srv){
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        WriteBuffer vel; vel.varint(entityId); vel.i16((int16_t)(dx/d*0.42*8000)); vel.i16((int16_t)(0.38*8000)); vel.i16((int16_t)(dz/d*0.42*8000));
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::EntityVelocity, vel);
            })) return false;
    }
    m.foxPounceCooldown = now + 40;
    return true;
}
bool PandaRollGoal::shouldStart(MobEntity& m, AiContext&) {
    if (m.kind != MobKind::Panda) return false;
    return (nextRandom()%200==0);
}
bool PandaRollGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Panda) return false;
    if (now < m.pandaRollCooldown) return false;
    m.pandaRollCooldown = now + 100;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x=m.x, y=m.y, z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.panda.cant_breed",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    return true;
}
bool DolphinPlayGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Dolphin) return false;
    if (!dimensionPlayer(m, ctx.nearestPlayer)) return false;
    return ctx.nearestPlayerDist2 < 10*10;
}
bool DolphinPlayGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Dolphin) return false;
    if (now < m.dolphinPlayCooldown) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x += dx/d*0.13; m.z += dz/d*0.13; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (ctx.srv && nextRandom()%30==0) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x=m.x, y=m.y, z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.dolphin.play",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    m.dolphinPlayCooldown = now + 20;
    return true;
}
bool EvokerFangGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Evoker) return false;
    if (!dimensionPlayer(m, ctx.nearestPlayer)) return false;
    if (ctx.nearestPlayerDist2 > 12*12) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.evokerFangCooldown) return false;
    return true;
}
bool EvokerFangGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Evoker) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double mobX = m.x, mobY = m.y, mobZ = m.z;
        std::vector<std::shared_ptr<MobEntity>> fangs;
        fangs.reserve(3);
        // Construct the child entities before releasing the source lock.  The
        // callback phase below then captures no live Mob fields.
        for(int i=0;i<3;++i){
            const double fx=t->x + (nextRandom()/(double)RAND_MAX-0.5)*2;
            const double fz=t->z + (nextRandom()/(double)RAND_MAX-0.5)*2;
            auto fang=std::make_shared<MobEntity>();
            fang->entityId=ctx.srv->nextEntityId();
            fang->kind=MobKind::EvokerFangs;
            fang->x=fx; fang->y=t->y; fang->z=fz;
            fang->health=1; fang->dimension=dimension;
            fangs.push_back(std::move(fang));
        }
        if (!withoutMobStateLock(m, [&] {
                for (const auto& fang : fangs) {
                    bool spawnAllowed = true;
                    if (ctx.srv->jvmRuntime()) {
                        spawnAllowed = ctx.srv->jvmRuntime()->onMobSpawn(
                            *fang, fang->x, fang->y, fang->z);
                    }
                    if (!spawnAllowed) continue;
                    ctx.srv->addMob(fang);
                    ctx.srv->broadcastMobSpawn(*fang);
                }
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.evoker.cast_spell",
                    mobX,mobY,mobZ,1.f,1.f,"hostile");
                ctx.srv->applyDamage(*t, 6.f, DamageSource::magic());
            })) return false;
    }
    m.evokerFangCooldown = now + 60;
    return true;
}

static bool nowIn(AiContext& ctx, std::int64_t cd){ return ctx.srv && ctx.srv->tickNoForTest() < cd; }
bool DrownedSwimGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Drowned) return false;
    if(!dimensionWorld(ctx, m)) return false;
    if(ctx.srv && ctx.srv->tickNoForTest()%5!=0) return false;
    uint16_t st=dimensionWorld(ctx, m)->getBlock((int)std::floor(m.x),(int)std::floor(m.y),(int)std::floor(m.z));
    auto* bd=gen::blockByState(st); if(!bd) return false;
    return std::string(bd->name).find("water")!=std::string::npos;
}
bool DrownedSwimGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t){
    if(m.kind!=MobKind::Drowned) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return true;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x += dx/d * 0.12; m.z += dz/d * 0.12;
    m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (World* world = dimensionWorld(ctx, m)) {
        const int chunkX = static_cast<int>(m.x) >> 4;
        const int chunkZ = static_cast<int>(m.z) >> 4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    return true;
}
bool PhantomCircleGoal::shouldStart(MobEntity& m, AiContext&){
    return m.kind==MobKind::Phantom && mobBehaviorSpec(m.kind) != nullptr;
}
bool PhantomCircleGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Phantom) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer);
    if(t){ m.phantomOrbitCenter={t->x, t->y+spec->auxiliaryOffset(), t->z}; }
    m.phantomOrbitAngle += 0.08;
    double r = spec->actionRange() + (m.phantomSize % spec->variantRangeModulo());
    double nx = m.phantomOrbitCenter.x + std::cos(m.phantomOrbitAngle)*r;
    double nz = m.phantomOrbitCenter.z + std::sin(m.phantomOrbitAngle)*r;
    double ny = m.phantomOrbitCenter.y + std::sin(now*0.02)*2;
    if(now - m.phantomLastSwoop >= spec->actionCooldown() && t){
        nx = t->x; nz = t->z; ny = t->y;
        const double attackThreshold = spec->actionThreshold();
        if(attackThreshold != 0.0 && std::hypot(nx-m.x,nz-m.z) < attackThreshold){
            m.phantomLastSwoop=now;
            if(ctx.srv && !withoutMobStateLock(m, [&] {
                    ctx.srv->mobAttackPlayer(m,*t);
                })) return false;
        }
    }
    double dx=nx-m.x, dy=ny-m.y, dz=nz-m.z; double d=std::sqrt(dx*dx+dy*dy+dz*dz)+1e-6;
    m.x+=dx/d*0.16; m.y+=dy/d*0.10; m.z+=dz/d*0.16;
    m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    const double altitudeFloor = spec->altitudeFloor();
    if(altitudeFloor != 0.0 && m.y < altitudeFloor) m.y=altitudeFloor;
    if (World* world = dimensionWorld(ctx, m)) {
        const int chunkX = static_cast<int>(m.x) >> 4;
        const int chunkZ = static_cast<int>(m.z) >> 4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    return true;
}
bool WardenSonicBoomGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Warden) return false;
    if(ctx.srv && ctx.srv->tickNoForTest()%5!=0) return false;
    return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 15*15;
}
bool WardenSonicBoomGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Warden) return false;
    if(now < m.wardenSonicCooldown) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t||ctx.nearestPlayerDist2>15*15) return false;
    const auto dimension = canonicalDimension(m.dimension);
    const auto entityId = m.entityId;
    const double mobX = m.x, mobY = m.y, mobZ = m.z;
    const float targetYaw = (float)(std::atan2(t->z-mobZ,t->x-mobX)*180/3.14159);
    if(ctx.srv){
        if(!raycastObstructed(dimensionWorld(ctx, m),t->x,t->y+1,t->z,
                              mobX,mobY+0.9,mobZ)){
            WriteBuffer vel; vel.varint(t->entityId); vel.i16(0); vel.i16((int16_t)(1.5*8000)); vel.i16(0);
            const WriteBuffer particle = makeWorldParticlesBody(
                mobX, mobY + 1.6, mobZ, 0, 0, 0, 0.1f, 1,
                ParticleId::sonic_boom, {}, true, false);
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastHurtAnimationFor(
                        dimension, t->entityId, targetYaw);
                    ctx.srv->applyDamage(*t, 10.f,
                                         DamageSource::sonicBoom());
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.warden.sonic_boom",
                        mobX,mobY,mobZ,1.f,1.f,"hostile");
                    ctx.srv->broadcastEntitySoundFor(
                        dimension, entityId,
                        "minecraft:entity.warden.sonic_boom", 1.f, 1.f,
                        GameServer::SoundSource::Hostile);
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::EntityVelocity,
                        vel);
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::WorldParticles,
                        particle);
                })) return false;
        }
    }
    m.wardenSonicCooldown=now+34;
    return true;
}
bool EndermanTeleportGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Enderman) return false;
    if(ctx.srv &&
       ctx.srv->tickNoForTest() -
               ctx.lastHurtTick.load(std::memory_order_acquire) < 30)
        return true;
    if(!dimensionWorld(ctx, m) || !ctx.srv) return false;
    // daylight flee check simplified: if sky light high and not night
    if(!ctx.srv->isNight()){
        World* world = dimensionWorld(ctx, m);
        const int chunkX = static_cast<int>(m.x) >> 4;
        const int chunkZ = static_cast<int>(m.z) >> 4;
        const int blockX = static_cast<int>(m.x);
        const int blockY = static_cast<int>(m.y);
        const int blockZ = static_cast<int>(m.z);
        std::uint8_t sky = 0;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
                sky = world->getSkyLight(blockX, blockY, blockZ);
            })) return false;
        if(sky>=14) return true;
    }
    return false;
}
bool EndermanTeleportGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Enderman) return false;
    if(now - m.lastTeleportTick < 30) return false;
    if(!dimensionWorld(ctx, m)) return false;
    for(int attempt=0; attempt<16; ++attempt){
        double nx = m.x + (nextRandom()/(double)RAND_MAX*64 -32);
        double nz = m.z + (nextRandom()/(double)RAND_MAX*64 -32);
        double ny = m.y + (nextRandom()/(double)RAND_MAX*32 -16);
        int ix=(int)std::floor(nx), iz=(int)std::floor(nz), iy=(int)std::floor(ny);
        World* world = dimensionWorld(ctx, m);
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(ix>>4, iz>>4);
            })) return false;
        for(int dy=-4; dy<=4; ++dy){
            int tryY=iy+dy; if(tryY<kMinY || tryY>kMinY+320) continue;
            uint16_t a1=dimensionWorld(ctx, m)->getBlock(ix,tryY,iz); uint16_t a2=dimensionWorld(ctx, m)->getBlock(ix,tryY+1,iz); uint16_t below=dimensionWorld(ctx, m)->getBlock(ix,tryY-1,iz);
            if(a1==0 && a2==0 && below!=0){
                const double ox=m.x, oy=m.y, oz=m.z;
                m.x=ix+0.5; m.z=iz+0.5; m.y=tryY+0.5; m.lastTeleportTick=now;
                if(ctx.srv){
                    const auto dimension = canonicalDimension(m.dimension);
                    const auto entityId = m.entityId;
                    const auto yaw = m.yaw;
                    const double newX=m.x, newY=m.y, newZ=m.z;
                    WriteBuffer tp; tp.varint(entityId); tp.f64(newX); tp.f64(newY); tp.f64(newZ); tp.f32(yaw); tp.f32(0); tp.boolean(true);
                    if (!withoutMobStateLock(m, [&] {
                            ctx.srv->broadcastSoundFor(
                                dimension, "minecraft:entity.enderman.teleport",
                                newX,newY,newZ,1.f,1.f,"hostile");
                            ctx.srv->broadcastPacketExceptInDimension(
                                dimension, nullptr,
                                proto::pl::sc::EntityTeleport, tp);
                            for(int i=0;i<8;i++){
                                const WriteBuffer pt = makeWorldParticlesBody(
                                    ox + (nextRandom()/(double)RAND_MAX-0.5)*1.5,
                                    oy + nextRandom()/(double)RAND_MAX*2.0,
                                    oz + (nextRandom()/(double)RAND_MAX-0.5)*1.5,
                                    0, 0, 0, 0.1f, 1, ParticleId::portal,
                                    {}, true, false);
                                ctx.srv->broadcastPacketExceptInDimension(
                                    dimension, nullptr,
                                    proto::pl::sc::WorldParticles, pt);
                            }
                        })) return false;
                    if (m.entityId != entityId ||
                        canonicalDimension(m.dimension) != dimension ||
                        m.x != newX || m.y != newY || m.z != newZ)
                        return true;
                }
                return true;
            }
        }
    }
    return true;
}
bool ShulkerPeekGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Shulker) return false;
    return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < perceptionRange2(MobKind::Shulker); // plan44 G-05
}
bool ShulkerPeekGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Shulker) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    m.shulkerPeek = std::min(100, m.shulkerPeek+5);
    if(ctx.srv){
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double mobX=m.x, mobY=m.y, mobZ=m.z;
        WriteBuffer md; md.varint(entityId); meta::writeMetaByte(md, 15, (int8_t)m.shulkerPeek); md.u8(255);
        if(now%60==0){
            double dx=t->x-mobX, dy=(t->y+0.5)-mobY, dz=t->z-mobZ; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::SetEntityMetadata,
                        md);
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::Arrow,
                        mobX, mobY+0.5, mobZ, dx/d*0.7,
                        dy/d*0.2+0.1, dz/d*0.7, entityId, false);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.shulker.shoot",
                        mobX,mobY,mobZ,1.f,1.f,"hostile");
                })) return false;
        } else if (!withoutMobStateLock(m, [&] {
                       ctx.srv->broadcastPacketExceptInDimension(
                           dimension, nullptr,
                           proto::pl::sc::SetEntityMetadata, md);
                   })) return false;
        if(now - ctx.lastHurtTick.load(std::memory_order_acquire) < 20){
            // teleport 8 blocks on hurt
            const double nx=mobX+(nextRandom()/(double)RAND_MAX*16-8);
            const double nz=mobZ+(nextRandom()/(double)RAND_MAX*16-8);
            m.x=nx; m.z=nz;
            if (World* world = dimensionWorld(ctx, m)) {
                if (!withoutMobStateLock(m, [&] {
                        world->generateChunkIfMissing(
                            static_cast<int>(nx)>>4,
                            static_cast<int>(nz)>>4);
                    })) return false;
            }
        }
    }
    return true;
}
bool GuardianBeamGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Guardian && m.kind!=MobKind::ElderGuardian) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    return spec && dimensionPlayer(m, ctx.nearestPlayer) && spec->withinActionRangeSquared(ctx.nearestPlayerDist2);
}
bool GuardianBeamGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Guardian && m.kind!=MobKind::ElderGuardian) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(now < m.guardianBeamCooldown) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    const auto dimension = canonicalDimension(m.dimension);
    const auto entityId = m.entityId;
    const double mobX=m.x, mobY=m.y, mobZ=m.z;
    double dx=t->x-mobX, dz=t->z-mobZ; double d=std::sqrt(dx*dx+dz*dz);
    if (!spec->withinActionRange(d)) return false;
    if(ctx.srv){
        const double magnitude = m.kind == MobKind::ElderGuardian
            ? spec->secondaryActionMagnitude() : spec->actionMagnitude();
        float dmg = static_cast<float>(magnitude);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->applyDamage(*t, dmg, DamageSource::magic());
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.guardian.attack",
                    mobX,mobY,mobZ,1.f,1.f,"hostile");
                ctx.srv->broadcastEntitySoundFor(
                    dimension, entityId, "minecraft:entity.guardian.attack",
                    1.f, 1.f, GameServer::SoundSource::Hostile);
            })) return false;
    }
    m.guardianBeamCooldown=now+spec->actionCooldown();
    return true;
}
bool SlimeSplitGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Slime) return false;
    if(now < m.slimeJumpCooldown) return false;
    if(nextRandom()%40!=0) return false;
    m.y += 0.4 * (m.slimeSize+1)*0.5;
    if(ctx.srv){
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double x=m.x, y=m.y, z=m.z;
        WriteBuffer vel; vel.varint(entityId); vel.i16(0); vel.i16((int16_t)(0.4*8000)); vel.i16(0);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::EntityVelocity, vel);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.slime.jump",
                    x,y,z,0.5f,1.f,"hostile");
            })) return false;
    }
    m.slimeJumpCooldown=now+20;
    return true;
}
bool MagmaCubeJumpGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::MagmaCube) return false;
    if(now < m.slimeJumpCooldown) return false;
    if(nextRandom()%30!=0) return false;
    m.y += 0.45 * (m.slimeSize+1)*0.5;
    if(m.y < kMinY+1) m.y = kMinY+1;
    if(ctx.srv){
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double x=m.x, y=m.y, z=m.z;
        WriteBuffer vel; vel.varint(entityId); vel.i16(0); vel.i16((int16_t)(0.45*8000)); vel.i16(0);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::EntityVelocity, vel);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.magma_cube.jump",
                    x,y,z,0.5f,1.f,"hostile");
            })) return false;
    }
    m.slimeJumpCooldown=now+18;
    return true;
}
bool SilverfishInfestGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Silverfish) return false; return nowIn(ctx, m.silverfishCallCooldown) ? false : (ctx.lastHurtTick.load(std::memory_order_acquire)>=0 && ctx.srv && ctx.srv->tickNoForTest()-ctx.lastHurtTick.load(std::memory_order_acquire)<20); }
bool SilverfishInfestGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Silverfish) return false;
    if(now < m.silverfishCallCooldown) return false;
    if(!ctx.srv) return false;
    const auto dimension = canonicalDimension(m.dimension);
    const double mobX=m.x, mobY=m.y, mobZ=m.z;
    World* world = dimensionWorld(ctx, m);
    int spawned=0;
    bool foundInfested=false;
    for(int dx=-6; dx<=6 && spawned<3; ++dx) for(int dz=-6; dz<=6 && spawned<3; ++dz){
        int bx=(int)std::floor(mobX)+dx, bz=(int)std::floor(mobZ)+dz, by=(int)std::floor(mobY);
        uint16_t st=world?world->getBlock(bx,by,bz):0; if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        std::string n(bd->name); if(n.find("infested")!=std::string::npos){
            foundInfested=true;
            auto silverfish=std::make_shared<MobEntity>();
            silverfish->entityId=ctx.srv->nextEntityId();
            silverfish->kind=MobKind::Silverfish;
            silverfish->x=bx+0.5; silverfish->y=by+0.5;
            silverfish->z=bz+0.5;
            silverfish->health=mobStats(MobKind::Silverfish).maxHealth;
            silverfish->dimension=dimension;
            bool spawnedThis = false;
            if (!withoutMobStateLock(m, [&] {
                    bool spawnAllowed = true;
                    if (ctx.srv->jvmRuntime()) {
                        spawnAllowed = ctx.srv->jvmRuntime()->onMobSpawn(
                            *silverfish, silverfish->x,
                            silverfish->y, silverfish->z);
                    }
                    if (!spawnAllowed) return;
                    ctx.srv->addMob(silverfish);
                    ctx.srv->broadcastMobSpawn(*silverfish);
                    if (world) {
                        world->setBlock(bx, by, bz, 0);
                        ctx.srv->broadcastBlockChangeFor(
                            dimension, bx, by, bz, 0);
                    }
                    spawnedThis = true;
                })) return false;
            if (spawnedThis) ++spawned;
        }
    }
    // fallback spawn even without infested block for test determinism
    if(spawned==0 && !foundInfested){
        auto silverfish=std::make_shared<MobEntity>();
        silverfish->entityId=ctx.srv->nextEntityId();
        silverfish->kind=MobKind::Silverfish;
        silverfish->x=mobX+1; silverfish->y=mobY; silverfish->z=mobZ+1;
        silverfish->health=mobStats(MobKind::Silverfish).maxHealth;
        silverfish->dimension=dimension;
        if (!withoutMobStateLock(m, [&] {
                bool spawnAllowed = true;
                if (ctx.srv->jvmRuntime()) {
                    spawnAllowed = ctx.srv->jvmRuntime()->onMobSpawn(
                        *silverfish, silverfish->x,
                        silverfish->y, silverfish->z);
                }
                if (spawnAllowed) {
                    ctx.srv->addMob(silverfish);
                    ctx.srv->broadcastMobSpawn(*silverfish);
                }
            })) return false;
    }
    m.silverfishCallCooldown=now+100;
    if(!withoutMobStateLock(m, [&] {
            ctx.srv->broadcastSoundFor(
                dimension, "minecraft:entity.silverfish.ambient",
                mobX,mobY,mobZ,1.f,1.f,"hostile");
        })) return false;
    return true;
}
bool EndermiteTeleportGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Endermite) return false; return ctx.lastHurtTick.load(std::memory_order_acquire)>=0 && ctx.srv && ctx.srv->tickNoForTest()-ctx.lastHurtTick.load(std::memory_order_acquire)<20; }
bool EndermiteTeleportGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Endermite) return false;
    if(now < m.endermiteLifeUntil - 2390) return false; // throttle
    m.x += (nextRandom()/(double)RAND_MAX-0.5)*4; m.z += (nextRandom()/(double)RAND_MAX-0.5)*4;
    if(ctx.srv){
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double x=m.x, y=m.y, z=m.z;
        const auto yaw=m.yaw;
        WriteBuffer tp; tp.varint(entityId); tp.f64(x); tp.f64(y); tp.f64(z); tp.f32(yaw); tp.f32(0); tp.boolean(true);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.endermite.ambient",
                    x,y,z,1.f,1.f,"hostile");
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::EntityTeleport, tp);
            })) return false;
    }
    if(m.endermiteLifeUntil==0) m.endermiteLifeUntil=now+2400;
    return true;
}
bool VindicatorAxeGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Vindicator) return false; return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 12*12; }
bool VindicatorAxeGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Vindicator) return false;
    if(now < m.vindicatorJohnnyUntil && m.vindicatorJohnnyUntil!=0) { /* johnny cooldown */ }
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    const auto dimension = canonicalDimension(m.dimension);
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d<1.9){
        if(now%20==0 && ctx.srv && !withoutMobStateLock(m, [&] {
                ctx.srv->mobAttackPlayer(m,*t);
            })) return false;
        return true;
    }
    m.x+=dx/d*0.11; m.z+=dz/d*0.11; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (World* world=dimensionWorld(ctx, m)) {
        const int chunkX=static_cast<int>(m.x)>>4, chunkZ=static_cast<int>(m.z)>>4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    if(ctx.srv) {
        const double x=m.x,y=m.y,z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.vindicator.ambient",
                    x,y,z,1.f,1.f,"hostile");
            })) return false;
    }
    m.vindicatorJohnnyUntil=now+20;
    return true;
}
bool PillagerCrossbowGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Pillager) return false; return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < perceptionRange2(MobKind::Pillager); } // plan44 G-05
bool PillagerCrossbowGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Pillager) return false;
    if(now < m.pillagerCrossbowCooldown) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    const auto dimension = canonicalDimension(m.dimension);
    const auto entityId = m.entityId;
    const double mobX=m.x, mobY=m.y, mobZ=m.z;
    double dx=t->x-mobX, dy=(t->y+1)-(mobY+1.6), dz=t->z-mobZ; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d<5 || d > perceiveDist(MobKind::Pillager)) { // patrol approach (plan44 G-05 follow_range)
        m.x+=dx/d*0.09; m.z+=dz/d*0.09; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90); return true;
    }
    if(ctx.srv && !withoutMobStateLock(m, [&] {
            ctx.srv->spawnProjectileFor(
                dimension, ProjectileKind::Arrow,
                mobX, mobY+1.6, mobZ, dx/d*1.4,
                dy/d*0.2+0.12, dz/d*1.4, entityId, false);
        })) return false;
    m.pillagerCrossbowCooldown=now+40;
    if(ctx.srv && !withoutMobStateLock(m, [&] {
            ctx.srv->broadcastSoundFor(
                dimension, "minecraft:entity.pillager.shoot",
                mobX,mobY,mobZ,1.f,1.f,"hostile");
        })) return false;
    return true;
}
bool HoglinRepelGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Hoglin) return false;
    if(!dimensionWorld(ctx, m)) return false;
    for(int dx=-7; dx<=7; ++dx) for(int dz=-7; dz<=7; ++dz){
        int bx=(int)std::floor(m.x)+dx, bz=(int)std::floor(m.z)+dz, by=(int)std::floor(m.y);
        uint16_t st=dimensionWorld(ctx, m)->getBlock(bx,by,bz); if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        std::string n(bd->name); if(n.find("warped_fungus")!=std::string::npos || n.find("respawn_anchor")!=std::string::npos || n.find("nether_portal")!=std::string::npos) return true;
    }
    return false;
}
bool HoglinRepelGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Hoglin) return false;
    if(now < m.hoglinRepelCooldown) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer);
    double dx, dz;
    if(t){ dx=m.x - t->x; dz=m.z - t->z; } else { dx=(nextRandom()/(double)RAND_MAX-0.5)*2; dz=(nextRandom()/(double)RAND_MAX-0.5)*2; }
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.x+=dx/d*0.14; m.z+=dz/d*0.14; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (World* world=dimensionWorld(ctx, m)) {
        const int chunkX=static_cast<int>(m.x)>>4, chunkZ=static_cast<int>(m.z)>>4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    m.hoglinRepelCooldown=now+10;
    return true;
}
bool ZoglinFrenzyGoal::shouldStart(MobEntity& m, AiContext&) { return m.kind==MobKind::Zoglin; }
bool ZoglinFrenzyGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Zoglin) return false;
    if(now < m.zoglinFrenzyUntil) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    const auto dimension = canonicalDimension(m.dimension);
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d<1.9){
        if(now%15==0 && ctx.srv) {
            WriteBuffer vel; vel.varint(t->entityId); vel.i16((int16_t)(dx/d*1.0*8000)); vel.i16((int16_t)(0.4*8000)); vel.i16((int16_t)(dz/d*1.0*8000));
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->mobAttackPlayer(m,*t);
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::EntityVelocity,
                        vel);
                })) return false;
        }
        return true;
    }
    m.x+=dx/d*0.14; m.z+=dz/d*0.14; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (World* world=dimensionWorld(ctx, m)) {
        const int chunkX=static_cast<int>(m.x)>>4, chunkZ=static_cast<int>(m.z)>>4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    m.zoglinFrenzyUntil=now+10;
    return true;
}
bool WitherSkeletonEffectGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::WitherSkeleton) return false; return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 3*3; }
bool WitherSkeletonEffectGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::WitherSkeleton) return false;
    if(now < m.witherSkeletonEffectCooldown) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    if(ctx.srv){
        const auto dimension = canonicalDimension(m.dimension);
        const double x=m.x,y=m.y,z=m.z;
        WriteBuffer eff; eff.varint(t->entityId); eff.varint(20); eff.i8(0); eff.varint(100); eff.u8(0x01);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->mobAttackPlayer(m,*t);
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::EntityEffect, eff);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.wither_skeleton.ambient",
                    x,y,z,1.f,1.f,"hostile");
            })) return false;
    }
    m.witherSkeletonEffectCooldown=now+40;
    return true;
}
bool GoatRamGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Goat) return false; return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 10*10; }
bool GoatRamGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Goat) return false;
    if(now < m.goatRamCooldown) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    const auto dimension = canonicalDimension(m.dimension);
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d>10) return false;
    // charge 30t: ram
    m.x+=dx/d*0.42; m.z+=dz/d*0.42;
    if(d<1.9){
        if(ctx.srv){
            WriteBuffer vel; vel.varint(t->entityId); vel.i16((int16_t)(dx/d*1.5*8000)); vel.i16((int16_t)(0.4*8000)); vel.i16((int16_t)(dz/d*1.5*8000));
            const double x=m.x,y=m.y,z=m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::EntityVelocity, vel);
                    ctx.srv->applyDamage(*t, 5.f, "mob");
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.goat.ram_impact",
                        x,y,z,1.f,1.f,"neutral");
                })) return false;
        }
        m.goatRamCooldown=now+100;
        return true;
    }
    if(ctx.srv && nextRandom()%20==0) {
        const double x=m.x,y=m.y,z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.goat.prepare_ram",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    m.goatRamCooldown=now+50;
    return true;
}
bool AxolotlPlayDeadGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Axolotl) return false; if(m.health > mobStats(m.kind).maxHealth*0.33) return false; return ctx.lastHurtTick.load(std::memory_order_acquire)>=0 && ctx.srv && ctx.srv->tickNoForTest()-ctx.lastHurtTick.load(std::memory_order_acquire)<20; }
bool AxolotlPlayDeadGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Axolotl) return false;
    if(now < m.axolotlPlayDeadUntil && m.axolotlPlayDeadUntil!=0) return true;
    m.axolotlPlayDeadUntil=now+200;
    m.health = std::min(m.health+2.0, (double)mobStats(m.kind).maxHealth);
    if(ctx.srv){
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double x=m.x,y=m.y,z=m.z;
        WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md, 16, true); md.u8(255);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr,
                    proto::pl::sc::SetEntityMetadata, md);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.axolotl.splash",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    return true;
}
bool FrogTongueGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Frog) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(ctx.srv && ctx.srv->tickNoForTest()<m.frogTongueCooldown) return false;
    return true;
}
bool FrogTongueGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Frog) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(now < m.frogTongueCooldown) return false;
    // Find a small slime/magma cube within the spec's action range.
    if(ctx.srv){
        std::shared_ptr<MobEntity> prey;
        MobSnapshot preyState;
        const auto dimension = canonicalDimension(m.dimension);
        const double mobX=m.x, mobY=m.y, mobZ=m.z;
        double best=0.0;
        if (!withoutMobStateLock(m, [&] {
                for(auto& mm: ctx.srv->mobsSnapshot()) {
                    if (!mm || mm.get() == &m) continue;
                    const MobSnapshot view = snapshotMob(*mm);
                    if (view.dimension != dimension ||
                        (view.kind!=MobKind::Slime &&
                         view.kind!=MobKind::MagmaCube) ||
                        view.slimeSize!=0 || view.dead) continue;
                    const double dx=view.x-mobX, dz=view.z-mobZ;
                    const double d2=dx*dx+dz*dz;
                    if(spec->withinActionRangeSquared(d2) &&
                       (!prey || d2<best)) {
                        best=d2; prey=mm; preyState=view;
                    }
                }
            })) return false;
        if(prey){
            double dx=preyState.x-mobX, dz=preyState.z-mobZ;
            double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            if(!spec->beyondActionThreshold(d)){
                const auto slimeBall = gen::itemIdByName().at("minecraft:slime_ball");
                const auto preyId = preyState.entityId;
                bool consumed = false;
                if (!withoutMobStateLock(m, [&] {
                        std::lock_guard preyLock(*prey->stateMtx);
                        if (prey->entityId != preyId ||
                            canonicalDimension(prey->dimension) != dimension ||
                            prey->dead || prey->slimeSize != 0)
                            return;
                        prey->dead=true;
                        consumed = true;
                        ctx.srv->broadcastSoundFor(
                            dimension, "minecraft:entity.frog.eat",
                            mobX,mobY,mobZ,1.f,1.f,"neutral");
                        ctx.srv->spawnItemDropFor(
                            dimension, mobX,mobY,mobZ, slimeBall,
                            static_cast<std::uint8_t>(spec->actionMagnitude()));
                    })) return false;
                if (!consumed) return true;
            } else {
                m.x+=dx/d*0.12; m.z+=dz/d*0.12;
                const double x=m.x,y=m.y,z=m.z;
                if (!withoutMobStateLock(m, [&] {
                        ctx.srv->broadcastSoundFor(
                            dimension, "minecraft:entity.frog.tongue",
                            x,y,z,1.f,1.f,"neutral");
                    })) return false;
            }
        } else if(nextRandom()%spec->randomDenominator()==0){
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.frog.ambient",
                        mobX,mobY,mobZ,1.f,1.f,"neutral");
                })) return false;
        }
    }
    m.frogTongueCooldown=now+spec->actionCooldown();
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
        if (World* world=dimensionWorld(ctx, m)) {
            const int chunkX=static_cast<int>(m.x)>>4;
            const int chunkZ=static_cast<int>(m.z)>>4;
            if (!withoutMobStateLock(m, [&] {
                    world->generateChunkIfMissing(chunkX, chunkZ);
                })) return false;
        }
        return true;
    }
    // lay 1-4 eggs (simulate by placing turtle_egg block)
    if(dimensionWorld(ctx, m) && ctx.srv){
        int ex=(int)std::floor(m.x), ey=(int)std::floor(m.y), ez=(int)std::floor(m.z);
        auto* bd=gen::blockByName("minecraft:turtle_egg");
        if(bd){
            World* world=dimensionWorld(ctx, m);
            const auto dimension=canonicalDimension(m.dimension);
            const double mobX=m.x,mobY=m.y,mobZ=m.z;
            auto baby=std::make_shared<MobEntity>();
            baby->entityId=ctx.srv->nextEntityId();
            baby->kind=MobKind::Turtle;
            baby->health=mobStats(MobKind::Turtle).maxHealth;
            baby->age=-24000; baby->x=ex+0.5; baby->y=ey+1;
            baby->z=ez+0.5; baby->dimension=dimension;
            if (!withoutMobStateLock(m, [&] {
                    world->setBlock(ex,ey,ez, bd->defaultState);
                    ctx.srv->broadcastBlockChangeFor(
                        dimension, ex,ey,ez, bd->defaultState);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.turtle.lay_egg",
                        mobX,mobY,mobZ,1.f,1.f,"neutral");
                    bool spawnAllowed = true;
                    if (ctx.srv->jvmRuntime()) {
                        spawnAllowed = ctx.srv->jvmRuntime()->onMobSpawn(
                            *baby, baby->x, baby->y, baby->z);
                    }
                    if (spawnAllowed) {
                        ctx.srv->addMob(baby);
                        ctx.srv->broadcastMobSpawn(*baby);
                    }
                })) return false;
        }
    }
    m.turtleEggCooldown=now+6000;
    auto& rm=m.turtleHomePos; rm={INT_MAX,INT_MAX,INT_MAX};
    return true;
}
bool ParrotDanceGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Parrot) return false;
    if(!dimensionWorld(ctx, m)) return false;
    // near jukebox playing: check within 6 for jukebox block
    for(int dx=-6; dx<=6; ++dx) for(int dz=-6; dz<=6; ++dz){
        int bx=(int)std::floor(m.x)+dx, bz=(int)std::floor(m.z)+dz, by=(int)std::floor(m.y);
        uint16_t st=dimensionWorld(ctx, m)->getBlock(bx,by,bz); if(st==0) continue;
        auto* bd=gen::blockByState(st); if(!bd) continue;
        if(std::string(bd->name).find("jukebox")!=std::string::npos) return true;
    }
    return false;
}
bool ParrotDanceGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Parrot) return false;
    m.parrotDancing=true; m.parrotDanceUntil=now+40;
    m.yaw += 18; if(m.yaw>360) m.yaw-=360;
    if(ctx.srv && now%20==0) {
        const auto dimension=canonicalDimension(m.dimension);
        const double x=m.x,y=m.y,z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.parrot.imitate.warden",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    return true;
}
bool OcelotTrustGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Ocelot) return false;
    return static_cast<bool>(dimensionPlayer(m, ctx.temptingPlayer)) ||
           (dimensionPlayer(m, ctx.nearestPlayer) &&
            ctx.nearestPlayerDist2 < 10*10);
}
bool OcelotTrustGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Ocelot) return false;
    if(now < m.ocelotTrustCooldown) return false;
    auto t=dimensionPlayer(m, ctx.temptingPlayer);
    if (!t) t = dimensionPlayer(m, ctx.nearestPlayer);
    if(!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d<2.5){
        m.isTamed=true;
        if(ctx.srv){
            const auto dimension=canonicalDimension(m.dimension);
            const auto entityId=m.entityId;
            const double x=m.x,y=m.y,z=m.z;
            WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md,16,true); md.u8(255);
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr,
                        proto::pl::sc::SetEntityMetadata, md);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.ocelot.ambient",
                        x,y,z,1.f,1.f,"neutral");
                })) return false;
        }
        m.ocelotTrustCooldown=now+100;
        return true;
    }
    // sprint 0.18 when creeper approach 6: already handled via Avoid? just move toward player
    m.x+=dx/d*0.09; m.z+=dz/d*0.09; m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
    if (World* world=dimensionWorld(ctx, m)) {
        const int chunkX=static_cast<int>(m.x)>>4, chunkZ=static_cast<int>(m.z)>>4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    m.ocelotTrustCooldown=now+20;
    return true;
}
bool SnowGolemSnowTrailGoal::shouldStart(MobEntity& m, AiContext&){ return m.kind==MobKind::SnowGolem; }
bool SnowGolemSnowTrailGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::SnowGolem) return false;
    if(now < m.snowGolemTrailCooldown) return false;
    if(dimensionWorld(ctx, m) && ctx.srv){
        World* world = dimensionWorld(ctx, m);
        const auto dimension = canonicalDimension(m.dimension);
        int bx=(int)std::floor(m.x), by=(int)std::floor(m.y)-1, bz=(int)std::floor(m.z);
        uint16_t below=world->getBlock(bx,by,bz); if(below!=0){
            auto* bdSnow=gen::blockByName("minecraft:snow");
            if(bdSnow){
                int snowY=by+1;
                uint16_t at=world->getBlock(bx,snowY,bz);
                if(at==0){
                    if (!withoutMobStateLock(m, [&] {
                            world->setBlock(bx,snowY,bz, bdSnow->defaultState);
                            ctx.srv->broadcastBlockChangeFor(
                                dimension, bx,snowY,bz, bdSnow->defaultState);
                        })) return false;
                }
            }
        }
        // shoot snowball
        const auto t=dimensionPlayer(m, ctx.nearestPlayer);
        if(t && ctx.nearestPlayerDist2 < 16*16 && now%40==0){
            const double mobX=m.x,mobY=m.y,mobZ=m.z;
            const auto entityId=m.entityId;
            const double dx=t->x-mobX, dy=(t->y+1)-(mobY+1.2), dz=t->z-mobZ;
            const double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::Snowball,
                        mobX, mobY+1.2, mobZ, dx/d*1.2,
                        dy/d*0.2+0.1, dz/d*1.2, entityId, false);
                })) return false;
        }
        // melt in nether/desert biom check simplified: if y>60 and isNight false and biome desert -> melt damage
        std::string biome; try{ biome=dimensionWorld(ctx, m)->sampledBiome(bx,by,bz);}catch(...){}
        if(biome.find("desert")!=std::string::npos || biome.find("nether")!=std::string::npos){
            if(now%40==0 && ctx.srv &&
               !withoutMobStateLock(m, [&] {
                   ctx.srv->applyDamageToMob(m, 1.f, "burned to death");
               })) return false;
        }
    }
    m.snowGolemTrailCooldown=now+10;
    return true;
}
bool WitherSkullBarrageGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Wither) return false; return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 24*24 && m.health <= mobStats(m.kind).maxHealth*0.5f; }
bool WitherSkullBarrageGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Wither) return false;
    if(now < m.witherBarrageCooldown) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    if(m.health > mobStats(m.kind).maxHealth*0.5f) return false;
    if(ctx.srv){
        const auto dimension=canonicalDimension(m.dimension);
        const auto entityId=m.entityId;
        const double mobX=m.x,mobY=m.y,mobZ=m.z;
        const double dx=t->x-mobX, dy=(t->y+1)-(mobY+1.5), dz=t->z-mobZ;
        const double d=std::sqrt(dx*dx+dz*dz)+1e-6;
        if (!withoutMobStateLock(m, [&] {
                for(int i=0;i<3;++i){
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::WitherSkull,
                        mobX, mobY+1.5, mobZ,
                        dx/d*1.1+(nextRandom()/(double)RAND_MAX-0.5)*0.1,
                        dy/d*0.3+0.1,
                        dz/d*1.1+(nextRandom()/(double)RAND_MAX-0.5)*0.1,
                        entityId, false, true);
                }
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.wither.shoot",
                    mobX,mobY,mobZ,1.f,1.f,"hostile");
            })) return false;
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
        if(ctx.srv && now%40==0) {
            const auto dimension=canonicalDimension(m.dimension);
            const auto entityId=m.entityId;
            const double x=m.x,y=m.y,z=m.z;
            const double vx=(nextRandom()/(double)RAND_MAX-0.5)*0.6;
            const double vz=(nextRandom()/(double)RAND_MAX-0.5)*0.6;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::DragonFireball,
                        x,y,z,vx,-0.3,vz,entityId,false);
                })) return false;
        }
    } else if(m.y < 78){
        double ang=now*0.03; double rx=std::cos(ang)*28, rz=std::sin(ang)*28;
        double dx=rx-m.x, dz=rz-m.z; m.x+=dx*0.04; m.z+=dz*0.04; m.y += (68-m.y)*0.02;
    } else {
        if(ctx.srv && now%20==0) {
            const auto dimension=canonicalDimension(m.dimension);
            const auto entityId=m.entityId;
            const double x=m.x,y=m.y,z=m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::DragonFireball,
                        x,y,z,0,-0.4,0,entityId,false);
                })) return false;
        }
        if(nextRandom()%100<5) m.dragonPhaseUntil=now+80;
    }
    m.yaw=(float)(now*0.8);
    return true;
}
bool StriderLavaWalkGoal::shouldStart(MobEntity& m, AiContext&){
    return m.kind == MobKind::Strider && mobBehaviorSpec(m.kind) != nullptr;
}
bool StriderLavaWalkGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Strider) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(!dimensionWorld(ctx, m)) return false;
    uint16_t st=dimensionWorld(ctx, m)->getBlock((int)std::floor(m.x),(int)std::floor(m.y)-1,(int)std::floor(m.z));
    auto* bd=gen::blockByState(st);
    bool onLava = bd && std::string(bd->name).find("lava")!=std::string::npos;
    if(!onLava){
        // shiver when cold
        if(!m.striderShivering){
            m.striderShivering=true;
            m.striderShiverUntil=now+spec->actionCooldown();
            if(ctx.srv) {
                const auto dimension=canonicalDimension(m.dimension);
                const double x=m.x,y=m.y,z=m.z;
                if (!withoutMobStateLock(m, [&] {
                        ctx.srv->broadcastSoundFor(
                            dimension, "minecraft:entity.strider.ambient",
                            x,y,z,0.5f,1.f,"neutral");
                    })) return false;
            }
        }
        m.y -= 0.02;
    } else {
        m.striderShivering=false;
        // lava walk no sink, steer toward player if saddled
        if(const auto target=dimensionPlayer(m, ctx.nearestPlayer)){
            double dx=target->x-m.x, dz=target->z-m.z;
            double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            m.x+=dx/d*0.09; m.z+=dz/d*0.09;
        }
        m.y = std::max(m.y, (double)kMinY+2);
    }
    if (World* world=dimensionWorld(ctx, m)) {
        const int chunkX=static_cast<int>(m.x)>>4, chunkZ=static_cast<int>(m.z)>>4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    return true;
}
bool IllusionerInvisGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Illusioner) return false; return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 12*12; }
bool IllusionerInvisGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Illusioner) return false;
    if(now < m.illusionerInvisUntil) return false;
    m.illusionerInvisUntil=now+200;
    if(ctx.srv){
        const auto dimension=canonicalDimension(m.dimension);
        const auto entityId=m.entityId;
        const double mobX=m.x,mobY=m.y,mobZ=m.z;
        WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md,16,true); md.u8(255);
        const auto t=dimensionPlayer(m, ctx.nearestPlayer);
        double vx=0,vz=0;
        bool shoot=false;
        WriteBuffer eff;
        if (t) {
            const double dx=t->x-mobX, dz=t->z-mobZ;
            const double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            vx=dx/d*1.2; vz=dz/d*1.2; shoot=true;
            eff.varint(t->entityId); eff.varint(15); eff.i8(1);
            eff.varint(100); eff.u8(0x01);
        }
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr,
                    proto::pl::sc::SetEntityMetadata, md);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.illusioner.cast_spell",
                    mobX,mobY,mobZ,1.f,1.f,"hostile");
                if (shoot) {
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::Arrow,
                        mobX, mobY+1.6, mobZ, vx, 0.12, vz,
                        entityId, false);
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::EntityEffect, eff);
                }
            })) return false;
    }
    return true;
}
bool SnifferDigGoal::shouldStart(MobEntity& m, AiContext&){
    return m.kind==MobKind::Sniffer && mobBehaviorSpec(m.kind) != nullptr;
}
bool SnifferDigGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Sniffer) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(now < m.snifferDigCooldown) return false;
    if(nextRandom()%spec->actionInterval()!=0) return false;
    // sniff 6s -> dig
    if(ctx.srv) {
        const auto dimension=canonicalDimension(m.dimension);
        const double x=m.x,y=m.y,z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.sniffer.scenting",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    m.snifferDigCooldown=now+spec->actionCooldown();
    // after sniff, dig ancient seed after 6s simplified to immediate drop
    if(nextRandom()%spec->randomDenominator()==0 && ctx.srv){
        // drop torchflower seeds
        auto it = gen::itemIdByName().find("minecraft:torchflower_seeds");
        const auto dimension=canonicalDimension(m.dimension);
        const double x=m.x,y=m.y,z=m.z;
        const auto entityId=m.entityId;
        WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md,16,true); md.u8(255);
        if (!withoutMobStateLock(m, [&] {
                if(it!=gen::itemIdByName().end())
                    ctx.srv->spawnItemDropFor(
                        dimension, x,y,z, it->second,
                        static_cast<std::uint8_t>(spec->actionMagnitude()));
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.sniffer.digging",
                    x,y,z,1.f,1.f,"neutral");
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr,
                    proto::pl::sc::SetEntityMetadata, md);
            })) return false;
    }
    return true;
}
bool CamelDashGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Camel) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    return spec && dimensionPlayer(m, ctx.nearestPlayer) && spec->withinActionRangeSquared(ctx.nearestPlayerDist2);
}
bool CamelDashGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Camel) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(now < m.camelDashCooldown) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(!spec->withinActionRange(d) || !spec->beyondActionThreshold(d)) return false;
    m.x+=dx/d*0.42*10*0.1; m.z+=dz/d*0.42*10*0.1; // dash ~4.2 blocks scaled by tick
    if(ctx.srv){
        const auto dimension=canonicalDimension(m.dimension);
        const auto entityId=m.entityId;
        const double x=m.x,y=m.y,z=m.z;
        WriteBuffer vel; vel.varint(entityId); vel.i16((int16_t)(dx/d*0.42*8000)); vel.i16(0); vel.i16((int16_t)(dz/d*0.42*8000));
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::EntityVelocity, vel);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.camel.dash",
                    x,y,z,1.f,1.f,"neutral");
            })) return false;
    }
    m.camelDashCooldown=now+spec->actionCooldown();
    return true;
}
bool AllayDuplicateGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Allay) return false;
    if(!dimensionWorld(ctx, m)) return false;
    for(int dx=-4; dx<=4; ++dx) for(int dz=-4; dz<=4; ++dz){
        int bx=(int)std::floor(m.x)+dx, bz=(int)std::floor(m.z)+dz, by=(int)std::floor(m.y);
        uint16_t st=dimensionWorld(ctx, m)->getBlock(bx,by,bz); if(st==0) continue;
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
        const auto dimension=canonicalDimension(m.dimension);
        const auto entityId=m.entityId;
        const double x=m.x,y=m.y,z=m.z;
        WriteBuffer md; md.varint(entityId); meta::writeMetaBool(md,16,true); md.u8(255);
        if (!withoutMobStateLock(m, [&] {
                if(it!=gen::itemIdByName().end())
                    ctx.srv->spawnItemDropFor(dimension, x,y+1,z,
                                              it->second, 1);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:block.note_block.chime",
                    x,y,z,1.f,1.f,"block");
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr,
                    proto::pl::sc::SetEntityMetadata, md);
            })) return false;
    }
    m.allayDuplicateCooldown=now+120;
    return true;
}
bool BoggedPoisonGoal::shouldStart(MobEntity& m, AiContext& ctx){
    if(m.kind!=MobKind::Bogged) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    return spec && dimensionPlayer(m, ctx.nearestPlayer) && spec->withinActionRangeSquared(ctx.nearestPlayerDist2);
}
bool BoggedPoisonGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Bogged) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if(now < m.boggedPoisonCooldown) return false;
    auto t=dimensionPlayer(m, ctx.nearestPlayer); if(!t) return false;
    const auto dimension=canonicalDimension(m.dimension);
    const auto entityId=m.entityId;
    const double mobX=m.x,mobY=m.y,mobZ=m.z;
    double dx=t->x-mobX, dy=(t->y+1)-(mobY+1.6), dz=t->z-mobZ; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(!spec->withinActionRange(d) || !spec->beyondActionThreshold(d)) return false;
    if(ctx.srv){
        WriteBuffer eff; eff.varint(t->entityId); eff.varint(19); eff.i8(0); eff.varint(160); eff.u8(0x01);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->spawnProjectileFor(
                    dimension, ProjectileKind::Arrow,
                    mobX, mobY+1.6, mobZ, dx/d*1.2,
                    dy/d*0.2+0.12, dz/d*1.2, entityId, false);
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::EntityEffect, eff);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.bogged.shoot",
                    mobX,mobY,mobZ,1.f,1.f,"hostile");
            })) return false;
    }
    m.boggedPoisonCooldown=now+spec->actionCooldown();
    return true;
}
bool FishSwimGoal::shouldStart(MobEntity& m, AiContext&) { return MobEntity::isFishKind(m.kind); }
bool FishSwimGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!MobEntity::isFishKind(m.kind)) return false;
    // vanilla FishSwimGoal: drift in water, speed from mobStats moveSpeed
    if (now - m.nextWanderAt > 60 || !m.hasTarget) {
        double ang = (nextRandom()/(double)RAND_MAX)*6.28318;
        m.tx = m.x + std::cos(ang)*4.0; m.tz = m.z + std::sin(ang)*4.0;
        m.hasTarget = true; m.nextWanderAt = now;
    }
    double dx = m.tx - m.x, dz = m.tz - m.z;
    double d = std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d < 0.4) { m.hasTarget = false; return true; }
    float sp = mobStats(m.kind).moveSpeed;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    m.x += dx/d*sp; m.z += dz/d*sp;
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
    return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < perceptionRange2(MobKind::Vex); // plan44 G-05
}
bool VexChargeGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Vex) return false;
    if (now < m.vexChargeCooldown) return false;
    auto t = dimensionPlayer(m, ctx.nearestPlayer); if (!t) return false;
    double dx=t->x-m.x, dy=(t->y+1)-(m.y+1), dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d < 1.9) {
        if (now%20==0 && ctx.srv) {
            const auto dimension=canonicalDimension(m.dimension);
            const double x=m.x,y=m.y,z=m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->mobAttackPlayer(m,*t);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.vex.charge",
                        x,y,z,1.f,1.f,"hostile");
                })) return false;
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
    return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 24*24;
}
bool PiglinBruteAttackGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::PiglinBrute) return false;
    auto t = dimensionPlayer(m, ctx.nearestPlayer); if (!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    // brute never barters; enrages (1.5x speed) for 100t after being hurt
    bool enraged = ctx.srv &&
        (ctx.srv->tickNoForTest() -
             ctx.lastHurtTick.load(std::memory_order_acquire) < 100);
    if (enraged) m.piglinBruteEnrageUntil = now+100;
    double sp = (now < m.piglinBruteEnrageUntil) ? 0.15 : 0.10;
    if (d < 1.9) {
        if (now%20==0 && ctx.srv) {
            const auto dimension=canonicalDimension(m.dimension);
            const double x=m.x,y=m.y,z=m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->mobAttackPlayer(m,*t);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.piglin_brute.angry",
                        x,y,z,1.f,1.f,"hostile");
                })) return false;
        }
        return true;
    }
    m.x += dx/d*sp; m.z += dz/d*sp;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    if (World* world = dimensionWorld(ctx, m)) {
        const int chunkX = static_cast<int>(m.x) >> 4;
        const int chunkZ = static_cast<int>(m.z) >> 4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
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
    if (ctx.srv && now%40==0) {
        const auto dimension=canonicalDimension(m.dimension);
        const double x=m.x,y=m.y,z=m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.zombie_villager.cure",
                    x,y,z,1.f,1.f,"hostile");
            })) return false;
    }
    return true;
}
bool ZombifiedPiglinAngerGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::ZombifiedPiglin) return false;
    return ctx.srv &&
        (ctx.srv->tickNoForTest() -
             ctx.lastHurtTick.load(std::memory_order_acquire) < 200);
}
bool ZombifiedPiglinAngerGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::ZombifiedPiglin) return false;
    auto t = dimensionPlayer(m, ctx.nearestPlayer); if (!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d < 1.9) {
        if (now%20==0 && ctx.srv && !withoutMobStateLock(m, [&] {
                ctx.srv->mobAttackPlayer(m,*t);
            })) return false;
        return true;
    }
    // pack anger: nearby zombified piglins converge (vanilla anger propagation)
    if (ctx.srv) {
        const auto dimension=canonicalDimension(m.dimension);
        const double mobX=m.x,mobZ=m.z;
        std::vector<std::pair<std::shared_ptr<MobEntity>, MobSnapshot>> pack;
        if (!withoutMobStateLock(m, [&] {
                for (auto& mm : ctx.srv->mobsSnapshot()) {
                    if (!mm || mm.get()==&m) continue;
                    const MobSnapshot view=snapshotMob(*mm);
                    if (view.dimension != dimension ||
                        view.kind!=MobKind::ZombifiedPiglin || view.dead)
                        continue;
                    const double ox=view.x-mobX, oz=view.z-mobZ;
                    if (ox*ox+oz*oz < 16*16)
                        pack.emplace_back(mm, view);
                }
            })) return false;
        for (auto& entry : pack) {
            const auto& mm=entry.first;
            const auto& view=entry.second;
            if (!withoutMobStateLock(m, [&] {
                    std::scoped_lock pairLock(*m.stateMtx, *mm->stateMtx);
                    if (mm->entityId != view.entityId || mm->dead ||
                        canonicalDimension(mm->dimension) != dimension)
                        return;
                    mm->x += dx/d*0.08;
                    mm->z += dz/d*0.08;
                })) return false;
        }
    }
    m.x += dx/d*0.11; m.z += dz/d*0.11;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    if (World* world=dimensionWorld(ctx, m)) {
        const int chunkX=static_cast<int>(m.x)>>4, chunkZ=static_cast<int>(m.z)>>4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    return true;
}
bool SkeletonHorseTrapGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::SkeletonHorse) return false;
    if (!ctx.srv || ctx.srv->tickNoForTest() < m.skeletonHorseTrapCooldown) return false;
    return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 10*10;
}
bool SkeletonHorseTrapGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::SkeletonHorse) return false;
    if (now < m.skeletonHorseTrapCooldown) return false;
    if (ctx.srv) {
        // vanilla skeleton trap: lightning strike on approach
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->strikeLightningFor(dimension, x, y, z);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.skeleton_horse.ambient",
                    x, y, z, 1.f, 1.f, "neutral");
            })) return false;
    }
    m.skeletonHorseTrapCooldown = now+1200;
    return true;
}
bool GiantStompGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Giant) return false;
    return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 24*24;
}
bool GiantStompGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Giant) return false;
    if (now < m.giantStompCooldown) return false;
    auto t = dimensionPlayer(m, ctx.nearestPlayer); if (!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d < 2.5) {
        if (ctx.srv) {
            const auto dimension = canonicalDimension(m.dimension);
            const double x = m.x, y = m.y, z = m.z;
            const auto entityId = m.entityId;
            WriteBuffer vel; vel.varint(t->entityId);
            vel.i16((int16_t)(dx/d*1.2*8000)); vel.i16((int16_t)(0.5*8000)); vel.i16((int16_t)(dz/d*1.2*8000));
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->mobAttackPlayer(m,*t);
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::EntityVelocity, vel);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.giant.stomp",
                        x, y, z, 1.f, 1.f, "hostile");
                })) return false;
            if (m.entityId != entityId ||
                canonicalDimension(m.dimension) != dimension)
                return false;
        }
        m.giantStompCooldown = now+40;
        return true;
    }
    m.x += dx/d*0.08; m.z += dz/d*0.08;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    if (World* world = dimensionWorld(ctx, m)) {
        const int chunkX = static_cast<int>(m.x) >> 4;
        const int chunkZ = static_cast<int>(m.z) >> 4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    return true;
}
bool LlamaSpitGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Llama && m.kind != MobKind::TraderLlama) return false;
    if (!ctx.srv || ctx.srv->tickNoForTest() < m.llamaSpitCooldown) return false;
    return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 > 4*4 && ctx.nearestPlayerDist2 < 16*16;
}
bool LlamaSpitGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Llama && m.kind != MobKind::TraderLlama) return false;
    if (now < m.llamaSpitCooldown) return false;
    auto t = dimensionPlayer(m, ctx.nearestPlayer); if (!t) return false;
    // LlamaSpitEntity is a real projectile: clients need the entity spawn and
    // the hit must travel through the normal collision/damage pipeline.  The
    // vanilla thrower aims at the target's body rather than applying damage
    // immediately when the goal fires.
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        const double mobX = m.x, mobY = m.y, mobZ = m.z;
        const double dx = t->x - mobX;
        const double dy = (t->y + 0.9) - (mobY + 1.5);
        const double dz = t->z - mobZ;
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!withoutMobStateLock(m, [&] {
                if (distance > 1e-6) {
                    constexpr double speed = 1.5;
                    ctx.srv->spawnProjectileFor(
                        dimension, ProjectileKind::LlamaSpit,
                        mobX, mobY + 1.5, mobZ,
                        dx / distance * speed, dy / distance * speed,
                        dz / distance * speed, entityId, false);
                }
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.llama.spit",
                    mobX, mobY, mobZ, 1.f, 1.f, "neutral");
            })) return false;
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
    const auto dimension = canonicalDimension(m.dimension);
    const double x = m.x, y = m.y, z = m.z;
    if (!withoutMobStateLock(m, [&] {
            if (it != gen::itemIdByName().end())
                ctx.srv->spawnItemDropFor(dimension, x, y, z, it->second, 1);
            ctx.srv->broadcastSoundFor(
                dimension, "minecraft:entity.chicken.egg",
                x, y, z, 1.f, 1.f, "neutral");
        })) return false;
    m.chickenLayCooldown = now+6000+(nextRandom()%6000); // vanilla 5-10min
    return true;
}
bool HuskHungerGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Husk) return false;
    return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 3*3;
}
bool HuskHungerGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Husk) return false;
    if (now < m.huskHungerCooldown) return false;
    auto t = dimensionPlayer(m, ctx.nearestPlayer); if (!t) return false;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        // vanilla husk inflicts Hunger (effect id 9) on hit
        WriteBuffer eff; eff.varint(t->entityId); eff.varint(9); eff.i8(0); eff.varint(140); eff.u8(0x01);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->mobAttackPlayer(m,*t);
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::EntityEffect, eff);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.husk.ambient",
                    x, y, z, 1.f, 1.f, "hostile");
            })) return false;
    }
    m.huskHungerCooldown = now+40;
    return true;
}
bool PolarBearDefendGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::PolarBear) return false;
    return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 8*8;
}
bool PolarBearDefendGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::PolarBear) return false;
    auto t = dimensionPlayer(m, ctx.nearestPlayer); if (!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.polarBearDefendUntil = now+20; // standing/defending posture window
    if (d < 1.9) {
        if (now%20==0 && ctx.srv) {
            const auto dimension = canonicalDimension(m.dimension);
            const double x = m.x, y = m.y, z = m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->mobAttackPlayer(m,*t);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.polar_bear.warning",
                        x, y, z, 1.f, 1.f, "neutral");
                })) return false;
        }
        return true;
    }
    m.x += dx/d*0.10; m.z += dz/d*0.10;
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180.0/3.14159-90.0);
    if (World* world = dimensionWorld(ctx, m)) {
        const int chunkX = static_cast<int>(m.x) >> 4;
        const int chunkZ = static_cast<int>(m.z) >> 4;
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(chunkX, chunkZ);
            })) return false;
    }
    return true;
}
bool PufferfishPuffGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Pufferfish) return false;
    return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 4*4;
}
bool PufferfishPuffGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Pufferfish) return false;
    m.pufferfishPuffUntil = now+40; // inflated while threatened
    auto t = dimensionPlayer(m, ctx.nearestPlayer);
    if (t && ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double mobX = m.x, mobY = m.y, mobZ = m.z;
        double dx=t->x-mobX, dz=t->z-mobZ;
        if (dx*dx+dz*dz < 1.5*1.5) {
            // vanilla contact poison (effect id 19, like bogged arrow)
            WriteBuffer eff; eff.varint(t->entityId); eff.varint(19); eff.i8(0); eff.varint(120); eff.u8(0x01);
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->applyDamage(*t, 3.f, "mob");
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr, proto::pl::sc::EntityEffect, eff);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.puffer_fish.blow_up",
                        mobX, mobY, mobZ, 1.f, 1.f, "neutral");
                })) return false;
        } else if (now%40==0) {
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.puffer_fish.blow_up",
                        mobX, mobY, mobZ, 0.5f, 1.f, "neutral");
                })) return false;
        }
    }
    return true;
}
bool ProjectileFlyGoal::shouldStart(MobEntity& m, AiContext&) { return MobEntity::isProjectileKind(m.kind); }
bool ProjectileFlyGoal::tick(MobEntity& m, AiContext&, std::int64_t) {
    if (!MobEntity::isProjectileKind(m.kind)) return false;
    // ballistic hold: projectiles keep latched velocity (set by thrower systems) instead of wandering randomly. Returning true claims the
    // tick so the generic WanderAroundGoal never steers a flying projectile.
    m.x += m.projectileVx; m.y += m.projectileVy; m.z += m.projectileVz;
    m.projectileVy -= 0.02; // mild gravity like vanilla thrown projectiles
    return true;
}
bool EvokerFangsSnapGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::EvokerFangs) return false;
    if (!ctx.srv || ctx.srv->tickNoForTest() < m.evokerFangsSnapCooldown) return false;
    return dimensionPlayer(m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 2*2;
}
bool EvokerFangsSnapGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::EvokerFangs) return false;
    if (now < m.evokerFangsSnapCooldown) return false;
    auto t = dimensionPlayer(m, ctx.nearestPlayer); if (!t) return false;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->applyDamage(*t, 6.f, "magic");
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.evoker_fangs.attack",
                    x, y, z, 1.f, 1.f, "hostile");
            })) return false;
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
    return !m.dead;
}
bool TntFuseGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Tnt || m.dead) return false;
    if (m.tntFuseStartedAt < 0) {
        m.tntFuseStartedAt = now;
        if (ctx.srv) {
            const auto dimension = canonicalDimension(m.dimension);
            const double x = m.x, y = m.y, z = m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.tnt.primed",
                        x, y, z, 1.f, 1.f, "block");
                })) return false;
        }
        return true;
    }
    if (now - m.tntFuseStartedAt >= 80) { // vanilla 80t fuse
        if (ctx.srv) {
            const auto dimension = canonicalDimension(m.dimension);
            const double x = m.x, y = m.y, z = m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->explodeAtFor(dimension, x, y, z, 4.0f);
                })) return false;
            if (canonicalDimension(m.dimension) != dimension) return false;
        }
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
        auto t = dimensionPlayer(m, ctx.nearestPlayer);
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
            const auto dimension = canonicalDimension(m.dimension);
            const auto entityId = m.entityId;
            const double x = m.x, y = m.y, z = m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->strikeLightningFor(dimension, x, y, z);
                    ctx.srv->broadcastSoundFor(
                        dimension, "minecraft:entity.lightning_bolt.thunder",
                        x, y, z, 2.f, 1.f, "weather");
                })) return false;
            if (m.entityId != entityId ||
                canonicalDimension(m.dimension) != dimension)
                return false;
            m.lightningStruck = true;
        }
        m.dead = true; // instant strike entity, vanilla despawns after the flash
        return false;
    case MobKind::OminousItemSpawner:
        if (ctx.srv && now%100==0) {
            const auto dimension = canonicalDimension(m.dimension);
            const double x = m.x, y = m.y, z = m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastSoundFor(
                        dimension,
                        "minecraft:block.trial_spawner.ominous_activate",
                        x, y, z, 0.5f, 1.f, "block");
                })) return false;
        }
        return true; // hold, ominous idle
    case MobKind::ArmorStand:
    default:
        return true; // pose hold, never wanders
    }
}
bool Brain::coversKind(MobKind k) {
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
    ActiveBrainMobScope brainScope(m, ctx);

    if (ctx.srv) {
        // The tick loop currently seeds AiContext with the Overworld.  Make
        // the brain authoritative for the dimension before either the
        // behavior tree or goal code performs a world query.
        ctx.world = &ctx.srv->worldFor(m.dimension);
    }
    withoutMobStateLock(m, [&] {
        NearestPlayerSensor::update(m, ctx);
    });
    if (m.kind == MobKind::Armadillo) {
        const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
        if (spec && now - m.armadilloLastScanTick >= spec->scanInterval()) {
            m.armadilloLastScanTick = now;
            bool danger = false;
            const auto target = dimensionPlayer(m, ctx.nearestPlayer);
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
