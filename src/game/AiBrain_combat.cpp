// AI goal implementations grouped by behavior domain.

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

using namespace ai_detail;
using ai_brain_detail::dimensionPlayer;
using ai_brain_detail::nearestPlayerView;

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
    const auto geometry = world->verticalGeometry();
    int col=4;
    withoutMobStateLock(m, [&] {
        world->withChunk(chunkX, chunkZ,[&](const Chunk& c){
            for (int y = geometry.maxY() - 1; y >= geometry.minY; --y) {
                const int storageY = y - kMinY;
                if(c.blockAt(Chunk::index(storageY >> 4, storageY & 15,
                                          blockZ&15, blockX&15))!=0){
                    col = y - geometry.minY + 1;
                    break;
                }
            }
        });
    });
    m.y = geometry.minY + col + 1.0;
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
    if (World* world = dimensionWorld(ctx, m))
        m.y = std::min(m.y, static_cast<double>(world->maxY() - 1));
    m.yaw = static_cast<float>(std::atan2(dz,dx)*180/3.14159 -90);
    if (ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double x = m.x, y = m.y, z = m.z;
        const auto entityId = m.entityId;
        WriteBuffer vel; vel.varint(entityId); vel.i16((int16_t)(vx*8000)); vel.i16((int16_t)(vy*8000)); vel.i16((int16_t)(vz*8000));
        withoutMobStateLock(m, [&] {
            ctx.srv->broadcastMobPositionUpdate(
                m, proto::pl::sc::EntityVelocity, vel);
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
            ctx.srv->broadcastMobPositionUpdate(
                m, proto::pl::sc::EntityVelocity, vel);
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


bool WitchPotionThrowGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Witch) return false;
    const MobBehaviorSpec* spec = mobBehaviorSpec(m.kind);
    if (!spec) return false;
    if (!dimensionPlayer(ctx, m, ctx.nearestPlayer)) return false;
    if (!spec->withinActionRangeSquared(ctx.nearestPlayerDist2)) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.witchPotionCooldown) return false;
    return true;
}


bool WitchPotionThrowGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Witch) return false;
    const MobBehaviorSpec* wspec = mobBehaviorSpec(m.kind);
    if (!wspec) return false;
    auto t = dimensionPlayer(ctx, m, ctx.nearestPlayer); if (!t) return false;
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
    if (!dimensionPlayer(ctx, m, ctx.nearestPlayer)) return false;
    if (ctx.nearestPlayerDist2 > 5*5) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.ravagerRoarCooldown) return false;
    return true;
}


bool RavagerRoarGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Ravager) return false;
    if (now < m.ravagerRoarCooldown) return false;
    auto t = dimensionPlayer(ctx, m, ctx.nearestPlayer); if (!t) return false;
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
    const auto player = dimensionPlayer(ctx, m, ctx.nearestPlayer);
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


bool WolfAngerGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Wolf) return false;
    if (ctx.lastHurtTick.load(std::memory_order_acquire) >= 0 && ctx.srv &&
        ctx.srv->tickNoForTest() -
                ctx.lastHurtTick.load(std::memory_order_acquire) < 40)
        return true;
    if (m.wolfAngerTarget!=-1 && ctx.srv && ctx.srv->tickNoForTest() < m.wolfAngerUntil) return true;
    if (dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 4) return false; // not angry by proximity alone
    return false;
}


bool WolfAngerGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Wolf) return false;
    if (m.isTamed) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
    double dx=t->x - m.x, dz=t->z - m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if (d>12) return false;
    if (d<1.9) {
        if(now%20==0 && ctx.srv &&
           !withoutMobStateLock(m, [&] {
               ctx.srv->mobAttackPlayer(m,*t->player);
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
    if (!dimensionPlayer(ctx, m, ctx.nearestPlayer)) return false;
    if (ctx.nearestPlayerDist2 > perceptionRange2(MobKind::Drowned)) return false; // plan44 G-05
    if (ctx.srv && ctx.srv->tickNoForTest() < m.drownedTridentCooldown) return false;
    return true;
}


bool DrownedTridentGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Drowned) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
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


bool EvokerFangGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Evoker) return false;
    if (!dimensionPlayer(ctx, m, ctx.nearestPlayer)) return false;
    if (ctx.nearestPlayerDist2 > 12*12) return false;
    if (ctx.srv && ctx.srv->tickNoForTest() < m.evokerFangCooldown) return false;
    return true;
}


bool EvokerFangGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Evoker) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
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
                    jvm::MobSpawnAdmission admission(
                        ctx.srv->jvmRuntime(), *fang,
                        &cancelMobSpawnOutsideMobStateLock);
                    if (!admission.approve(fang->x, fang->y, fang->z) ||
                        !ctx.srv->addMob(fang))
                        continue;
                    admission.commit();
                    ctx.srv->broadcastMobSpawn(*fang);
                }
                ctx.srv->broadcastSoundFor(
                    dimension, "minecraft:entity.evoker.cast_spell",
                    mobX,mobY,mobZ,1.f,1.f,"hostile");
                ctx.srv->applyDamage(*t->player, 6.f, DamageSource::magic());
            })) return false;
    }
    m.evokerFangCooldown = now + 60;
    return true;
}


