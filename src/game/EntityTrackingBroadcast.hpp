#pragma once

#include "EntityTrackingRange.hpp"
#include "Player.hpp"

#include "../core/ByteBuffer.hpp"
#include "../net/Connection.hpp"
#include "../proto/Ids.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace cppfm {

enum class EntityTrackingAction : std::uint8_t {
    None,
    Spawn,
    Update,
    Remove,
};

struct EntityTrackingDecision {
    EntityTrackingAction action = EntityTrackingAction::None;
    std::shared_ptr<Connection> connection;
};

inline bool entityChunkCoordinate(const double position,
                                 std::int32_t& chunk) noexcept {
    if (!std::isfinite(position)) return false;
    const double coordinate = std::floor(position / 16.0);
    if (coordinate < std::numeric_limits<std::int32_t>::min() ||
        coordinate > std::numeric_limits<std::int32_t>::max())
        return false;
    chunk = static_cast<std::int32_t>(coordinate);
    return true;
}

// Update one viewer's authoritative entity set before publishing its packet.
// Callers serialize publication with SimulationDispatchGuard so this change,
// the packet enqueue, and chunk unloads preserve their existing FIFO order.
inline EntityTrackingDecision reconcileEntityTracking(
    Player& viewer, const std::int32_t entityId,
    const std::int8_t entityDimension, const double entityX,
    const double entityZ, const MobKind kind,
    const int serverViewDistanceChunks,
    const int broadcastRangePercentage) {
    std::int32_t chunkX = 0;
    std::int32_t chunkZ = 0;
    const bool validChunk = entityChunkCoordinate(entityX, chunkX) &&
                            entityChunkCoordinate(entityZ, chunkZ);

    std::lock_guard playerLock(viewer.stateMtx);
    EntityTrackingDecision result;
    result.connection = viewer.conn;
    const bool wasTracking = viewer.entityTracking.tracksEntity(entityId);
    const int viewDistance = effectiveEntityTrackingViewDistanceChunks(
        serverViewDistanceChunks, viewer.clientSettings.viewDistance);
    const bool visible = validChunk && viewer.inPlay && viewer.spawned &&
        static_cast<bool>(result.connection) &&
        viewer.dimension == entityDimension &&
        viewer.entityTracking.hasChunk(entityDimension, chunkX, chunkZ) &&
        withinHorizontalEntityTrackingRange(
            entityX, entityZ, viewer.x, viewer.z,
            effectiveEntityTrackingRangeBlocks(
                kind, viewDistance, broadcastRangePercentage));

    if (wasTracking) {
        if (visible && viewer.entityTracking.updateTrackedEntityChunk(
                           entityId, entityDimension, chunkX, chunkZ)) {
            result.action = EntityTrackingAction::Update;
        } else {
            viewer.entityTracking.forgetEntity(entityId);
            result.action = EntityTrackingAction::Remove;
        }
    } else if (visible && viewer.entityTracking.tryTrackEntity(
                              entityId, entityDimension, chunkX, chunkZ)) {
        result.action = EntityTrackingAction::Spawn;
    }
    return result;
}

inline void forgetEntityTracking(Player& viewer,
                                 const std::int32_t entityId) {
    std::lock_guard playerLock(viewer.stateMtx);
    viewer.entityTracking.forgetEntity(entityId);
}

template <typename SpawnPublisher, typename UpdatePublisher>
void publishTrackedEntity(
    const std::vector<std::shared_ptr<Player>>& viewers,
    const std::int32_t entityId, const std::int8_t entityDimension,
    const double entityX, const double entityZ, const MobKind kind,
    const int serverViewDistanceChunks,
    const int broadcastRangePercentage,
    SpawnPublisher&& publishSpawn, UpdatePublisher&& publishUpdate) {
    WriteBuffer remove;
    remove.varint(1);
    remove.varint(entityId);

    for (const auto& viewer : viewers) {
        if (!viewer) continue;
        const auto decision = reconcileEntityTracking(
            *viewer, entityId, entityDimension, entityX, entityZ, kind,
            serverViewDistanceChunks, broadcastRangePercentage);
        if (decision.action == EntityTrackingAction::None ||
            !decision.connection)
            continue;

        bool queued = false;
        try {
            switch (decision.action) {
            case EntityTrackingAction::Spawn:
                queued = publishSpawn(*decision.connection, *viewer);
                break;
            case EntityTrackingAction::Update:
                queued = publishUpdate(*decision.connection, *viewer);
                break;
            case EntityTrackingAction::Remove:
                queued = decision.connection->trySendPacketLowPriority(
                    proto::pl::sc::RemoveEntities, remove);
                break;
            case EntityTrackingAction::None:
                queued = true;
                break;
            }
        } catch (...) {
            if (decision.action != EntityTrackingAction::Remove)
                forgetEntityTracking(*viewer, entityId);
            decision.connection->close();
            throw;
        }
        if (!queued) {
            if (decision.action != EntityTrackingAction::Remove)
                forgetEntityTracking(*viewer, entityId);
            decision.connection->close();
        }
    }
}

