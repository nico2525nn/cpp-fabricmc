#include "GameServer.hpp"
#include "../jvm/JvmRuntime.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cppfm {

bool GameServer::addPlayer(PlayerRef player,
                           const PlayerDirectory::ReservationId reservation) {
    std::vector<PlayerRef> retiredVictims;
    const bool added = playerDirectory_.add(
        std::move(player), reservation,
        [this, &retiredVictims](
            const PlayerDirectory::DuplicatePlayers& victims) {
            retiredVictims = victims;
            WriteBuffer kick;
            nbt::writeTextComponent(
                kick, "You logged in from another location");
            for (const auto& victim : victims) {
                if (!victim) continue;

                if (jvmRuntime_)
                    jvmRuntime_->beginPlayerRetirement(*victim);
                std::shared_ptr<Connection> connection;
                std::string name;
                {
                    std::lock_guard victimLock(victim->stateMtx);
                    victim->inPlay = false;
                    connection = victim->conn;
                    name = victim->name;
                }
                if (connection) {
                    connection->trySendPacket(proto::pl::sc::Disconnect, kick);
                    connection->close();
                }
                std::fprintf(
                    stderr,
                    "[cppfm] duplicate login %s: kicked older session\n",
                    name.c_str());
            }
        });
    if (added && jvmRuntime_) {
        for (const auto& victim : retiredVictims)
            if (victim) jvmRuntime_->finishPlayerRetirement(*victim);
    }
    return added;
}

void GameServer::removePlayer(Player* player) {
    auto* runtime = jvmRuntime_.get();
    if (player && runtime) {
        runtime->beginPlayerRetirement(*player);
        {
            std::lock_guard playerLock(player->stateMtx);
            player->inPlay = false;
        }
        runtime->invalidatePlayer(*player);
    }
    playerDirectory_.remove(player);
    if (player && runtime) runtime->finishPlayerRetirement(*player);
}

} // namespace cppfm
