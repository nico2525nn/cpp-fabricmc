#include "GameServer.hpp"
#include "Session.hpp"
#include "Messages.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppfm {
using namespace proto;

bool Session::restorePlayerDataForLogin(const std::string& uuidHex) {
    // Keep player-specific persisted state loading separate from admission:
    // callers must already hold the identity handoff and admission lease.
    bool unsupportedItemData = false;
    bool unsafeExistingPlayerData = false;
    {
        std::lock_guard playerLock(self_->stateMtx);
        self_->gamemode = srv_.defaultGameMode();
    }
    (void)srv_.loadPlayerData(uuidHex, *self_, nullptr, &unsupportedItemData,
                              &unsafeExistingPlayerData);
    if (unsupportedItemData || unsafeExistingPlayerData) {
        preservePlayerDataOnDisconnect_ = true;
        WriteBuffer kick;
        nbt::writeTextComponent(
            kick,
            unsupportedItemData
                ? "Saved player data contains an item this server cannot load; install compatible mods and retry. Player data was preserved."
                : "Saved player data could not be safely loaded or isolated. The original data was preserved; check server file access and retry.");
        conn_->trySendPacket(pl::sc::Disconnect, kick);
        state_ = State::Done;
        return false;
    }

    if (srv_.config().forceGameMode) {
        std::lock_guard playerLock(self_->stateMtx);
        if (self_->gamemode != srv_.defaultGameMode()) {
            self_->previousGamemode = self_->gamemode;
            self_->gamemode = srv_.defaultGameMode();
        }
    }

    bool inventoryCanBeSent = true;
    {
        std::lock_guard playerLock(self_->stateMtx);
        for (const auto& stack : self_->inv) {
            if (!stack.canWriteToProtocol()) {
                inventoryCanBeSent = false;
                break;
            }
        }
        if (inventoryCanBeSent) {
            for (const auto& stack : self_->enderItems) {
                if (!stack.canWriteToProtocol()) {
                    inventoryCanBeSent = false;
                    break;
                }
            }
        }
    }
    if (!inventoryCanBeSent) {
        WriteBuffer kick;
        nbt::writeTextComponent(
            kick,
            "Saved player inventory data contains items this server cannot encode; player data was preserved.");
        conn_->trySendPacket(pl::sc::Disconnect, kick);
        state_ = State::Done;
        return false;
    }

    self_->dimension = GameServer::canonicalDimension(self_->dimension);
    return true;
}

void Session::sendInitialPlayerStateForLogin() {
    // Join Game must follow the durable dimension/position restore so its
    // initial world and first chunk view agree with the player's saved state.
    sendJoinGame();
    {
        // Join Game establishes the world, but clients also need the current
        // clock immediately; waiting for the next periodic sync leaves a new
        // session with stale/no day and time state during its first second.
        WriteBuffer time;
        time.i64(srv_.tickNow());
        time.i64(srv_.dayTime());
        time.boolean(!srv_.gameRules().contains("doDaylightCycle") ||
                     srv_.gameRules().getBool("doDaylightCycle"));
        conn_->sendPacket(pl::sc::UpdateTime, time);
    }
    srv_.sendServerData(*self_);
    srv_.sendWorldBorderTo(*self_);

    { // Brand is also sent during Configuration; vanilla repeats it in Play.
        WriteBuffer packet;
        packet.string("minecraft:brand");
        WriteBuffer payload;
        payload.string("CppFabricMC");
        packet.raw(payload.data.data(), payload.data.size());
        conn_->sendPacket(pl::sc::CustomPayload, packet);
    }
    {
        WriteBuffer packet;
        packet.i8(0);
        conn_->sendPacket(pl::sc::SetHeldSlot, packet);
    }
    {
        const auto spawn = srv_.worldFor(self_->dimension).spawnPoint();
        WriteBuffer packet;
        packet.position(spawn.x, spawn.y, spawn.z);
        packet.f32(spawn.angle);
        conn_->sendPacket(pl::sc::SetDefaultSpawn, packet);
    }
    sendTeleport(self_->x, self_->y, self_->z, self_->yaw, self_->pitch);
}

