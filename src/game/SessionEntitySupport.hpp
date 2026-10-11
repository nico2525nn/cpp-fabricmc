#pragma once

#include "GameServer.hpp"
#include "SessionInteractionSupport.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace cppfm::session_entity_detail {

inline bool canInteractWithNonPlayerMob(
    const std::uint8_t gamemode) noexcept {
    return gamemode != 3; // vanilla PlayerEntity.interactOn rejects spectators
}

inline bool canOpenHorseInventory(const MobEntity& mob,
                                  const bool secondaryUse) noexcept {
    if (!secondaryUse || MobEntity::isBaby(mob)) return false;
    // CamelEntity.interactMob handles secondary-use before the inherited
    // passenger/tame checks, so an adult camel permits its inventory even
    // while ridden.
    if (mob.kind == MobKind::Camel) return true;
    if (mob.riderEntityId != -1) return false;
    switch (mob.kind) {
    case MobKind::Horse:
    case MobKind::Donkey:
    case MobKind::Mule:
    case MobKind::Llama:
    case MobKind::TraderLlama:
    case MobKind::SkeletonHorse:
    case MobKind::ZombieHorse:
        return mob.isTamed;
    default:
        return false;
    }
}

struct SessionMobSnapshot {
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    MobKind kind = MobKind::Pig;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool dead = false;
    int slimeSize = 0;
    bool entityOnFire = false;
};

inline SessionMobSnapshot snapshotMobForSession(const MobEntity& mob) {
    std::lock_guard entityLock(*mob.stateMtx);
    return {mob.entityId, GameServer::canonicalDimension(mob.dimension),
            mob.kind, mob.x, mob.y, mob.z, mob.dead, mob.slimeSize,
            mob.onFireTicks > 0};
}

inline double interactionEntityHalfWidth(const MobKind kind,
                                         const int slimeSize = 2) {
    if (MobEntity::isBoat(kind) || MobEntity::isMinecartKind(kind))
        return 0.72;
    if (kind == MobKind::EnderDragon || kind == MobKind::Wither) return 1.0;
    if (kind == MobKind::Slime || kind == MobKind::MagmaCube)
        return std::max(0.26,
                        static_cast<double>(slimeWidthForSize(slimeSize)) * 0.5);
    return 0.32;
}

inline double interactionEntityHeight(const MobKind kind) {
    if (MobEntity::isBoat(kind) || MobEntity::isMinecartKind(kind)) return 0.7;
    if (kind == MobKind::EnderDragon || kind == MobKind::Wither) return 3.5;
    if (kind == MobKind::Enderman || kind == MobKind::IronGolem ||
        kind == MobKind::Ravager || kind == MobKind::Warden)
        return 2.9;
    return 1.8;
}

inline bool withinEntityInteractionRange(
    const double playerX, const double playerY, const double playerZ,
    const std::uint8_t gamemode, const double targetX, const double targetY,
    const double targetZ, const MobKind targetKind = MobKind::Pig,
    const int slimeSize = 2) {
    if (!std::isfinite(playerX) || !std::isfinite(playerY) ||
        !std::isfinite(playerZ) || !std::isfinite(targetX) ||
        !std::isfinite(targetY) || !std::isfinite(targetZ))
        return false;

    const double range = gamemode == 1 ? 5.0 : 3.0;
    const double halfWidth = interactionEntityHalfWidth(targetKind, slimeSize);
    const double height = interactionEntityHeight(targetKind);
    return session_detail::squaredDistanceToBox(
               playerX, playerY + 1.62, playerZ,
               targetX - halfWidth, targetY, targetZ - halfWidth,
               targetX + halfWidth, targetY + height, targetZ + halfWidth) <=
           range * range + 1e-6;
}

inline bool withinEntityInteractionRange(
    const session_detail::SessionPlayerSnapshot& player,
    const double targetX, const double targetY, const double targetZ,
    const MobKind targetKind = MobKind::Pig, const int slimeSize = 2) {
    if (!player.inPlay || player.dead) return false;
    return withinEntityInteractionRange(
        player.x, player.y, player.z, player.gamemode, targetX, targetY,
        targetZ, targetKind, slimeSize);
}

} // namespace cppfm::session_entity_detail
