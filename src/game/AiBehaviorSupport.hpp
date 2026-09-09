#pragma once

#include "GameServer.hpp"
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace cppfm::ai_detail {

inline std::int8_t canonicalDimension(std::int8_t dimension) noexcept {
    return dimension == -1 ? static_cast<std::int8_t>(-1)
         : dimension == 1 ? static_cast<std::int8_t>(1)
                           : static_cast<std::int8_t>(0);
}

inline World* dimensionWorld(AiContext& ctx, const MobEntity& mob) {
    return ctx.srv != nullptr ? &ctx.srv->worldFor(mob.dimension) : ctx.world;
}

inline bool sameDimension(const MobEntity& mob,
                          const AiPlayerSnapshot& player) {
    return canonicalDimension(mob.dimension) ==
           canonicalDimension(player.dimension);
}

// One snapshot type is shared by the goal and data-driven tree paths.  The
// extra fields are intentionally cheap and let breeding/predator goals use the
// same identity check without taking a second entity lock.
struct MobSnapshot {
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    MobKind kind = MobKind::Pig;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool dead = false;
    bool inLove = false;
    std::int64_t breedCooldownUntil = 0;
    double health = 0.0;
    std::int32_t age = 0;
    int slimeSize = 0;
};

inline MobSnapshot snapshotMob(const MobEntity& mob) {
    std::lock_guard lock(*mob.stateMtx);
    return {mob.entityId, canonicalDimension(mob.dimension), mob.kind,
            mob.x, mob.y, mob.z, mob.dead, mob.inLove,
            mob.breedCooldownUntil, mob.health, mob.age, mob.slimeSize};
}

inline bool sameDimension(const MobEntity& mob,
                          const MobSnapshot& other) {
    return canonicalDimension(mob.dimension) == other.dimension;
}

inline bool sameMobIdentity(const MobEntity& mob,
                            const MobSnapshot& snapshot) {
    return mob.entityId == snapshot.entityId &&
           canonicalDimension(mob.dimension) == snapshot.dimension &&
           mob.kind == snapshot.kind;
}

inline bool snapshotPlayer(const MobEntity& mob, const AiContext& ctx,
                           Player* candidate, AiPlayerSnapshot& out) {
    if (!candidate) return false;
    for (const auto& view : ctx.playerViews) {
        if (view.player != candidate) continue;
        if (!sameDimension(mob, view)) return false;
        out = view;
        return true;
    }

    // A live server context must use the sensor's coherent same-tick view;
    // taking Player::stateMtx under a Mob lock would invert session lock order.
    if (ctx.srv) return false;

    out = {};
    out.player = candidate;
    {
        std::lock_guard lock(candidate->stateMtx);
        out.dimension = candidate->dimension;
        out.entityId = candidate->entityId;
        out.gamemode = candidate->gamemode;
        out.inPlay = candidate->inPlay;
        out.dead = candidate->dead;
        out.isSprinting = candidate->isSprinting;
        out.heldSlot = candidate->heldSlot;
        out.x = candidate->x;
        out.y = candidate->y;
        out.z = candidate->z;
        out.yaw = candidate->yaw;
        out.pitch = candidate->pitch;
        if (out.heldSlot >= 0 && out.heldSlot < 9) {
            const auto& held = candidate->inv[36 + out.heldSlot];
            out.heldItemId = held.itemId;
            out.heldItemEmpty = held.empty();
        }
        for (int i = 5; i <= 8 && i < static_cast<int>(candidate->inv.size()); ++i) {
            if (!candidate->inv[i].empty() &&
                candidate->inv[i].name() == "minecraft:carved_pumpkin") {
                out.hasPumpkin = true;
                break;
            }
        }
    }
    return sameDimension(mob, out);
}

template <typename Fn>
inline bool withoutMobStateLock(const MobEntity& mob, Fn&& fn) {
    return runWithoutMobStateLock(
        mob, std::function<void()>(std::forward<Fn>(fn)));
}

inline void groundSnap(AiContext& ctx, MobEntity& mob) {
    World* world = dimensionWorld(ctx, mob);
    if (!world) return;

    const std::int8_t dimension = canonicalDimension(mob.dimension);
    const double x = mob.x;
    const double z = mob.z;
    const int chunkX = static_cast<std::int32_t>(x) >> 4;
    const int chunkZ = static_cast<std::int32_t>(z) >> 4;
    withoutMobStateLock(mob, [&] {
        world->generateChunkIfMissing(chunkX, chunkZ);
    });

    int columnHeight = 4;
    withoutMobStateLock(mob, [&] {
        world->withChunk(chunkX, chunkZ, [&](const Chunk& chunk) {
            for (int relativeY = kSectionsPerChunk * 16 - 1;
                 relativeY >= 0; --relativeY) {
                if (chunk.blocks[Chunk::index(
                        relativeY >> 4, relativeY & 15,
                        static_cast<std::int32_t>(z) & 15,
                        static_cast<std::int32_t>(x) & 15)] != 0) {
                    columnHeight = relativeY + 1;
                    break;
                }
            }
        });
    });

    // World access is deliberately outside the Mob lock.  A callback may
    // have moved or changed the dimension while the query was in progress;
    // never overwrite that newer state with a stale height.
    if (canonicalDimension(mob.dimension) == dimension &&
        std::abs(mob.x - x) < 1e-9 && std::abs(mob.z - z) < 1e-9) {
        mob.y = kMinY + columnHeight + 1.0;
    }
}

} // namespace cppfm::ai_detail
