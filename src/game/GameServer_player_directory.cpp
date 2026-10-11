#include "GameServer.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace cppfm {

bool GameServer::addPlayer(PlayerRef player,
                           const PlayerDirectory::ReservationId reservation) {
    return playerDirectory_.add(
        std::move(player), reservation,
        [this](const PlayerDirectory::DuplicatePlayers& victims) {
            WriteBuffer kick;
            nbt::writeTextComponent(
                kick, "You logged in from another location");
            for (const auto& victim : victims) {
                if (!victim) continue;

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
}

} // namespace cppfm
