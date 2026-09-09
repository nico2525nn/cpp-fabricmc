#include "BehaviorTree.hpp"
#include "GameServer.hpp"
#include "AiBehaviorSupport.hpp"
#include "World.hpp"
#include "MetadataTypes.hpp"
#include "Particles.hpp"
#include "../worldgen/MultiNoise.hpp"
#include <mutex>
#include <type_traits>
#include <utility>

namespace cppfm {

namespace {
using namespace ai_detail;
using MobStateLock = std::unique_lock<std::recursive_mutex>;
thread_local MobStateLockContext activeMobStateLockContext{};
}

// mobsTick() owns the live MobEntity lock while the brain evaluates direct
// state transitions.  BehaviorTree actions also perform side effects, though,
// and those must not run while that lock is held: a packet send or a JVM call
// may re-enter native code and need to inspect the same entity.  The tick loop
// installs its lock here for this thread only; actions use the small helper
// below around world/network/JVM operations.  A tree used by a standalone
// test has no installed lock and simply executes the operation normally.
MobStateLockContext setBehaviorTreeMobStateLock(
    const MobEntity* mob, MobStateLock* lock) noexcept {
    const MobStateLockContext previous = activeMobStateLockContext;
    activeMobStateLockContext = {
        mob, mob != nullptr ? mob->entityId : 0, lock};
    return previous;
}

MobStateLockContext currentMobStateLockContext() noexcept {
    return activeMobStateLockContext;
}

bool runWithoutMobStateLock(
    const MobEntity& expectedMob,
    const std::function<void()>& operation) {
    if (!operation) return true;
    const MobStateLockContext context = activeMobStateLockContext;
    if (!context.lock || !context.lock->owns_lock()) {
        operation();
        return true;
    }
    if (context.mob != &expectedMob ||
        context.entityId != expectedMob.entityId)
        return false;

    context.lock->unlock();
    try {
        operation();
    } catch (...) {
        context.lock->lock();
        throw;
    }
    context.lock->lock();
    // A callback may have replaced or invalidated the source entity while the
    // lock was released.  The caller can use the false result to discard any
    // stale post-callback mutation.
    return context.mob == &expectedMob &&
           context.entityId == expectedMob.entityId;
}

void runWithoutMobStateLock(const std::function<void()>& operation) {
    if (!operation) return;
    const MobStateLockContext context = activeMobStateLockContext;
    if (!context.lock || !context.lock->owns_lock()) {
        operation();
        return;
    }
    // This overload is retained for existing server APIs whose signatures do
    // not carry a source Mob.  A live context without an owner is not safe to
    // unlock; source-aware AI code uses the overload above.
    if (!context.mob) return;

    context.lock->unlock();
    try {
        operation();
    } catch (...) {
        context.lock->lock();
        throw;
    }
    context.lock->lock();
}

bool mobStateLockOwnedByCurrentThread() noexcept {
    return activeMobStateLockContext.mob != nullptr &&
           activeMobStateLockContext.lock != nullptr &&
           activeMobStateLockContext.lock->owns_lock();
}

BTStatus IsHurtCondition::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    if (m.health <= 0) return BTStatus::Failure;
    const float maxH = mobStats(m.kind).maxHealth;
    if (m.health < maxH * 0.9f) return BTStatus::Success;
    if (ctx.lastHurtTick >=0 && ctx.srv && ctx.srv->tickNoForTest() - ctx.lastHurtTick < 20) return BTStatus::Success;
    return BTStatus::Failure;
}

BTStatus IsPlayerLookingCondition::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    AiPlayerSnapshot p;
    if (!snapshotPlayer(m, ctx, ctx.nearestPlayer, p))
        return BTStatus::Failure;
    if (p.gamemode==1 || p.gamemode==3 || p.hasPumpkin) return BTStatus::Failure;
    double dx = m.x - p.x;
    double dy = (m.y+1.6) - (p.y + 1.62);
    double dz = m.z - p.z;
    double len = std::sqrt(dx*dx+dy*dy+dz*dz);
    if (len < 1e-6 || len > 64) return BTStatus::Failure;
    dx/=len; dy/=len; dz/=len;
    double yawRad = p.yaw * 3.1415926535 / 180.0;
    double pitchRad = p.pitch * 3.1415926535 / 180.0;
    double lx = -std::sin(yawRad) * std::cos(pitchRad);
    double ly = -std::sin(pitchRad);
    double lz =  std::cos(yawRad) * std::cos(pitchRad);
    double dot = dx*lx + dy*ly + dz*lz;
    return dot > 0.99 ? BTStatus::Success : BTStatus::Failure;
}

BTStatus MoveToPlayerAction::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    AiPlayerSnapshot t;
    if (!snapshotPlayer(m, ctx, ctx.nearestPlayer, t))
        return BTStatus::Failure;
    World* world = dimensionWorld(ctx, m);
    double dx = t.x - m.x, dz = t.z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d < 1.9) return BTStatus::Success;
    if (d > 24) return BTStatus::Failure;
    m.yaw = (float)(std::atan2(dz,dx)*180.0/3.1415926535 - 90.0);
    m.x += dx/d * 0.10;
    m.z += dz/d * 0.10;
    if (ctx.srv && world) {
        groundSnap(ctx, m);
    }
    return BTStatus::Running;
}

BTStatus AttackPlayerAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    AiPlayerSnapshot t;
    if (!snapshotPlayer(m, ctx, ctx.nearestPlayer, t))
        return BTStatus::Failure;
    double dx = t.x - m.x, dz = t.z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d > 2.2) return BTStatus::Failure;
    if (now % 20 == 0 && ctx.srv)
        withoutMobStateLock(m, [&] { ctx.srv->mobAttackPlayer(m, *t.player); });
    return BTStatus::Success;
}