bool VindicatorAxeGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Vindicator) return false; return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 12*12; }


bool VindicatorAxeGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Vindicator) return false;
    if(now < m.vindicatorJohnnyUntil && m.vindicatorJohnnyUntil!=0) { /* johnny cooldown */ }
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
    const auto dimension = canonicalDimension(m.dimension);
    double dx=t->x-m.x, dz=t->z-m.z; double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    if(d<1.9){
        if(now%20==0 && ctx.srv && !withoutMobStateLock(m, [&] {
                ctx.srv->mobAttackPlayer(m,*t->player);
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


bool PillagerCrossbowGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Pillager) return false; return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < perceptionRange2(MobKind::Pillager); } // plan44 G-05


bool PillagerCrossbowGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Pillager) return false;
    if(now < m.pillagerCrossbowCooldown) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
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


bool WitherSkeletonEffectGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::WitherSkeleton) return false; return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 3*3; }


bool WitherSkeletonEffectGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::WitherSkeleton) return false;
    if(now < m.witherSkeletonEffectCooldown) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
    if(ctx.srv){
        const auto dimension = canonicalDimension(m.dimension);
        const double x=m.x,y=m.y,z=m.z;
        WriteBuffer eff; eff.varint(t->entityId); eff.varint(20); eff.i8(0); eff.varint(100); eff.u8(0x01);
        if (!withoutMobStateLock(m, [&] {
                ctx.srv->mobAttackPlayer(m,*t->player);
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


bool GoatRamGoal::shouldStart(MobEntity& m, AiContext& ctx){ if(m.kind!=MobKind::Goat) return false; return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 10*10; }


bool GoatRamGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now){
    if(m.kind!=MobKind::Goat) return false;
    if(now < m.goatRamCooldown) return false;
    auto t=dimensionPlayer(ctx, m, ctx.nearestPlayer); if(!t) return false;
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
                    ctx.srv->applyDamage(*t->player, 5.f, "mob");
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


bool PiglinBruteAttackGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::PiglinBrute) return false;
    return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 24*24;
}


bool PiglinBruteAttackGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::PiglinBrute) return false;
    auto t = dimensionPlayer(ctx, m, ctx.nearestPlayer); if (!t) return false;
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
                    ctx.srv->mobAttackPlayer(m,*t->player);
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


bool LlamaSpitGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::Llama && m.kind != MobKind::TraderLlama) return false;
    if (!ctx.srv || ctx.srv->tickNoForTest() < m.llamaSpitCooldown) return false;
    return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 > 4*4 && ctx.nearestPlayerDist2 < 16*16;
}


bool LlamaSpitGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Llama && m.kind != MobKind::TraderLlama) return false;
    if (now < m.llamaSpitCooldown) return false;
    auto t = dimensionPlayer(ctx, m, ctx.nearestPlayer); if (!t) return false;
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


bool PolarBearDefendGoal::shouldStart(MobEntity& m, AiContext& ctx) {
    if (m.kind != MobKind::PolarBear) return false;
    return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 8*8;
}


bool PolarBearDefendGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::PolarBear) return false;
    auto t = dimensionPlayer(ctx, m, ctx.nearestPlayer); if (!t) return false;
    double dx=t->x-m.x, dz=t->z-m.z;
    double d=std::sqrt(dx*dx+dz*dz)+1e-6;
    m.polarBearDefendUntil = now+20; // standing/defending posture window
    if (d < 1.9) {
        if (now%20==0 && ctx.srv) {
            const auto dimension = canonicalDimension(m.dimension);
            const double x = m.x, y = m.y, z = m.z;
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->mobAttackPlayer(m,*t->player);
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
    return dimensionPlayer(ctx, m, ctx.nearestPlayer) && ctx.nearestPlayerDist2 < 4*4;
}


bool PufferfishPuffGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (m.kind != MobKind::Pufferfish) return false;
    auto t = dimensionPlayer(ctx, m, ctx.nearestPlayer);
    const bool threatened = t && ctx.nearestPlayerDist2 < 4 * 4;
    if (threatened) {
        m.pufferfishPuffUntil = now + 40;
        m.pufferfishPuffState = 2;
    } else if (now < m.pufferfishPuffUntil) {
        // Keep the existing deadline as the deflation clock: full size for
        // the first half, then the vanilla intermediate size before expiry.
        m.pufferfishPuffState =
            m.pufferfishPuffUntil - now > 20 ? 2 : 1;
        return true;
    } else {
        m.pufferfishPuffState = 0;
        return false;
    }
    if (t && ctx.srv) {
        const auto dimension = canonicalDimension(m.dimension);
        const double mobX = m.x, mobY = m.y, mobZ = m.z;
        double dx=t->x-mobX, dz=t->z-mobZ;
        if (dx*dx+dz*dz < 1.5*1.5) {
            // vanilla contact poison (effect id 19, like bogged arrow)
            WriteBuffer eff; eff.varint(t->entityId); eff.varint(19); eff.i8(0); eff.varint(120); eff.u8(0x01);
            if (!withoutMobStateLock(m, [&] {
                    ctx.srv->applyDamage(*t->player, 3.f, "mob");
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


} // namespace cppfm