template <typename SpawnPublisher>
void publishTransientEntitySpawn(
    const std::vector<std::shared_ptr<Player>>& viewers,
    const std::int8_t entityDimension, const double entityX,
    const double entityZ, const MobKind kind,
    const int serverViewDistanceChunks,
    const int broadcastRangePercentage, SpawnPublisher&& publishSpawn) {
    std::int32_t chunkX = 0;
    std::int32_t chunkZ = 0;
    if (!entityChunkCoordinate(entityX, chunkX) ||
        !entityChunkCoordinate(entityZ, chunkZ))
        return;

    for (const auto& viewer : viewers) {
        if (!viewer) continue;
        std::shared_ptr<Connection> connection;
        bool visible = false;
        {
            std::lock_guard playerLock(viewer->stateMtx);
            connection = viewer->conn;
            const int viewDistance = effectiveEntityTrackingViewDistanceChunks(
                serverViewDistanceChunks, viewer->clientSettings.viewDistance);
            visible = viewer->inPlay && viewer->spawned && connection &&
                viewer->dimension == entityDimension &&
                viewer->entityTracking.hasChunk(
                    entityDimension, chunkX, chunkZ) &&
                withinHorizontalEntityTrackingRange(
                    entityX, entityZ, viewer->x, viewer->z,
                    effectiveEntityTrackingRangeBlocks(
                        kind, viewDistance, broadcastRangePercentage));
        }
        if (!visible || !connection) continue;
        if (!publishSpawn(*connection, *viewer)) connection->close();
    }
}

inline void removeTrackedEntity(
    const std::vector<std::shared_ptr<Player>>& viewers,
    const std::int32_t entityId) {
    WriteBuffer remove;
    remove.varint(1);
    remove.varint(entityId);
    for (const auto& viewer : viewers) {
        if (!viewer) continue;
        std::shared_ptr<Connection> connection;
        bool wasTracking = false;
        {
            std::lock_guard playerLock(viewer->stateMtx);
            wasTracking = viewer->entityTracking.forgetEntity(entityId);
            connection = viewer->conn;
        }
        if (wasTracking && connection &&
            !connection->trySendPacketLowPriority(
                proto::pl::sc::RemoveEntities, remove))
            connection->close();
    }
}

inline void sendPacketToTrackedEntityViewers(
    const std::vector<std::shared_ptr<Player>>& viewers,
    const std::int32_t entityId, const std::uint8_t packetId,
    const WriteBuffer& body) {
    for (const auto& viewer : viewers) {
        if (!viewer) continue;
        std::shared_ptr<Connection> connection;
        bool tracksEntity = false;
        {
            std::lock_guard playerLock(viewer->stateMtx);
            tracksEntity = viewer->entityTracking.tracksEntity(entityId);
            connection = viewer->conn;
        }
        if (tracksEntity && connection &&
            !connection->trySendPacketLowPriority(packetId, body))
            connection->close();
    }
}

inline MobKind projectileTrackingKind(const ProjectileKind kind) noexcept {
    switch (kind) {
    case ProjectileKind::Arrow: return MobKind::Arrow;
    case ProjectileKind::Snowball: return MobKind::Snowball;
    case ProjectileKind::Egg: return MobKind::Egg;
    case ProjectileKind::EnderPearl: return MobKind::EnderPearl;
    case ProjectileKind::Potion: return MobKind::Potion;
    case ProjectileKind::WitherSkull: return MobKind::WitherSkull;
    case ProjectileKind::Fireball: return MobKind::Fireball;
    case ProjectileKind::DragonFireball: return MobKind::DragonFireball;
    case ProjectileKind::Trident: return MobKind::Trident;
    case ProjectileKind::WindCharge: return MobKind::WindCharge;
    case ProjectileKind::BreezeWindCharge: return MobKind::BreezeWindCharge;
    case ProjectileKind::LlamaSpit: return MobKind::LlamaSpit;
    case ProjectileKind::ShulkerBullet: return MobKind::ShulkerBullet;
    }
    return MobKind::Marker;
}

} // namespace cppfm
