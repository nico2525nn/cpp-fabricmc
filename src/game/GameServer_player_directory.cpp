#include "GameServer.hpp"
#include "Constants.hpp"
#include "PlayerEntityState.hpp"
#include "PlayerTrackingRules.hpp"
#include "../jvm/JvmRuntime.hpp"
#include "../generated/EntityIds.hpp"

#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cppfm {

namespace {

struct PlayerTrackingSnapshot {
    std::shared_ptr<Connection> connection;
    std::array<std::uint8_t, 16> uuid{};
    std::int32_t entityId = 0;
    std::int8_t dimension = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    int viewDistance = 0;
    bool inPlay = false;
    PlayerEntityMetadataState metadata;
    std::vector<EffectInstance> effects;
};

PlayerTrackingSnapshot snapshotForTracking(const Player& player) {
    std::lock_guard playerLock(player.stateMtx);
    PlayerTrackingSnapshot snapshot;
    snapshot.connection = player.conn;
    snapshot.uuid = player.uuid;
    snapshot.entityId = player.entityId;
    snapshot.dimension = GameServer::canonicalDimension(player.dimension);
    snapshot.x = player.x;
    snapshot.y = player.y;
    snapshot.z = player.z;
    snapshot.yaw = player.yaw;
    snapshot.pitch = player.pitch;
    snapshot.viewDistance = player.clientSettings.viewDistance;
    snapshot.inPlay = player.inPlay;
    snapshot.metadata = playerEntityMetadataState(player);
    snapshot.effects = player.effects;
    return snapshot;
}

#ifdef CPPFM_WORLD_MUTATION_TEST
std::function<void()>& playerPublicationPostCommitHookForTesting() {
    static thread_local std::function<void()> hook;
    return hook;
}

void runPlayerPublicationPostCommitHookForTesting() {
    auto hook = std::move(playerPublicationPostCommitHookForTesting());
    playerPublicationPostCommitHookForTesting() = {};
    if (hook) hook();
}
#endif

WriteBuffer makePlayerSpawnPacket(const PlayerTrackingSnapshot& player) {
    WriteBuffer packet;
    packet.varint(player.entityId);
    packet.uuid(player.uuid.data());
    packet.varint(static_cast<std::int32_t>(gen::kPlayerEntityTypeId));
    packet.f64(player.x);
    packet.f64(player.y);
    packet.f64(player.z);
    packet.i8(constants::protocolSignedAngleByte(player.pitch));
    packet.i8(constants::protocolSignedAngleByte(player.yaw));
    packet.i8(constants::protocolSignedAngleByte(player.yaw));
    packet.varint(0);
    packet.i16(0);
    packet.i16(0);
    packet.i16(0);
    return packet;
}

void sendPlayerSpawnToViewer(GameServer& server, Player& subject,
                             const PlayerTrackingSnapshot& subjectSnapshot,
                             Player& viewer,
                             const PlayerTrackingSnapshot& viewerSnapshot) {
    if (!viewerSnapshot.connection) {
        viewer.entityTracking.forgetEntity(subjectSnapshot.entityId);
        return;
    }

    // The subject's chunk frame is low-priority. A barrier ensures that chunk
    // data is on the wire before the entity is introduced to this viewer.
    const auto spawn = makePlayerSpawnPacket(subjectSnapshot);
    if (!viewerSnapshot.connection->trySendPacketBarrier(
            proto::pl::sc::SpawnEntity, spawn)) {
        viewer.entityTracking.forgetEntity(subjectSnapshot.entityId);
        viewerSnapshot.connection->close();
        return;
    }
    const auto metadata = makePlayerEntityMetadataPacketBody(
        subjectSnapshot.entityId, subjectSnapshot.metadata);
    if (!viewerSnapshot.connection->trySendPacket(
            proto::pl::sc::SetEntityMetadata, metadata)) {
        viewerSnapshot.connection->close();
        return;
    }
    for (const auto& effect : subjectSnapshot.effects) {
        if (isInstantEffectType(effect.type)) continue;
        const auto packet = makePlayerEntityEffectPacketBody(
            subjectSnapshot.entityId, effect);
        if (!viewerSnapshot.connection->trySendPacket(
                proto::pl::sc::EntityEffect, packet)) {
            viewerSnapshot.connection->close();
            return;
        }
    }
    server.sendPlayerEquipmentTo(subject, viewer);
}

void reconcilePlayerPair(GameServer& server, Player& viewer,
                         Player& subject) {
    if (&viewer == &subject) return;
    const auto viewerSnapshot = snapshotForTracking(viewer);
    const auto subjectSnapshot = snapshotForTracking(subject);
    const auto chunkX = playerTrackingChunkCoordinate(subjectSnapshot.x);
    const auto chunkZ = playerTrackingChunkCoordinate(subjectSnapshot.z);
    const bool subjectChunkSent = chunkX && chunkZ &&
        viewer.entityTracking.hasChunk(subjectSnapshot.dimension,
                                       *chunkX, *chunkZ);
    const int rangeBlocks = effectivePlayerTrackingRangeBlocks(
        server.config().viewDistance, viewerSnapshot.viewDistance,
        server.config().entityBroadcastRangePercentage);
    const PlayerTrackingCandidate candidate{
        .isSelf = &viewer == &subject,
        .viewerInPlay = viewerSnapshot.inPlay &&
                        static_cast<bool>(viewerSnapshot.connection),
        .subjectInPlay = subjectSnapshot.inPlay &&
                         static_cast<bool>(subjectSnapshot.connection),
        .sameDimension = viewerSnapshot.dimension ==
                         subjectSnapshot.dimension,
        .subjectChunkSent = subjectChunkSent,
        .viewerX = viewerSnapshot.x,
        .viewerZ = viewerSnapshot.z,
        .subjectX = subjectSnapshot.x,
        .subjectZ = subjectSnapshot.z,
        .rangeBlocks = rangeBlocks,
    };
    const auto transition = viewer.entityTracking.reconcileEntity(
        subjectSnapshot.entityId,
        shouldTrackPlayerEntity(candidate) && chunkX && chunkZ,
        subjectSnapshot.dimension,
        chunkX.value_or(0), chunkZ.value_or(0));

    if (transition == ClientEntityTracking::EntityTransition::Spawned) {
        sendPlayerSpawnToViewer(server, subject, subjectSnapshot,
                                viewer, viewerSnapshot);
    } else if (transition ==
               ClientEntityTracking::EntityTransition::Despawned) {
        if (!viewerSnapshot.connection || !viewerSnapshot.inPlay) return;
        WriteBuffer remove;
        remove.varint(1);
        remove.varint(subjectSnapshot.entityId);
        if (!viewerSnapshot.connection->trySendPacket(
                proto::pl::sc::RemoveEntities, remove))
            viewerSnapshot.connection->close();
    }
}

} // namespace