BTStatus TeleportRandomAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (now - m.lastTeleportTick < 20) return BTStatus::Failure;
    World* world = dimensionWorld(ctx, m);
    if (!world) return BTStatus::Failure;
    auto emitTeleport = [&](std::int8_t dimension, std::int32_t entityId,
                            double x, double y, double z, float yaw,
                            double oldX, double oldY, double oldZ) {
        return withoutMobStateLock(m, [&] {
            ctx.srv->broadcastSoundFor(
                dimension, "minecraft:entity.enderman.teleport",
                x, y, z, 1.f, 1.f, "hostile");
            WriteBuffer tp;
            tp.varint(entityId);
            tp.f64(x); tp.f64(y); tp.f64(z);
            tp.f32(yaw); tp.f32(0); tp.boolean(true);
            ctx.srv->broadcastPacketExceptInDimension(
                dimension, nullptr, proto::pl::sc::EntityTeleport, tp);
            for (int i=0; i<8; ++i) {
                const WriteBuffer pt = makeWorldParticlesBody(
                    oldX + (nextRandom()/(double)RAND_MAX-0.5)*1.5,
                    oldY + nextRandom()/(double)RAND_MAX*2.0,
                    oldZ + (nextRandom()/(double)RAND_MAX-0.5)*1.5,
                    0, 0, 0, 0.1f, 1, ParticleId::portal, {}, true, false);
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::WorldParticles, pt);
            }
        });
    };
    for (int attempt=0; attempt<64; ++attempt) {
        double nx = m.x + (nextRandom()/(double)RAND_MAX*64 -32);
        double nz = m.z + (nextRandom()/(double)RAND_MAX*64 -32);
        double ny = m.y + (nextRandom()/(double)RAND_MAX*32 -16);
        int ix = (int)std::floor(nx);
        int iz = (int)std::floor(nz);
        int iy = (int)std::floor(ny);
        if (!withoutMobStateLock(m, [&] {
                world->generateChunkIfMissing(ix>>4, iz>>4);
            })) return BTStatus::Failure;
        // try around iy first, fall back to column search if needed
        for (int dy=-4; dy<=4; ++dy) {
            int tryY = iy + dy;
            if (tryY < kMinY || tryY > kMinY+320) continue;
            std::uint16_t a1 = world->getBlock(ix, tryY, iz);
            std::uint16_t a2 = world->getBlock(ix, tryY+1, iz);
            std::uint16_t below = world->getBlock(ix, tryY-1, iz);
            if (a1==0 && a2==0 && below!=0) {
                double ox=m.x, oy=m.y, oz=m.z;
                m.x = ix + 0.5; m.z = iz + 0.5; m.y = tryY + 0.5;
                const auto dimension = canonicalDimension(m.dimension);
                const auto entityId = m.entityId;
                const auto yaw = m.yaw;
                const double newX = m.x, newY = m.y, newZ = m.z;
                if (ctx.srv && !emitTeleport(
                        dimension, entityId, newX, newY, newZ, yaw,
                        ox, oy, oz)) return BTStatus::Failure;
                // A callback may have transferred or moved this entity while
                // packets were emitted.  Do not overwrite its new state.
                if (m.entityId == entityId &&
                    canonicalDimension(m.dimension) == dimension &&
                    m.x == newX && m.y == newY && m.z == newZ)
                    m.lastTeleportTick = now;
                return BTStatus::Success;
            }
        }
        // fallback column search if not found around ny
        int col = 4;
        bool found=false;
        if (!withoutMobStateLock(m, [&] {
                world->withChunk(ix>>4, iz>>4,[&](const Chunk& c){
                    for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, iz&15, ix&15)]!=0){col=ry+1; found=true; break;}
                });
            })) return BTStatus::Failure;
        if(!found) continue;
        int feetY = kMinY + col;
        std::uint16_t a1 = world->getBlock(ix, feetY, iz);
        std::uint16_t a2 = world->getBlock(ix, feetY+1, iz);
        std::uint16_t below = world->getBlock(ix, feetY-1, iz);
        if (a1==0 && a2==0 && below!=0) {
            double ox=m.x, oy=m.y, oz=m.z;
            m.x = ix + 0.5; m.z = iz + 0.5; m.y = feetY + 1.0;
            const auto dimension = canonicalDimension(m.dimension);
            const auto entityId = m.entityId;
            const auto yaw = m.yaw;
            const double newX = m.x, newY = m.y, newZ = m.z;
            if (ctx.srv && !emitTeleport(
                    dimension, entityId, newX, newY, newZ, yaw,
                    ox, oy, oz)) return BTStatus::Failure;
            if (m.entityId == entityId &&
                canonicalDimension(m.dimension) == dimension &&
                m.x == newX && m.y == newY && m.z == newZ)
                m.lastTeleportTick = now;
            return BTStatus::Success;
        }
    }
    return BTStatus::Failure;
}

