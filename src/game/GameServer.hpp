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
#include <array>
#include "../net/Connection.hpp"
#include "../proto/Ids.hpp"
#include "World.hpp"
#include "ChunkCodec.hpp"
#include "EmbeddedData.hpp"
#include "Persistence.hpp"
#include "Entities.hpp"
#include "MineData.hpp"
#include "../net/Rcon.hpp"
#include "../net/Crypto.hpp"
#include "../net/MojangAuth.hpp"
#include "Items.hpp"
#include "Containers.hpp"
#include "Recipes.hpp"
#include "BlockEntities.hpp"
#include "GameData.hpp"
#include "Xp.hpp"
#include "MobEffects.hpp"
#include "../api/EventBus.hpp"
#include "../api/PluginChannels.hpp"
#include "../brigadier/Tree.hpp"
#include "GameRules.hpp"
#include "ServerEvents.hpp"

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
    std::string recipesDir = "assets/data/recipes";
    std::string resourcePackUrl;                 // optional server pack
    std::string resourcePackSha1;
    bool resourcePackForced = false;
    std::string levelType = "flat";          // flat | normal
    bool whitelist = false;
    bool onlineMode = false;
    RconConfig rcon;
    std::string levelTypeCli;
    std::uint64_t seed = 1378645410614731511ULL;
    std::int64_t startTime = 1000;
    int compressionThreshold = 256;   // -1 disables Set Compression entirely
};

// Player inventory slot = full ItemStack (components preserved end-to-end).
using InvSlot = ItemStack;

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
    // survival state
    std::uint8_t gamemode = 1;     // 0 survival 1 creative 2 adventure 3 spectator
    float health = 20.f;
    std::int32_t food = 20;
    float saturation = 5.f;
    double exhaustion = 0;
    double fallDist = 0;
    double prevFeetY = -60.0;
    bool airborne = false;
    std::array<InvSlot, 46> inv{};
    std::int32_t invStateId = 1;
    bool dead = false;
    struct LoginProp { std::string name, value, signature; };
    std::vector<LoginProp> loginProps;
    // survival dig tracking
    bool digActive = false;
    std::int32_t digX=0, digY=0, digZ=0;
    std::int64_t digStartTick = 0;
    std::int32_t digTotalTicks = 0;
    std::uint8_t digLastStage = 255;
    double sentX = 0, sentY = 0, sentZ = 0;   // last broadcast to others
    float  sentYaw = 0, sentPitch = 0;
    // experience (plan3 経験値システム)
    XpState xp{};
    // active status effects (plan3 ポーション)
    std::vector<EffectInstance> effects;
    // chat signing session (plan3 Chat signing)
    bool hasChatSession = false;
    std::vector<std::uint8_t> chatPubKey;
    std::int64_t chatSessionExpiry = 0;
    // cookies (plan3 Cookie) — opaque server-defined blobs
    std::unordered_map<std::string, std::vector<std::uint8_t>> cookies;
    // client-declared plugin channels
    std::unordered_set<std::string> clientChannels;
    Connection* conn = nullptr;
};

class GameServer;

// Per-connection session: drives the state machine on its own thread.
class Session {
public:
    Session(GameServer& srv, std::unique_ptr<Connection> conn)
        : srv_(srv), conn_(std::move(conn)) {}