void GameServer::reconcilePlayerTrackingForViewer(Player& viewer) {
    serializePlayerTrackingPublication([&] {
        reconcilePlayerTrackingForViewerUnsequenced(viewer);
    });
}

void GameServer::reconcilePlayerTrackingForViewerUnsequenced(Player& viewer) {
    for (const auto& subject : playersSnapshot()) {
        if (!subject || subject.get() == &viewer) continue;
        reconcilePlayerPair(*this, viewer, *subject);
    }
}

void GameServer::reconcilePlayerTrackingForChangedPlayer(Player& subject) {
    serializePlayerTrackingPublication([&] {
        reconcilePlayerTrackingForChangedPlayerUnsequenced(subject);
    });
}

void GameServer::reconcilePlayerTrackingForChangedPlayerUnsequenced(
    Player& subject) {
    for (const auto& viewer : playersSnapshot()) {
        if (!viewer || viewer.get() == &subject) continue;
        reconcilePlayerPair(*this, *viewer, subject);
        reconcilePlayerPair(*this, subject, *viewer);
    }
}

void GameServer::broadcastTrackedPlayerPacketUnsequenced(
    const Player& subject, const std::uint8_t packetId,
    const WriteBuffer& body, const Player* except) {
    const auto subjectSnapshot = snapshotForTracking(subject);
    if (!subjectSnapshot.inPlay) return;
    for (const auto& viewer : playersSnapshot()) {
        if (!viewer || viewer.get() == &subject || viewer.get() == except ||
            !viewer->entityTracking.tracksEntity(subjectSnapshot.entityId))
            continue;
        const auto viewerSnapshot = snapshotForTracking(*viewer);
        if (!viewerSnapshot.inPlay || !viewerSnapshot.connection ||
            viewerSnapshot.dimension != subjectSnapshot.dimension)
            continue;
        if (!viewerSnapshot.connection->trySendPacket(packetId, body))
            viewerSnapshot.connection->close();
    }
}