BTStatus PickupBlockAction::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    if (m.carriedBlock !=0) return BTStatus::Failure;
    World* world = dimensionWorld(ctx, m);
    if (!world) return BTStatus::Failure;
    if (nextRandom()%1000 != 0) return BTStatus::Failure;
    static const char* holdable[] = {
        "minecraft:grass_block","minecraft:dirt","minecraft:coarse_dirt","minecraft:podzol","minecraft:rooted_dirt",
        "minecraft:dirt_path","minecraft:mud","minecraft:clay","minecraft:sand","minecraft:red_sand",
        "minecraft:gravel","minecraft:soul_sand","minecraft:soul_soil","minecraft:snow","minecraft:snow_block",
        "minecraft:pumpkin","minecraft:carved_pumpkin","minecraft:melon","minecraft:brown_mushroom","minecraft:red_mushroom",
        "minecraft:mushroom_stem","minecraft:brown_mushroom_block","minecraft:red_mushroom_block","minecraft:crimson_fungus","minecraft:warped_fungus",
        "minecraft:crimson_nylium","minecraft:warped_nylium","minecraft:nether_wart_block","minecraft:warped_wart_block","minecraft:cactus",
        "minecraft:tnt","minecraft:mycelium","minecraft:moss_block","minecraft:pale_moss_block","minecraft:muddy_mangrove_roots",
        "minecraft:dandelion","minecraft:poppy","minecraft:blue_orchid","minecraft:allium","minecraft:azure_bluet",
        "minecraft:red_tulip","minecraft:orange_tulip","minecraft:white_tulip","minecraft:pink_tulip","minecraft:oxeye_daisy",
        "minecraft:cornflower","minecraft:lily_of_the_valley","minecraft:wither_rose","minecraft:sunflower","minecraft:lilac",
        "minecraft:rose_bush","minecraft:peony","minecraft:pitcher_plant","minecraft:torchflower","minecraft:spore_blossom",
        "minecraft:dead_bush","minecraft:fern","minecraft:short_grass","minecraft:vine","minecraft:lily_pad",
        "minecraft:mangrove_propagule","minecraft:bamboo","minecraft:azalea","minecraft:flowering_azalea","minecraft:big_dripleaf",
        "minecraft:small_dripleaf","minecraft:chorus_flower","minecraft:chorus_plant","minecraft:crimson_roots","minecraft:warped_roots"
    };
    auto isHoldable = [&](std::string_view n)->bool{
        for(auto h: holdable) if(n==h) return true;
        // fallback: allow any non-hard hardness < 0.5 and not bedrock/obsidian
        if(n=="minecraft:bedrock"||n=="minecraft:obsidian") return false;
        return false;
    };
    // try nearby positions
    for(int tries=0; tries<8; ++tries){
        int bx=(int)std::floor(m.x)+(nextRandom()%5-2);
        int by=(int)std::floor(m.y)+(nextRandom()%3-1);
        int bz=(int)std::floor(m.z)+(nextRandom()%5-2);
        std::uint16_t st = world->getBlock(bx, by, bz);
        if (st==0) continue;
        auto* def = gen::blockByState(st);
        if (!def) continue;
        if (!isHoldable(def->name)) continue;
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        if (!withoutMobStateLock(m, [&] {
                world->setBlock(bx, by, bz, 0);
            })) return BTStatus::Failure;
        if (m.entityId != entityId ||
            canonicalDimension(m.dimension) != dimension)
            return BTStatus::Failure;
        m.carriedBlock = st;
        if (ctx.srv) {
            WriteBuffer md;
            md.varint(entityId);
            meta::writeMetaOptBlockState(md, 15, st);
            md.u8(255);
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->broadcastBlockChangeFor(
                        dimension, bx, by, bz, 0);
                    // SetEntityMetadata for carriedBlock (index 15, Yarn
                    // EndermanEntity CARRIED_BLOCK Optional<BlockState>).
                    ctx.srv->broadcastPacketExceptInDimension(
                        dimension, nullptr,
                        proto::pl::sc::SetEntityMetadata, md);
                })) return BTStatus::Failure;
        }
        return BTStatus::Success;
    }
    return BTStatus::Failure;
}

BTStatus StareAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    AiPlayerSnapshot p;
    if (!snapshotPlayer(m, ctx, ctx.nearestPlayer, p))
        return BTStatus::Failure;
    if (p.gamemode==1 || p.gamemode==3 || p.hasPumpkin)
        return BTStatus::Failure;
    double dx = p.x - m.x, dz = p.z - m.z;
    m.yaw = (float)(std::atan2(dz,dx)*180.0/3.1415926535 - 90.0);
    // set anger
    m.angerTargetEntityId = p.entityId;
    m.angryUntilTick = now + 100 + nextRandom()%100;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        WriteBuffer md;
        md.varint(entityId);
        meta::writeMetaBool(md, 16, true);
        md.u8(255);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::SetEntityMetadata, md);
            })) return BTStatus::Failure;
    }
    return BTStatus::Success;
}

BTStatus WitherSkullAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Wither) return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    AiPlayerSnapshot t;
    if (!snapshotPlayer(m, ctx, ctx.nearestPlayer, t))
        return BTStatus::Failure;
    const std::int8_t dimension = canonicalDimension(m.dimension);
    const std::int32_t entityId = m.entityId;
    const double mobX = m.x, mobY = m.y, mobZ = m.z;
    const float mobYaw = m.yaw;
    const double dx = t.x - mobX, dy = (t.y+1.0) - (mobY+1.5), dz = t.z - mobZ;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d>32) return BTStatus::Failure;
    double inv = 1.0/ (d+1e-6);
    if (ctx.srv) {
        const float maxH = mobStats(m.kind).maxHealth;
        const bool halfHealth = m.health <= maxH * 0.5f;
        if (!withoutMobStateLock(m, [&] {
            for (int burst=0; burst<3; ++burst) {
                const double spreadX = (burst==0?0:(burst==1?-0.35:0.35));
                const double spreadZ = (burst==0?0:(burst==1?0.35:-0.35));
                const double yawRad = mobYaw * 3.1415926535 / 180.0;
                const double offX = std::cos(yawRad)*spreadX - std::sin(yawRad)*spreadZ;
                const double offZ = std::sin(yawRad)*spreadX + std::cos(yawRad)*spreadZ;
                const double vx = dx*inv*1.1 + (nextRandom()/(double)RAND_MAX-0.5)*0.08;
                const double vz = dz*inv*1.1 + (nextRandom()/(double)RAND_MAX-0.5)*0.08;
                const double vy = dy*inv*0.6 + 0.2 + (nextRandom()/(double)RAND_MAX-0.5)*0.05;
                const double sx = mobX + offX;
                const double sz = mobZ + offZ;
                const bool charged = halfHealth && burst==0;
                ctx.srv->spawnProjectileFor(dimension, ProjectileKind::WitherSkull,
                                             sx, mobY+1.5, sz, vx, vy, vz,
                                             entityId, false, charged);
            }
            ctx.srv->broadcastSoundFor(dimension, "minecraft:entity.wither.shoot",
                                       mobX, mobY, mobZ, 1.0f, 1.0f,
                                       "hostile");
        })) return BTStatus::Failure;
    }
    m.witherSkullCooldown = (int)(now + 40 + nextRandom()%40);
    return BTStatus::Success;
}