    void run();
    GameServer& server() { return srv_; }

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
    void onUseEntity(ReadBuffer& in);
    void handleRespawnRequest();
    void sendDeclareCommands();
    void onChatCommand(ReadBuffer& in);
    void dispatchCommand(const std::string& line);
    void broadcastMovement();
    void broadcastSpawnEntity(Player* about);
    void onMovement(ReadBuffer& in, bool hasPos, bool hasRot);
    void onTabComplete(ReadBuffer& in);
    void handlePlaceRecipe(std::int32_t recipeId, bool makeAll);
    void sendRecipeBook();
    void onPluginPayload(const std::string& channel,
                         const api::ChannelRegistry::Payload& body, int phase);
    void sendPluginPayload(int phase, const std::string& channel,
                           const std::vector<std::uint8_t>& body);

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
    // container menus (chest/furnace/crafting)
    void onCloseContainer();
    void handleMenuClick(Menu& m, int slot, int button, int mode);
    void sendMenuContent(Menu& m);
    void sendSetSlot(std::int32_t windowId, std::int32_t stateId,
                     std::int16_t slot, const ItemStack& s);
    void syncCursorItem();
    void openMenuAt(std::int32_t x, std::int32_t y, std::int32_t z,
                    std::uint16_t stateIdOfBlock);
    void closeOpenMenu(bool sendPacket);
    void onWindowClick(ReadBuffer& in);

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
    // open container menu (chest/furnace/crafting) when any
    std::unique_ptr<Menu> openMenu_;
    ItemStack cursorItem_;
    std::int32_t menuWindowCounter_ = 0;
};

class Session;
class GameServer {
    friend class Session;
public:
    explicit GameServer(ServerConfig cfg)
        : cfg_(cfg), startTime_(cfg.startTime),
          world_(cfg_.worldBiome,
                 cfg.levelType == "normal" ? LevelType::Normal : LevelType::Flat,
                 cfg.seed) {}
    ~GameServer() { stop(); }

    void init() {
        data_.load(cfg_.assetsDir);
        gameData_.load(data_);
        whitelist_.load("whitelist.json");
        if (cfg_.whitelist) whitelist_.setEnabled(true);
        recipes_.loadDefaults();
        recipes_.loadDirectory(cfg_.recipesDir);
        initCommands();
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
        stopTickLoop();
        if (rconServer_) rconServer_->stop();
        if (persist_) persist_->stop();
        if (listenFd_ >= 0) { ::close(listenFd_); listenFd_ = -1; }
    }
    Persistence& persistence() { return *persist_; }
    void savePlayerData(const std::string& uuidHex, Player& p);
    bool loadPlayerData(const std::string& uuidHex, Player& p);
    void saveLevelData();
    void loadLevelData();
    // Cookie persistence (plan3 Cookie): world/data/cookies/<uuid>/<key>
    void storeCookie(const std::array<std::uint8_t, 16>& uuid,
                     const std::string& key,
                     const std::vector<std::uint8_t>& value);
    void eraseCookie(const std::array<std::uint8_t, 16>& uuid,
                     const std::string& key);
    std::vector<std::uint8_t> loadCookie(
        const std::array<std::uint8_t, 16>& uuid, const std::string& key);
    bool requestCookie(Player& p, const std::string& key);
    auto& mobsForTest() { return mobs_; }
    Whitelist& whitelist() { return whitelist_; }
    BlockEntityStore& blockEntities() { return blockEntities_; }
    RecipeManager& recipes() { return recipes_; }
    brigadier::CommandDispatcher& commands() { return commands_; }
    void initCommands();                             // builds command tree
    api::ServerEvents& events() { return api::events(); }
    // Resolve a selector string (@a/@e/@p/...) against players & mobs.
    brigadier::SelectorResult resolveSelector(const std::string& raw,
                                              Player* source);
    // Spawn a mob by "minecraft:zombie"-style name at position.
    bool spawnMobByTypeName(const std::string& name, double x, double y, double z);
    // Furnace smelting tick (called once per game tick).
    void furnacesTick();
    // Send the experience bar + level to one player.
    static void sendSetExperience(Player& p);
    // Apply / expire status effects for all living things (per tick).
    void effectsTick();
    // Console command dispatch (shared by chat /commands and RCON)
    std::string dispatchConsole(const std::string& line);