void GameServer::broadcastTrackedPlayerPacket(
    const Player& subject, const std::uint8_t packetId,
    const WriteBuffer& body, const Player* except) {
    serializePlayerTrackingPublication([&] {
        broadcastTrackedPlayerPacketUnsequenced(
            subject, packetId, body, except);
    });
}

void GameServer::removePlayerTrackingFor(Player& subject) {
    serializePlayerTrackingPublication([&] {
        {
            std::lock_guard playerLock(subject.stateMtx);
            subject.inPlay = false;
            subject.trackingSpawnPublished = false;
        }
        reconcilePlayerTrackingForChangedPlayerUnsequenced(subject);
        subject.entityTracking.clear();
    });
}

#ifdef CPPFM_WORLD_MUTATION_TEST
void GameServer::setPlayerPublicationPostCommitHookForTesting(
    std::function<void()> hook) {
    playerPublicationPostCommitHookForTesting() = std::move(hook);
}
#endif

bool GameServer::addPlayer(PlayerRef player,
                           const PlayerDirectory::ReservationId reservation) {
    bool added = false;
    serializePlayerTrackingPublication(
        [&](PlayerTrackingSequencer::PublicationLease& publication) {
            added = addPlayer(std::move(player), reservation, publication);
        });
    return added;
}