BTStatus DragonBreathAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::EnderDragon) return BTStatus::Failure;
    if (m.dragonPhase == 0 && now > m.dragonPhaseUntil) {
        if (nextRandom()%100 < 12) { m.dragonPhase = 1; m.dragonPhaseUntil = now + 40; }
    }
    if (m.dragonPhase == 1) {
        double dx=-m.x, dz=-m.z;
        double d=std::sqrt(dx*dx+dz*dz);
        if (d<4) { m.dragonPhase=2; m.dragonPhaseUntil = now + 80; }
        else {
            m.x += dx/d * 0.18; m.z += dz/d * 0.18;
            m.y = 65;
            return BTStatus::Running;
        }
    }
    if (m.dragonPhase == 2) {
        if (now % 20 == 0 && ctx.srv) {
            const auto dimension = canonicalDimension(m.dimension);
            const auto entityId = m.entityId;
            const double x = m.x, y = m.y, z = m.z;
            const double vx = (nextRandom()/(double)RAND_MAX-0.5)*0.6;
            const double vz = (nextRandom()/(double)RAND_MAX-0.5)*0.6;
            withoutMobStateLock(m, [&] {
                ctx.srv->spawnProjectileFor(dimension,
                                             ProjectileKind::DragonFireball,
                                             x, y, z, vx, -0.3, vz,
                                             entityId, false);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.ender_dragon.shoot",
                    x, y, z, 2.f, 1.f, "hostile");
            });
        }
        if (now > m.dragonPhaseUntil) { m.dragonPhase=3; m.dragonPhaseUntil=now+30; }
        return BTStatus::Running;
    }
    if (m.dragonPhase == 3) {
        double ang = now * 0.04;
        double rx = std::cos(ang)*32, rz = std::sin(ang)*32;
        double dx=rx-m.x, dz=rz-m.z;
        m.x += dx*0.08; m.z += dz*0.08; m.y += (70-m.y)*0.05;
        if (now > m.dragonPhaseUntil) { m.dragonPhase=0; m.dragonPhaseUntil=now+120+nextRandom()%120; }
        return BTStatus::Running;
    }
    double ang = now * 0.03;
    double rx = std::cos(ang)*28, rz = std::sin(ang)*28;
    double dx=rx - m.x, dz=rz - m.z;
    m.x += dx*0.04; m.z += dz*0.04; m.y += (68 - m.y)*0.02;
    m.yaw = (float)(std::atan2(dz,dx)*180/3.14159 -90);
    if (nextRandom()%80==0 && ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        withoutMobStateLock(m, [&] {
            ctx.srv->broadcastSoundFor(
                dimension, "minecraft:entity.ender_dragon.flap",
                x, y, z, 1.f, 1.f, "hostile");
        });
    }
    return BTStatus::Running;
}