void Session::publishPlayerForLogin() {
    srv_.serializePlayerTrackingPublication(
        [&](PlayerTrackingSequencer::PublicationLease& publication) {
            {
                std::lock_guard playerLock(self_->stateMtx);
                self_->cleanupCompletion = cleanupCompletionFuture_;
            }
            if (!playerAdmissionReservation_ ||
                !srv_.addPlayer(self_, *playerAdmissionReservation_,
                                publication)) {
                throw std::logic_error(
                    "player admission reservation expired before roster publication");
            }
            playerAdmissionReservation_.reset();
            registered_ = true;
            {
                std::lock_guard playerLock(self_->stateMtx);
                self_->inPlay = true;
                // restorePlayerDataForLogin() has already loaded the durable
                // survival state. Admission must not replace it with fresh-player
                // defaults when publishing the session.
                self_->exhaustion = 0;
                self_->fallDist = 0;
                self_->dead = false;
                if (self_->enchantmentSeed == 0) {
                    self_->enchantmentSeed = static_cast<std::int32_t>(
                        self_->entityId * 0x9e3779b9u ^ srv_.config().hashedSeed ^
                        0x27d4eb2du);
                    if (self_->enchantmentSeed == 0)
                        self_->enchantmentSeed = 0x5a5a5a5a;
                }
            }

            // Keep activation and PlayerInfo publication in one ordered
            // section. The duplicate-retirement callback temporarily suspends
            // this lease, but directory commit and all publication resume here.
            sendPlayerInfoAddSelf();
            broadcastPlayerInfoAdd(self_.get());
            for (const auto& other : srv_.playersSnapshot()) {
                if (!other || other.get() == self_.get()) continue;
                std::array<std::uint8_t, 16> uuid{};
                std::string name;
                bool inPlay = false;
                {
                    std::lock_guard playerLock(other->stateMtx);
                    inPlay = other->inPlay;
                    if (inPlay) {
                        uuid = other->uuid;
                        name = other->name;
                    }
                }
                if (!inPlay) continue;
                WriteBuffer add;
                add.u8(0x01 | 0x08); // add_player | update_listed
                add.varint(1);
                add.uuid(uuid.data());
                add.string(name);
                add.varint(0); // properties
                add.varint(1); // listed
                if (!conn_->trySendPacket(pl::sc::PlayerInfoUpdate, add)) {
                    conn_->close();
                    break;
                }
            }
        });

    // This takes the simulation gate; keep it outside the publication
    // sequencer because portal/respawn acquire these locks in reverse order.
    srv_.registerSession(this);
}

void Session::completePlayerLogin(SimulationDispatchGuard& simulationLock) {
    sendAbilities();
    self_->prevFeetY = self_->y;
    // Initialize durable progress before any join hook can disconnect the
    // player. Session cleanup must be able to save the admitted identity even
    // when a Fabric/native callback rejects the just-published player.
    srv_.initPlayerProgress(*self_);

    api::PlayerJoinEvent event;
    event.player = self_.get();
    srv_.events().join.fire(event);
    if (srv_.jvmRuntime()) {
        auto* runtime = srv_.jvmRuntime();
        simulationLock.withLockReleased([&] { runtime->onPlayerJoin(*self_); });
    }

    bool stillAdmitted = false;
    {
        std::lock_guard playerLock(self_->stateMtx);
        stillAdmitted = self_->inPlay && self_->conn == conn_;
    }
    if (!stillAdmitted || !conn_->isOpen()) {
        conn_->close();
        state_ = State::Done;
        return;
    }

    // Keep the root advancement grant after callbacks: this preserves the
    // existing successful-login packet order while progress managers are
    // already available to join hooks and disconnect cleanup.
    srv_.grantAdvancement(*self_, "cppfm:root");
    srv_.sendAdvancementsTo(*self_, true);
    broadcastSpawnEntity(self_.get());
    sendDeclareCommands();
    sendRecipeBook();
    srv_.sendSetExperience(*self_);
    sendInitialInventory();

    srv_.sendSetHealth(*self_);

    srv_.broadcastSystemText(msg::kYellow + self_->name + " joined the game",
                             nullptr);
    {
        std::vector<std::string> players;
        for (const auto& player : srv_.playersSnapshot())
            players.push_back(player->name);
        srv_.sendChatSuggestions(*self_, 2, players);
        const std::vector<std::string> joinedPlayer{self_->name};
        srv_.broadcastChatSuggestions(0, joinedPlayer, self_.get());
    }
    sendSystemText(msg::kGray + "Welcome to " + msg::kAqua + "CppFabricMC" +
                   msg::kGray + "! Build with the hotbar, chat freely.");
    if (srv_.bossAI()) srv_.bossAI()->onPlayerJoin(*self_);
}

} // namespace cppfm
