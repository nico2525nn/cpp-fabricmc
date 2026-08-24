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
#include "Persistence.hpp"
#include "../net/Rcon.hpp"

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
    std::string worldDir = "world";
    std::string levelType = "flat";          // flat | normal
    bool whitelist = false;
    RconConfig rcon;
    std::string levelTypeCli;
    std::uint64_t seed = 1378645410614731511ULL;
    int compressionThreshold = 256;   // -1 disables Set Compression entirely
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
    std::int64_t lastKeepAliveSentMs = 0;
    std::int64_t keepAliveCounter = 0;
    bool spawned = false;          // position confirmed (teleport/movement)
    bool inPlay = false;           // finished onEnterPlay: eligible for broadcasts
    double sentX = 0, sentY = 0, sentZ = 0;   // last broadcast to others
    float  sentYaw = 0, sentPitch = 0;
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
    void handleRespawnRequest();
    void sendDeclareCommands();
    void onChatCommand(ReadBuffer& in);
    void dispatchCommand(const std::string& line);
    void broadcastMovement();
    void broadcastSpawnEntity(Player* about);
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
    std::shared_ptr<Player> self_ = std::make_shared<Player>();
    bool registered_ = false;         // present in server registry
    std::int32_t teleportId_ = 1;
    bool chunksStreamed_ = false;
    std::int32_t lastCx_ = INT32_MAX, lastCz_ = INT32_MAX;
    double sentX_ = 0, sentY_ = 0, sentZ_ = 0;
    float sentYaw_ = 0, sentPitch_ = 0;
    bool hasSent_ = false;
    std::unordered_set<std::int64_t> sentChunks_;
};

class GameServer {
public:
    explicit GameServer(ServerConfig cfg)
        : cfg_(cfg),
          world_(cfg_.worldBiome,
                 cfg.levelType == "normal" ? LevelType::Normal : LevelType::Flat,
                 cfg.seed) {}
    ~GameServer() { stop(); }

    void init() {
        data_.load(cfg_.assetsDir);
        whitelist_.load("whitelist.json");
        if (cfg_.whitelist) whitelist_.setEnabled(true);
        persist_ = std::make_unique<Persistence>(world_, cfg_.worldDir, cfg_.worldBiome);
        persist_->start();
        rconServer_ = std::make_unique<RconServer>(cfg_.rcon,
            [this](const std::string& cmd){ return dispatchConsole(cmd); });
        const bool rconUp = rconServer_->start();
        std::fprintf(stderr, "[cppfm] RCON %s (enabled=%d port=%u)\n",
                     rconUp ? "listening" : "not started", (int)cfg_.rcon.enabled,
                     cfg_.rcon.port);
    }
    void runForever();
    void stop() {
        running_ = false;
        if (rconServer_) rconServer_->stop();
        if (persist_) persist_->stop();
        if (listenFd_ >= 0) { ::close(listenFd_); listenFd_ = -1; }
    }
    Persistence& persistence() { return *persist_; }
    Whitelist& whitelist() { return whitelist_; }
    // Console command dispatch (shared by chat /commands and RCON)
    std::string dispatchConsole(const std::string& line);

    const ServerConfig& config() const { return cfg_; }
    World& world() { return world_; }
    EmbeddedData& data() { return data_; }
    bool running() const { return running_; }

    using PlayerRef = std::shared_ptr<Player>;
    std::vector<PlayerRef> playersSnapshot() {
        std::lock_guard lk(playersMtx_);
        return players_;
    }
    std::size_t playerCount() {
        std::lock_guard lk(playersMtx_);
        return players_.size();
    }
    std::int32_t nextEntityId() { return entityIdCounter_++; }
    void addPlayer(PlayerRef p) { std::lock_guard lk(playersMtx_); players_.push_back(std::move(p)); }
    void removePlayer(const Player* p) {
        std::lock_guard lk(playersMtx_);
        std::erase_if(players_, [p](const PlayerRef& e) { return e.get() == p; });
    }

    void broadcastSystemText(const std::string& text, Player* except = nullptr) {
        WriteBuffer body;
        nbt::writeTextComponent(body, text);
        body.boolean(false);
        broadcastPacketExcept(except, proto::pl::sc::SystemChat, body);
    }
    void broadcastPacketExcept(const Player* except, std::uint8_t id, const WriteBuffer& body) {
        for (auto& p : playersSnapshot()) {
            if (p.get() == except || !p->inPlay) continue;
            try { p->conn->sendPacket(id, body); } catch (...) {}
        }
    }
    void broadcastBlockChange(std::int32_t x, std::int32_t y, std::int32_t z,
                              std::uint16_t state) {
        WriteBuffer b;
        b.position(x, y, z);
        b.varint(state);
        broadcastPacketExcept(nullptr, proto::pl::sc::BlockUpdate, b);
        invalidateChunkCache(x >> 4, z >> 4);
    }
    // Serialized-chunk cache: shared across players; keyed by chunk, invalidated
    // by world revision on edits.
    using ChunkBodyRef = std::shared_ptr<const std::vector<std::uint8_t>>;
    bool getCachedChunk(std::int32_t cx, std::int32_t cz, std::uint32_t biomeIdx,
                        ChunkBodyRef& out) {
        const std::int64_t k = chunkKey(cx, cz);
        std::lock_guard lk(chunkCacheMtx_);
        auto it = chunkCache_.find(k);
        if (it == chunkCache_.end()) return false;
        out = it->second.body;
        (void)biomeIdx;
        return true;
    }
    void storeChunk(std::int32_t cx, std::int32_t cz, std::uint64_t rev, ChunkBodyRef body) {
        std::lock_guard lk(chunkCacheMtx_);
        if (chunkCache_.size() > 1024) chunkCache_.clear();   // simple bound
        chunkCache_[chunkKey(cx, cz)] = {rev, std::move(body)};
    }
    void invalidateChunkCache(std::int32_t cx, std::int32_t cz) {
        std::lock_guard lk(chunkCacheMtx_);
        chunkCache_.erase(chunkKey(cx, cz));
    }

private:
    void acceptLoop();

    ServerConfig cfg_;
    World world_;
    std::unique_ptr<Persistence> persist_;
    Whitelist whitelist_;
    std::unique_ptr<RconServer> rconServer_;
    EmbeddedData data_;
    std::vector<PlayerRef> players_;
    std::mutex playersMtx_;
    struct CachedChunk { std::uint64_t rev; ChunkBodyRef body; };
    std::unordered_map<std::int64_t, CachedChunk> chunkCache_;
    std::mutex chunkCacheMtx_;
    std::atomic<bool> running_{true};
    int listenFd_ = -1;
    std::int32_t entityIdCounter_ = 1;
};

} // namespace cppfm