BTStatus BreedAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!m.inLove || now > m.loveUntilTick) return BTStatus::Failure;
    if (MobEntity::isBaby(m)) return BTStatus::Failure;
    if (!ctx.srv) return BTStatus::Failure;
    if (now < m.breedCooldownUntil) return BTStatus::Failure;
    if (now < m.loveUntilTick - 30*20 + 30) return BTStatus::Running;
    std::shared_ptr<MobEntity> partner;
    withoutMobStateLock(m, [&] { partner = ctx.srv->findLovePartner(m); });
    if (!partner || partner.get() == &m) return BTStatus::Running;

    // The partner is a separately synchronized entity.  Snapshot it while
    // the current Mob lock is released; taking partner->stateMtx while m's
    // outer lock is held would recreate the A->B/B->A breeding deadlock.
    MobSnapshot partnerState;
    if (!withoutMobStateLock(m, [&] {
            partnerState = snapshotMob(*partner);
        })) return BTStatus::Running;
    if (partnerState.dimension != canonicalDimension(m.dimension) ||
        partnerState.kind != m.kind || partnerState.dead ||
        !partnerState.inLove)
        return BTStatus::Running;

    const MobSnapshot selfState{
        m.entityId, canonicalDimension(m.dimension), m.kind,
        m.x, m.y, m.z, m.dead, m.inLove, m.breedCooldownUntil};
    double bx=(m.x+partnerState.x)/2.0;
    double bz=(m.z+partnerState.z)/2.0;
    const std::int8_t breedDimension = selfState.dimension;
    const std::int64_t reservedCooldown = now + 6000;

    // Acquire both locks only after the outer lock has been released.  The
    // standard deadlock-avoiding lock algorithm handles two simultaneous
    // breeding ticks without recursive self-locking.
    bool pairReserved = false;
    if (!withoutMobStateLock(m, [&] {
            std::scoped_lock pairLock(*m.stateMtx, *partner->stateMtx);
            if (!sameMobIdentity(m, selfState) || m.dead || !m.inLove ||
                now < m.breedCooldownUntil ||
                canonicalDimension(partner->dimension) != breedDimension ||
                partner->entityId != partnerState.entityId ||
                partner->kind != selfState.kind || partner->dead ||
                !partner->inLove || now < partner->breedCooldownUntil)
                return;
            bx = (m.x + partner->x) / 2.0;
            bz = (m.z + partner->z) / 2.0;
            pairReserved = true;
            m.inLove = false;
            partner->inLove = false;
            m.breedCooldownUntil = reservedCooldown;
            partner->breedCooldownUntil = reservedCooldown;
        })) return BTStatus::Running;
    if (!pairReserved) return BTStatus::Running;

    const double breedY = m.y;
    auto baby = std::make_shared<MobEntity>();
    baby->entityId = ctx.srv->nextEntityId();
    baby->kind = m.kind;
    baby->health = mobStats(m.kind).maxHealth;
    baby->age = -24000; // plan14 §3: 20 min
    baby->x = bx; baby->y = breedY; baby->z = bz;
    baby->dimension = breedDimension;
    bool spawnAllowed = true;
    if (ctx.srv->jvmRuntime()) {
        if (!withoutMobStateLock(m, [&] {
            spawnAllowed = ctx.srv->jvmRuntime()->onMobSpawn(
                *baby, baby->x, baby->y, baby->z);
        })) return BTStatus::Failure;
    }
    bool reservationValid = false;
    if (!withoutMobStateLock(m, [&] {
            std::scoped_lock pairLock(*m.stateMtx, *partner->stateMtx);
            reservationValid = sameMobIdentity(m, selfState) &&
                sameMobIdentity(*partner, partnerState) && !m.dead &&
                !partner->dead && !m.inLove && !partner->inLove &&
                m.breedCooldownUntil == reservedCooldown &&
                partner->breedCooldownUntil == reservedCooldown;
        })) return BTStatus::Failure;
    if (!spawnAllowed) {
        withoutMobStateLock(m, [&] {
            std::scoped_lock pairLock(*m.stateMtx, *partner->stateMtx);
            if (reservationValid && !m.inLove && !partner->inLove &&
                m.breedCooldownUntil == reservedCooldown &&
                partner->breedCooldownUntil == reservedCooldown) {
                m.inLove = true;
                partner->inLove = true;
                m.breedCooldownUntil = 0;
                partner->breedCooldownUntil = 0;
            }
        });
        return BTStatus::Failure;
    }
    if (!reservationValid) return BTStatus::Failure;

    if (!withoutMobStateLock(m, [&] {
            ctx.srv->addMob(baby);
            ctx.srv->broadcastMobSpawn(*baby);
        })) return BTStatus::Failure;
    const auto xp = static_cast<std::int32_t>(1 + (nextRandom()%7));
    withoutMobStateLock(m, [&] {
        ctx.srv->spawnXpOrbsFor(breedDimension, bx, breedY + 0.5, bz,
                                xp, nullptr);
    });
    {
        const AiPlayerSnapshot* best=nullptr; double bestD=64;
        for (const auto& view : ctx.playerViews) {
            if (!view.inPlay || view.dead || !sameDimension(m, view)) continue;
            const double dx=view.x-bx, dz=view.z-bz;
            double d2=dx*dx+dz*dz;
            if (d2<bestD*bestD) {
                bestD=std::sqrt(d2);
                best=&view;
            }
        }
        if (best) {
            Player* player = best->player;
            withoutMobStateLock(m, [&] {
                ctx.srv->onBredAnimals(player);
            });
        }
    }
    return BTStatus::Success;
}

BTStatus TradeAction::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    if (m.kind != MobKind::Villager) return BTStatus::Failure;
    AiPlayerSnapshot player;
    if (!snapshotPlayer(m, ctx, ctx.nearestPlayer, player))
        return BTStatus::Failure;
    double dx=player.x - m.x, dz=player.z - m.z;
    if (dx*dx+dz*dz > 36) return BTStatus::Failure;
    m.yaw = (float)(std::atan2(dz,dx)*180/3.14159 -90);
    return BTStatus::Success;
}

BTStatus WanderAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!m.hasTarget) {
        double ang = (nextRandom()/(double)RAND_MAX)*6.28318;
        double dist = 4 + (nextRandom()%8);
        m.tx = m.x + std::cos(ang)*dist;
        m.tz = m.z + std::sin(ang)*dist;
        m.hasTarget=true;
        m.nextWanderAt = now + 3000 + nextRandom()%4000;
    }
    double dx=m.tx-m.x, dz=m.tz-m.z;
    double d=std::sqrt(dx*dx+dz*dz);
    if (d<0.6 || now > m.nextWanderAt) { m.hasTarget=false; return BTStatus::Success; }
    m.yaw = (float)(std::atan2(dz,dx)*180/3.14159 -90);
    m.x += dx/d * 0.05;
    m.z += dz/d * 0.05;
    World* world = dimensionWorld(ctx, m);
    if (world && ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double queryX = m.x, queryZ = m.z;
        const int chunkX = static_cast<int>(queryX) >> 4;
        const int chunkZ = static_cast<int>(queryZ) >> 4;
        const int blockX = static_cast<int>(queryX);
        const int blockZ = static_cast<int>(queryZ);
        withoutMobStateLock(m, [&] {
            world->generateChunkIfMissing(chunkX, chunkZ);
        });
        int col=4;
        withoutMobStateLock(m, [&] {
            world->withChunk(chunkX, chunkZ,[&](const Chunk& c){
                for(int ry=kSectionsPerChunk*16-1; ry>=0; --ry) if(c.blocks[Chunk::index(ry>>4, ry&15, blockZ&15, blockX&15)]!=0){col=ry+1;break;}
            });
        });
        if (canonicalDimension(m.dimension) == dimension &&
            m.x == queryX && m.z == queryZ)
            m.y = kMinY + col + 1.0;
    }
    return BTStatus::Running;
}


