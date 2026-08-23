// GameServer: protocol state machine (HANDSHAKE→STATUS/LOGIN→CONFIGURATION→PLAY),
// player registry, world interaction, broadcasting.
#pragma once
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>
#include "../net/Connection.hpp"
#include "../proto/Ids.hpp"
#include "World.hpp"
#include "ChunkCodec.hpp"
#include "EmbeddedData.hpp"

namespace cppfm {

struct ServerConfig {
    std::uint16_t port = 25565;
    std::int32_t maxPlayers = 20;
    std::int32_t viewDistance = 6;
    std::int32_t simulationDistance = 10;
    std::string motd = "CppFabricMC - C++ Minecraft 1.21.4 server";
    std::string worldBiome = "minecraft:plains";
    std::int64_t hashedSeed = 1378645410614731511LL;
    std::string assetsDir = "assets/registry";
};

struct Player {
    std::string name;
    std::array<std::uint8_t, 16> uuid{};
    std::int32_t entityId = 0;
    double x = 0.5, y = -60.0, z = 0.5;
    float yaw = 0, pitch = 0;
    bool onGround = true;
    std::int32_t heldSlot = 0;
    std::int64_t lastSeenMs = 0;
    std::int64_t pendingKeepAlive = 0;
    bool spawned = false;          // entered PLAY & confirmed position
    Connection* conn = nullptr;
};

class GameServer;

// Per-connection session: drives the state machine on its own thread.
class Session {
public:
    Session(GameServer& srv, std::unique_ptr<Connection> conn)
        : srv_(srv), conn_(std::move(conn)) {}

    void run();

private:
    void handleHandshake(ReadBuffer& in);
    void handleStatus();
    void handleLogin();
    void handleConfiguration();
    void handlePlay();
    void onEnterPlay();
    void tickChunksAround(double px, double pz);

    // play-phase handlers
    void onChatMessage(ReadBuffer& in);
    void onPlayerAction(ReadBuffer& in);
    void onUseItemOn(ReadBuffer& in);
    void onUseItem(ReadBuffer& in);
    void onHeldSlot(ReadBuffer& in);
    void onMovement(ReadBuffer& in, bool hasPos, bool hasRot);

    // send helpers
    void disconnectIn(const char* jsonReason);   // uses current state_
    void sendSystemText(const std::string& text);
    void sendJoinGame();
    void sendTeleport(double x, double y, double z, float yaw, float pitch);
    void sendChunk(std::int32_t cx, std::int32_t cz);
    void streamInitialChunks();
    void sendPlayerInfoAddSelf();
    void broadcastPlayerInfoAdd(Player* about);
    void sendStarterInventory();
    void sendAbilities();
    void ack(std::int32_t sequence);

    GameServer& srv_;
    std::unique_ptr<Connection> conn_;
    enum class State { Handshake, Status, Login, Configuration, Play, Done };
    State state_ = State::Handshake;
    Player player_{};                 // owned per-session copy; registry holds pointer
    Player* registered_ = nullptr;    // pointer while present in server registry
    std::int32_t teleportId_ = 1;
    bool chunksStreamed_ = false;
    std::int32_t lastCx_ = INT32_MAX, lastCz_ = INT32_MAX;
    std::unordered_set<std::int64_t> sentChunks_;
};

class GameServer {
public:
    explicit GameServer(ServerConfig cfg) : cfg_(cfg), world_(cfg.worldBiome) {}
    ~GameServer() { stop(); }

    void init() { data_.load(cfg_.assetsDir); }
    void runForever();
    void stop() {
        running_ = false;
        if (listenFd_ >= 0) { ::close(listenFd_); listenFd_ = -1; }
    }

    const ServerConfig& config() const { return cfg_; }
    World& world() { return world_; }
    EmbeddedData& data() { return data_; }
    bool running() const { return running_; }

    std::vector<Player*> playersSnapshot() {
        std::lock_guard lk(playersMtx_);
        return players_;
    }
    std::size_t playerCount() {
        std::lock_guard lk(playersMtx_);
        return players_.size();
    }
    void addPlayer(Player* p) { std::lock_guard lk(playersMtx_); players_.push_back(p); }
    void removePlayer(Player* p) {
        std::lock_guard lk(playersMtx_);
        std::erase(players_, p);
    }

    void broadcastSystemText(const std::string& text, Player* except = nullptr) {
        WriteBuffer body;
        nbt::writeTextComponent(body, text);
        body.boolean(false);
        broadcastPacketExcept(except, proto::pl::sc::SystemChat, body);
    }
    void broadcastPacketExcept(Player* except, std::uint8_t id, const WriteBuffer& body) {
        for (auto* p : playersSnapshot()) {
            if (p == except || !p->spawned) continue;
            try { p->conn->sendPacket(id, body); } catch (...) {}
        }
    }
    void broadcastBlockChange(std::int32_t x, std::int32_t y, std::int32_t z,
                              std::uint16_t state) {
        WriteBuffer b;
        b.position(x, y, z);
        b.varint(state);
        broadcastPacketExcept(nullptr, proto::pl::sc::BlockUpdate, b);
    }

private:
    void acceptLoop();

    ServerConfig cfg_;
    World world_;
    EmbeddedData data_;
    std::vector<Player*> players_;
    std::mutex playersMtx_;
    std::atomic<bool> running_{true};
    int listenFd_ = -1;
};

} // namespace cppfm