bool GameServer::addPlayer(
    PlayerRef player, const PlayerDirectory::ReservationId reservation,
    PlayerTrackingSequencer::PublicationLease& publication) {
    std::shared_ptr<Connection> publishedConnection;
    if (player) {
        std::lock_guard playerLock(player->stateMtx);
        publishedConnection = player->conn;
    }
    std::vector<PlayerRef> retiredVictims;
    const bool added = playerDirectory_.add(
        std::move(player), reservation,
        [this, &retiredVictims, &publication](
            const PlayerDirectory::DuplicatePlayers& victims) {
            retiredVictims = victims;
            publication.suspend([&] {
                WriteBuffer kick;
                nbt::writeTextComponent(
                    kick, "You logged in from another location");

                struct RetirementTarget {
                    std::shared_ptr<Player> player;
                    std::shared_ptr<Connection> connection;
                    std::string name;
                };
                std::vector<RetirementTarget> targets;
                targets.reserve(victims.size());
                for (const auto& victim : victims) {
                    if (!victim) continue;
                    RetirementTarget target;
                    target.player = victim;
                    {
                        std::lock_guard victimLock(victim->stateMtx);
                        target.connection = victim->conn;
                        target.name = victim->name;
                    }
                    targets.push_back(std::move(target));
                }

                // All potentially allocating snapshots are complete before the
                // first retirement side effect. Per-player failures are contained
                // so this callback cannot throw after it starts disconnecting the
                // old sessions and violate PlayerDirectory's retry contract.
                for (const auto& target : targets) {
                    if (jvmRuntime_) {
                        try {
                            jvmRuntime_->beginPlayerRetirement(*target.player);
                        } catch (...) {
                            std::fprintf(stderr,
                                "[cppfm] JVM player retirement start failed for %s\n",
                                target.name.c_str());
                        }
                    }
                    try {
                        removePlayerTrackingFor(*target.player);
                    } catch (...) {
                        std::fprintf(stderr,
                            "[cppfm] player tracking retirement failed for %s\n",
                            target.name.c_str());
                        try {
                            std::lock_guard victimLock(
                                target.player->stateMtx);
                            target.player->inPlay = false;
                        } catch (...) {
                            std::fprintf(stderr,
                                "[cppfm] player state retirement failed for %s\n",
                                target.name.c_str());
                        }
                    }
                    if (jvmRuntime_) {
                        // Dispatch Fabric's LEAVE and DISCONNECT events while the
                        // native handle is still valid. onPlayerQuit invalidates it
                        // after callbacks; the old Session's later cleanup then
                        // observes handle 0 and cannot dispatch these events twice.
                        // Its shared lifetime claim also handles a cleanup that
                        // wins the callback race before this takeover path.
                        try {
                            jvmRuntime_->onPlayerQuit(*target.player);
                        } catch (...) {
                            std::fprintf(stderr,
                                "[cppfm] JVM player quit callback failed for %s\n",
                                target.name.c_str());
                        }
                    }
                    if (target.connection) {
                        target.connection->trySendPacket(
                            proto::pl::sc::Disconnect, kick);
                        target.connection->close();
                    }
                    std::fprintf(
                        stderr,
                        "[cppfm] duplicate login %s: kicked older session\n",
                        target.name.c_str());
                }
            });
        });
    const bool incumbentsRetired = !retiredVictims.empty();
    bool postCommitCleanupFailed = false;
    const auto reportPostCommitFailure = [&](const char* operation,
                                             const char* detail) noexcept {
        postCommitCleanupFailed = true;
        std::fprintf(stderr,
            "[cppfm] committed player publication post-commit %s failed: %s\n",
            operation, detail ? detail : "unknown exception");
    };
    if (added || incumbentsRetired) {
#ifdef CPPFM_WORLD_MUTATION_TEST
        try {
            runPlayerPublicationPostCommitHookForTesting();
        } catch (const std::exception& error) {
            reportPostCommitFailure("test hook", error.what());
        } catch (...) {
            reportPostCommitFailure("test hook", nullptr);
        }
#endif
    }
    if (incumbentsRetired && jvmRuntime_) {
        for (const auto& victim : retiredVictims) {
            if (!victim) continue;
            try {
                jvmRuntime_->finishPlayerRetirement(*victim);
            } catch (const std::exception& error) {
                reportPostCommitFailure("JVM retirement finalization", error.what());
            } catch (...) {
                reportPostCommitFailure("JVM retirement finalization", nullptr);
            }
        }
    }
    // addPlayer() is called from the login publication sequencer. The old
    // roster entry has now been removed, so clean its per-viewer state before
    // publishing the replacement's spawn.
    if (added || incumbentsRetired) {
        for (const auto& victim : retiredVictims) {
            if (!victim) continue;
            try {
                reconcilePlayerTrackingForChangedPlayerUnsequenced(*victim);
            } catch (const std::exception& error) {
                reportPostCommitFailure("player tracking reconciliation", error.what());
            } catch (...) {
                reportPostCommitFailure("player tracking reconciliation", nullptr);
            }
            try {
                victim->entityTracking.clear();
            } catch (const std::exception& error) {
                reportPostCommitFailure("retired entity tracking cleanup", error.what());
            } catch (...) {
                reportPostCommitFailure("retired entity tracking cleanup", nullptr);
            }
        }
    }
    // PlayerDirectory has already committed. Never unwind past Session's
    // registered_ update: fail the candidate connection closed so its normal
    // post-registration Session cleanup removes the committed membership.
    if (postCommitCleanupFailed && added && publishedConnection)
        publishedConnection->close();
    return added;
}

void GameServer::removePlayer(Player* player) {
    auto* runtime = jvmRuntime_.get();
    const auto removal = playerDirectory_.beginRemoval(player);
    const auto finishJvmRetirement = [&] {
        if (!player || !runtime ||
            (!removal.ownsSideEffects &&
             !removal.acknowledgesPriorRetirement))
            return;
        try {
            runtime->finishPlayerRetirement(*player);
        } catch (...) {
            std::fprintf(stderr,
                "[cppfm] JVM player retirement finalization failed\n");
        }
    };
    try {
        if (removal.ownsSideEffects) {
            if (player && runtime) runtime->beginPlayerRetirement(*player);
            if (player) {
                {
                    std::lock_guard playerLock(player->stateMtx);
                    player->inPlay = false;
                }
                if (runtime) runtime->invalidatePlayer(*player);
            }
            if (player) removePlayerTrackingFor(*player);
        }
    } catch (...) {
        playerDirectory_.completeRemoval(removal);
        finishJvmRetirement();
        throw;
    }
    playerDirectory_.completeRemoval(removal);
    finishJvmRetirement();
}

} // namespace cppfm