BTStatus BlazeFireballAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Blaze) return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    AiPlayerSnapshot t;
    if (!snapshotPlayer(m, ctx, ctx.nearestPlayer, t))
        return BTStatus::Failure;
    const auto dimension = canonicalDimension(m.dimension);
    const auto entityId = m.entityId;
    const double mobX = m.x, mobY = m.y, mobZ = m.z;
    const double dx = t.x - mobX, dy = (t.y+1.0)-(mobY+1.0), dz = t.z - mobZ;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d>16 || d<4) return BTStatus::Failure;
    double inv = 1.0/(d+1e-6);
    double vx = dx*inv*1.0, vz = dz*inv*1.0, vy = dy*inv*0.2 + 0.1;
    if (ctx.srv && !withoutMobStateLock(m, [&] {
            ctx.srv->spawnProjectileFor(dimension, ProjectileKind::Fireball,
                                         mobX, mobY+1.0, mobZ, vx, vy, vz,
                                         entityId, false);
        })) return BTStatus::Failure;
    m.witherSkullCooldown = (int)(now + 30 + nextRandom()%30);
    return BTStatus::Success;
}

BTStatus GuardianBeamAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Guardian && m.kind != MobKind::ElderGuardian) return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    AiPlayerSnapshot t;
    if (!snapshotPlayer(m, ctx, ctx.nearestPlayer, t))
        return BTStatus::Failure;
    const auto dimension = canonicalDimension(m.dimension);
    const double mobX = m.x, mobY = m.y, mobZ = m.z;
    const double dx = t.x - mobX, dz = t.z - mobZ;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d>15) return BTStatus::Failure;
    if (ctx.srv && !withoutMobStateLock(m, [&] {
            ctx.srv->applyDamage(*t.player, 6.f, "magic");
            ctx.srv->broadcastSoundFor(
                dimension, "minecraft:entity.guardian.attack",
                mobX, mobY, mobZ, 1.f, 1.f, "hostile");
        })) return BTStatus::Failure;
    m.witherSkullCooldown = (int)(now + 60);
    return BTStatus::Success;
}

BTStatus GhastFireballAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Ghast) return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    AiPlayerSnapshot t;
    if (!snapshotPlayer(m, ctx, ctx.nearestPlayer, t))
        return BTStatus::Failure;
    const auto dimension = canonicalDimension(m.dimension);
    const auto entityId = m.entityId;
    const double mobX = m.x, mobY = m.y, mobZ = m.z;
    const double dx = t.x - mobX, dy = (t.y+1.0)-(mobY+1.5), dz = t.z - mobZ;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d>32) return BTStatus::Failure;
    double inv=1.0/(d+1e-6);
    double vx=dx*inv*1.0, vz=dz*inv*1.0, vy=dy*inv*0.2;
    if (ctx.srv && !withoutMobStateLock(m, [&] {
            ctx.srv->spawnProjectileFor(dimension, ProjectileKind::Fireball,
                                         mobX, mobY+1.5, mobZ, vx, vy, vz,
                                         entityId, false);
        })) return BTStatus::Failure;
    m.witherSkullCooldown = (int)(now + 80 + nextRandom()%40);
    return BTStatus::Success;
}

BTStatus PhantomSwoopAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Phantom) return BTStatus::Failure;
    AiPlayerSnapshot t;
    if (!snapshotPlayer(m, ctx, ctx.nearestPlayer, t))
        return BTStatus::Failure;
    double dx = t.x - m.x, dy = t.y - m.y, dz = t.z - m.z;
    double d = std::sqrt(dx*dx+dz*dz);
    if (d>30) {
        m.x += dx/d * 0.25; m.z += dz/d * 0.25;
        m.y += dy*0.05;
        return BTStatus::Running;
    }
    if (d<2.0 && now % 20==0 && ctx.srv) {
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->mobAttackPlayer(m, *t.player);
            })) return BTStatus::Failure;
    }
    else {
        m.x += dx/d * 0.18; m.z += dz/d * 0.18; m.y += (t.y+3 - m.y)*0.08;
    }
    return BTStatus::Running;
}

BTStatus ShulkerBulletAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Shulker) return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    AiPlayerSnapshot t;
    if (!snapshotPlayer(m, ctx, ctx.nearestPlayer, t))
        return BTStatus::Failure;
    const auto dimension = canonicalDimension(m.dimension);
    const auto entityId = m.entityId;
    const double mobX = m.x, mobY = m.y, mobZ = m.z;
    double d = std::sqrt((t.x-mobX)*(t.x-mobX)+(t.z-mobZ)*(t.z-mobZ));
    if (d>16) return BTStatus::Failure;
    double dx=t.x-mobX, dy=(t.y+0.5)-mobY, dz=t.z-mobZ;
    double inv=1.0/(d+1e-6);
    if (ctx.srv) {
        std::shared_ptr<ProjectileEntity> bullet;
        if (!withoutMobStateLock(m, [&] {
                bullet = ctx.srv->spawnProjectileFor(
                    dimension, ProjectileKind::ShulkerBullet, mobX, mobY + 0.5,
                    mobZ, dx * inv * 0.7, dy * inv * 0.7 + 0.1,
                    dz * inv * 0.7, entityId, false);
            })) return BTStatus::Failure;
        if (bullet) {
            bullet->targetId = t.entityId;
            bullet->targetIsPlayer = true;
        }
    }
    m.witherSkullCooldown = (int)(now + 60 + nextRandom()%40);
    if (ctx.srv) {
        withoutMobStateLock(m, [&] {
            ctx.srv->broadcastSoundFor(
                dimension, "minecraft:entity.shulker.shoot",
                mobX, mobY, mobZ, 1.f, 1.f, "hostile");
        });
    }
    return BTStatus::Success;
}

