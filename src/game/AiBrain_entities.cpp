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

bool BoatDriftGoal::shouldStart(MobEntity& m, AiContext&) { return MobEntity::isBoat(m.kind); }


bool BoatDriftGoal::tick(MobEntity& m, AiContext&, std::int64_t now) {
    if (!MobEntity::isBoat(m.kind)) return false;
    // vanilla Boat: water bob + slow drift along heading
    m.y += std::sin(static_cast<double>(now)*0.15)*0.004;
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


bool ProjectileFlyGoal::shouldStart(MobEntity& m, AiContext&) { return MobEntity::isProjectileKind(m.kind); }


bool ProjectileFlyGoal::tick(MobEntity& m, AiContext&, std::int64_t) {
    if (!MobEntity::isProjectileKind(m.kind)) return false;
    // ballistic hold: projectiles keep latched velocity (set by thrower systems) instead of wandering randomly. Returning true claims the
    // tick so the generic WanderAroundGoal never steers a flying projectile.
    m.x += m.projectileVx; m.y += m.projectileVy; m.z += m.projectileVz;
    m.projectileVy -= 0.02; // mild gravity like vanilla thrown projectiles
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


bool AmbientObjectGoal::shouldStart(MobEntity& m, AiContext&) { return MobEntity::isAmbientObjectKind(m.kind); }


bool AmbientObjectGoal::tick(MobEntity& m, AiContext& ctx, std::int64_t now) {
    if (!MobEntity::isAmbientObjectKind(m.kind)) return false;
    switch (m.kind) {
    case MobKind::ExperienceOrb:
    case MobKind::Item: {
        // vanilla magnet: drift toward nearest player within 8 (XP) / 3 (item)
        auto t = dimensionPlayer(ctx, m, ctx.nearestPlayer);
        double range = (m.kind==MobKind::ExperienceOrb) ? 8.0 : 3.0;
        if (t && ctx.nearestPlayerDist2 < range*range) {
            double dx=t->x-m.x, dz=t->z-m.z;
            double d=std::sqrt(dx*dx+dz*dz)+1e-6;
            m.x += dx/d*0.12; m.z += dz/d*0.12;
        }
        m.y += std::sin(static_cast<double>(now)*0.2)*0.003; // bob
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


} // namespace cppfm