    // ticking & entities (Phase 3/4)
    void startTickLoop();
    void stopTickLoop();
    void tickOnce();
    void survivalTick();
    void mobsTick();
    void applyDamageToMob(MobEntity& m, float amount, const char* cause);
    // Spawn a mob of `kind` at position and broadcast it.
    void spawnMob(MobKind kind, double x, double y, double z);
    // Melee hit from a mob onto a player target (uses stats table).
    void mobAttackPlayer(MobEntity& m, Player& target);
    // Feed-to-breed handling when a player right-clicks an animal with food.
    bool tryBreedFeed(Player& p, MobEntity& m);
    // XP orbs (経験値システム)
    void spawnXpOrbs(double x, double y, double z, int totalPoints,
                     Player* directTo);
    void xpOrbsTick();
    void itemsTick();
    void trySpawnMobs();
    void spawnItemDrop(double x,double y,double z,std::uint32_t itemId,std::uint8_t cnt,
                       double vx=0,double vy=0,double vz=0);
    void broadcastSpawnItem(const ItemEntity& it);
    bool addToInventory(Player& p, std::uint32_t itemId, std::uint16_t count);
    void resendInventory(Player& p);
    void sendSetHealth(Player& p);

    void applyDamage(Player& p, float amount, const char* cause);
    void killPlayer(Player& p, const char* cause);
    static std::string uuidToHex(const std::array<std::uint8_t,16>& u) {
        std::string h; char x[4];
        for (auto b : u) { snprintf(x,3,"%02x",b); h+=x; }
        return h;
    }
    std::int64_t tickNow() const { return tickNo_; }
    std::int64_t dayTime() const { return ((tickNo_ / 10) + timeOffset_ + startTime_) % 24000; }
    bool isNight() const { auto t = dayTime(); return t >= 13000 && t < 23000; }
    void setTimeOfDay(std::int64_t target) {
        timeOffset_ += target - dayTime();
    }
    void broadcastDigStageFor(Player& p, std::int8_t st) { broadcastDigStage(p, st); }
    void broadcastDigStage(Player& p, std::int8_t stage);
    void tickDigs();

    const ServerConfig& config() const { return cfg_; }
    World& world() { return world_; }
    EmbeddedData& data() { return data_; }
    bool running() const { return running_; }

    using PlayerRef = std::shared_ptr<Player>;
    std::vector<PlayerRef> playersSnapshot() {
        std::lock_guard lk(playersMtx_);
        return players_;
    }
    std::int64_t tickNoForTest() const { return tickNo_; }
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
    // entities
    std::mutex entsMtx_;
    std::vector<std::shared_ptr<MobEntity>> mobs_;
    std::vector<std::shared_ptr<ItemEntity>> itemDrops_;
    std::vector<std::shared_ptr<XpOrbEntity>> xpOrbs_;
    std::int64_t tickNo_ = 0;
    std::int64_t timeOffset_ = 0;
    std::int64_t startTime_ = 1000;
    std::thread tickThread_;
    std::unique_ptr<Persistence> persist_;
    Whitelist whitelist_;
    std::unique_ptr<RconServer> rconServer_;
    crypto::RsaKeyPair loginKeys_;
    std::vector<std::uint8_t> loginVerifyToken_;
  public:
    std::vector<std::uint8_t>& loginVerifyToken() { return loginVerifyToken_; }
    EmbeddedData data_;
    GameData gameData_;                                 // parsed registry orders
    std::vector<PlayerRef> players_;
    std::mutex playersMtx_;
    BlockEntityStore blockEntities_;                 // chests & furnaces
    RecipeManager recipes_;                          // crafting/smelting data
    brigadier::CommandDispatcher commands_;          // Brigadier tree
    GameRuleManager gamerules_;
    std::string difficulty_ = "normal";
    std::int32_t teleportCounterForTest_ = 1;
    struct CachedChunk { std::uint64_t rev; ChunkBodyRef body; };
    std::unordered_map<std::int64_t, CachedChunk> chunkCache_;
    std::mutex chunkCacheMtx_;
    std::atomic<bool> running_{true};
    int listenFd_ = -1;
    std::int32_t entityIdCounter_ = 1;
};

} // namespace cppfm