BTStatus WardenSonicBoomAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Warden) return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    AiPlayerSnapshot t;
    if (!snapshotPlayer(m, ctx, ctx.nearestPlayer, t))
        return BTStatus::Failure;
    auto isInSonicBoomRange = [](double wx, double wy, double wz, double tx, double ty, double tz) -> bool {
        double dx = tx - wx, dz = tz - wz, dy = ty - wy;
        double horiz2 = dx*dx + dz*dz;
        if (horiz2 > 15*15) return false;
        if (std::abs(dy) > 20) return false;
        return true; // cylinder 15×20 inclusive (plan22 §10: horiz hypot <=15 && vert abs <=20)
    };
    if (!isInSonicBoomRange(m.x, m.y+1.0, m.z, t.x, t.y+0.9, t.z)) {
        double dx=t.x-m.x, dz=t.z-m.z;
        double d = std::sqrt(dx*dx+dz*dz);
        if (d>1e-6) {
            m.x += dx/d*0.06; m.z += dz/d*0.06;
            m.yaw=(float)(std::atan2(dz,dx)*180/3.14159-90);
        }
        return BTStatus::Running;
    }
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double mobX = m.x, mobY = m.y, mobZ = m.z;
        // strict audit HIGH: sonic boom bypasses armor+enchant (bypassArmor/bypassEnchant=true) 15×20 cylinder, 10 damage (15 hard), no knockback, pierces shields
        float dmg = 10.0f;
        if (ctx.srv->difficulty() == "hard") dmg = 15.0f;
        // vanilla sonic boom has no knockback; do not send EntityVelocity
        const WriteBuffer particle = makeWorldParticlesBody(
            mobX, mobY + 1.6, mobZ, 0, 0, 0, 0.1f, 1,
            ParticleId::sonic_boom, {}, true, false);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->applyDamage(*t.player, dmg, DamageSource::sonicBoom());
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.warden.sonic_boom",
                    mobX, mobY, mobZ, 2.f, 1.f, "hostile");
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::WorldParticles, particle);
            })) return BTStatus::Failure;
    }
    m.witherSkullCooldown = (int)(now + 80);
    return BTStatus::Success;
}

BTStatus GenericRangedAttackAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    AiPlayerSnapshot t;
    if (!snapshotPlayer(m, ctx, ctx.nearestPlayer, t))
        return BTStatus::Failure;
    if (m.witherSkullCooldown > now) return BTStatus::Failure;
    const auto dimension = canonicalDimension(m.dimension);
    const auto entityId = m.entityId;
    const double mobX = m.x, mobY = m.y, mobZ = m.z;
    double dx=t.x-mobX, dy=(t.y+1.0)-(mobY+1.6), dz=t.z-mobZ;
    double d=std::sqrt(dx*dx+dz*dz);
    if (d<4 || d>16) return BTStatus::Failure;
    double inv=1.0/(d+1e-6);
    if (ctx.srv && !withoutMobStateLock(m, [&] {
            ctx.srv->spawnProjectileFor(dimension, ProjectileKind::Arrow,
                                         mobX, mobY+1.6, mobZ,
                                         dx*inv*1.2, dy*inv+0.15,
                                         dz*inv*1.2, entityId, false);
        })) return BTStatus::Failure;
    m.witherSkullCooldown=(int)(now+40+nextRandom()%30);
    return BTStatus::Success;
}
BTStatus WitchPotionAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Witch) return BTStatus::Failure;
    if(now < m.witchPotionCooldown) return BTStatus::Failure;
    AiPlayerSnapshot t;
    if(!snapshotPlayer(m, ctx, ctx.nearestPlayer, t)) return BTStatus::Failure;
    const auto dimension = canonicalDimension(m.dimension);
    const auto entityId = m.entityId;
    const double mobX = m.x, mobY = m.y, mobZ = m.z;
    double d2=(t.x-mobX)*(t.x-mobX)+(t.z-mobZ)*(t.z-mobZ); if(d2>256) return BTStatus::Failure;
    if(ctx.srv){
        double d=std::sqrt(d2)+1e-6;
        double vx=(t.x-mobX)/d*0.9, vz=(t.z-mobZ)/d*0.9;
        WriteBuffer md;
        md.varint(entityId);
        meta::writeMetaBool(md, 16, false); // witch is not drinking
        md.u8(255);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->spawnProjectileFor(dimension, ProjectileKind::Potion,
                                             mobX, mobY+1.6, mobZ, vx, 0.12,
                                             vz, entityId, false);
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.witch.throw",
                    mobX,mobY,mobZ, 1.f,1.f,"hostile");
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::SetEntityMetadata, md);
            })) return BTStatus::Failure;
    }
    m.witchPotionCooldown = now + 40 + nextRandom()%20; return BTStatus::Success;
}
BTStatus RavagerRoarAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Ravager) return BTStatus::Failure;
    if(now < m.ravagerRoarCooldown) return BTStatus::Failure;
    AiPlayerSnapshot t;
    if(!snapshotPlayer(m, ctx, ctx.nearestPlayer, t)) return BTStatus::Failure;
    const auto dimension = canonicalDimension(m.dimension);
    const double dx=t.x - m.x, dz=t.z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6; if(d>5) return BTStatus::Failure;
    if(ctx.srv){
        double vx=dx/d*0.4, vz=dz/d*0.4;
        WriteBuffer vel; vel.varint(t.entityId); vel.i16((int16_t)(vx*8000)); vel.i16((int16_t)(0.3*8000)); vel.i16((int16_t)(vz*8000));
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::EntityVelocity, vel);
                ctx.srv->broadcastHurtAnimationFor(dimension, t.entityId, 0);
            })) return BTStatus::Failure;
    }
    m.ravagerRoarCooldown = now + 100; return BTStatus::Success;
}
BTStatus IronGolemDefendAction::tick(MobEntity& m, AiContext& ctx, std::int64_t) {
    if (m.kind != MobKind::IronGolem) return BTStatus::Failure;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.iron_golem.step",
                    x, y, z, 0.5f, 1.f, "neutral");
            })) return BTStatus::Failure;
    }
    return BTStatus::Success;
}

BTStatus BeePollinateAction::tick(MobEntity& m, AiContext& ctx,
                                  std::int64_t now) {
    if (m.kind != MobKind::Bee) return BTStatus::Failure;
    if (m.beeHasNectar) return BTStatus::Failure;
    if (now % 20 != 0) return BTStatus::Running;
    m.beeHasNectar = true;
    m.beePollenUntil = now + 400;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        WriteBuffer md;
        md.varint(entityId);
        meta::writeMetaBool(md, 17, true);
        md.u8(255);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr, proto::pl::sc::SetEntityMetadata, md);
            })) return BTStatus::Failure;
    }
    return BTStatus::Success;
}

BTStatus VillagerScheduleAction::tick(MobEntity& m, AiContext& ctx,
                                      std::int64_t) {
    if (m.kind != MobKind::Villager && m.kind != MobKind::WanderingTrader)
        return BTStatus::Failure;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.villager.work",
                    x, y, z, 0.3f, 1.f, "neutral");
            })) return BTStatus::Failure;
    }
    return BTStatus::Success;
}
BTStatus WolfAngerAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Wolf) return BTStatus::Failure;
    if(m.isTamed) return BTStatus::Failure;
    AiPlayerSnapshot t;
    if(!snapshotPlayer(m, ctx, ctx.nearestPlayer, t)) return BTStatus::Failure;
    m.wolfAngerTarget=t.entityId; m.wolfAngerUntil=now+100;
    if(ctx.srv){
        const auto dimension = canonicalDimension(m.dimension);
        const auto entityId = m.entityId;
        WriteBuffer md; md.varint(entityId); meta::writeMetaByte(md,16,1); md.u8(255);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastPacketExceptInDimension(
                    dimension, nullptr,
                    proto::pl::sc::SetEntityMetadata, md);
            })) return BTStatus::Failure;
    }
    return BTStatus::Success;
}
BTStatus DrownedTridentAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Drowned) return BTStatus::Failure;
    if(now < m.drownedTridentCooldown) return BTStatus::Failure;
    AiPlayerSnapshot t;
    if(!snapshotPlayer(m, ctx, ctx.nearestPlayer, t)) return BTStatus::Failure;
    const auto dimension = canonicalDimension(m.dimension);
    const auto entityId = m.entityId;
    const double mobX=m.x, mobY=m.y, mobZ=m.z;
    double dx=t.x-mobX, dz=t.z-mobZ, dy=(t.y+1)-(mobY+1.6); double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d>16||d<5) return BTStatus::Failure;
    if(ctx.srv && !withoutMobStateLock(m, [&] {
            ctx.srv->spawnProjectileFor(
                dimension, ProjectileKind::Trident, mobX, mobY+1.6,
                mobZ, dx/d*1.2, dy/d*0.2+0.15, dz/d*1.2,
                entityId, false);
        })) return BTStatus::Failure;
    m.drownedTridentCooldown=now+40; return BTStatus::Success;
}
BTStatus PiglinBarterAction::tick(MobEntity& m, AiContext& ctx,
                                  std::int64_t now) {
    if (m.kind != MobKind::Piglin) return BTStatus::Failure;
    if (now < m.piglinBarterCooldown) return BTStatus::Failure;
    m.piglinBarterCooldown = now + 100;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.piglin.admiring_item",
                    x, y, z, 1.f, 1.f, "neutral");
            })) return BTStatus::Failure;
    }
    return BTStatus::Success;
}

BTStatus CatScareAction::tick(MobEntity& m, AiContext& ctx,
                              std::int64_t now) {
    if (m.kind != MobKind::Cat) return BTStatus::Failure;
    if (now < m.catScareCooldown) return BTStatus::Failure;
    m.catScareCooldown = now + 60;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.cat.purr",
                    x, y, z, 0.5f, 1.f, "neutral");
            })) return BTStatus::Failure;
    }
    return BTStatus::Success;
}
BTStatus FoxPounceAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Fox) return BTStatus::Failure;
    if(now < m.foxPounceCooldown) return BTStatus::Failure;
    AiPlayerSnapshot t;
    if(!snapshotPlayer(m, ctx, ctx.nearestPlayer, t)) return BTStatus::Failure;
    double dx=t.x-m.x, dz=t.z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d<2||d>6) return BTStatus::Failure;
    m.x+=dx/d*0.42; m.z+=dz/d*0.42; m.y+=0.38; m.foxPounceCooldown=now+40;
    return BTStatus::Success;
}
BTStatus DolphinPlayAction::tick(MobEntity& m, AiContext& ctx,
                                 std::int64_t now) {
    if (m.kind != MobKind::Dolphin) return BTStatus::Failure;
    if (now < m.dolphinPlayCooldown) return BTStatus::Failure;
    m.dolphinPlayCooldown = now + 20;
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.dolphin.play",
                    x, y, z, 1.f, 1.f, "neutral");
            })) return BTStatus::Failure;
    }
    return BTStatus::Success;
}
BTStatus EvokerFangAction::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Evoker) return BTStatus::Failure;
    if(now < m.evokerFangCooldown) return BTStatus::Failure;
    AiPlayerSnapshot t;
    if(!snapshotPlayer(m, ctx, ctx.nearestPlayer, t)) return BTStatus::Failure;
    const auto dimension = canonicalDimension(m.dimension);
    const double mobX=m.x, mobY=m.y, mobZ=m.z;
    if(ctx.srv && !withoutMobStateLock(m, [&] {
            ctx.srv->broadcastSoundFor(
                dimension, "minecraft:entity.evoker.cast_spell",
                mobX,mobY,mobZ,1.f,1.f,"hostile");
            ctx.srv->applyDamage(*t.player,6.f, DamageSource::magic());
        })) return BTStatus::Failure;
    m.evokerFangCooldown=now+60;
    return BTStatus::Success;
}

} // namespace cppfm
