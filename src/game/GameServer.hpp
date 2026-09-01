// GameServer: protocol state machine (HANDSHAKE→STATUS/LOGIN→CONFIGURATION→PLAY),
// player registry, world interaction, broadcasting.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>
#include <vector>
#include <array>
#include <future>
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
#include "Constants.hpp"
#include "Containers.hpp"
#include "Recipes.hpp"
#include "TagManager.hpp"
#include "LootTables.hpp"
#include "BlockEntities.hpp"
#include "GameData.hpp"
#include "Xp.hpp"
#include "MobEffects.hpp"
#include "Stats.hpp"
#include "Scoreboard.hpp"
#include "Teams.hpp"
#include "../physics/LightEngine.hpp"
#include "../physics/Fluids.hpp"
#include "../physics/Redstone.hpp"
#include "../physics/BlockTickScheduler.hpp"
#include "../api/EventBus.hpp"
#include "../api/PluginChannels.hpp"
#include "../brigadier/Tree.hpp"
#include "GameRules.hpp"
#include "ServerEvents.hpp"
#include "AiBrain.hpp"
#include "EntityData.hpp"
#include "../net/PacketBatcher.hpp"
#include "Attributes.hpp"
#include "DamageSource.hpp"
#include "ServerProperties.hpp"
#include "BossAI.hpp"
#include "MenuLogic.hpp"
#include "WorldManager.hpp"
#include "EntityManager.hpp"
#include "InventoryController.hpp"
#include "NetworkManager.hpp"
#include "HungerManager.hpp"
#include "CombatManager.hpp"
#include "DatapackManager.hpp"
#include "FunctionEvaluator.hpp"
#include "../core/ThreadPool.hpp"

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
    bool enforcesSecureChat = false;
    RconConfig rcon;
    std::string levelTypeCli;
    std::uint64_t seed = 1378645410614731511ULL;
    std::int64_t startTime = 1000;
    int compressionThreshold = 256;   // -1 disables Set Compression entirely (N4 respects config, not hard-coded)
    int spawnProtection = 16;         // spawn-protection radius (0 disables)
    int maxLoadedChunks = 8192;       // W19 cap — 0 = unlimited, default max(8192, viewDist²*4) (plan21 §3)
    int ioWorkerThreads = 4;          // W19 async I/O workers (ThreadPool 4 for RegionFile zlib)
    bool pvp = true;                  // plan35 §5: server.properties pvp (default true)
    bool allowFlight = false;         // plan35 §5: server.properties allow-flight (default false)
    bool hardcore = false;            // plan35 §5: server.properties hardcore (default false)
};

// Player inventory slot = full ItemStack (components preserved end-to-end).
using InvSlot = ItemStack;

struct TradeOffer {
    std::uint32_t inItem;
    std::uint16_t inCount;
    std::uint32_t inItem2;
    std::uint16_t inCount2;
    std::uint32_t outItem;
    std::uint16_t outCount;
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
    // survival state
    std::uint8_t gamemode = 1;     // 0 survival 1 creative 2 adventure 3 spectator
    float health = 20.f;
    std::int32_t food = 20;
    float saturation = 5.f;
    double exhaustion = 0;
    std::int32_t foodTickTimer = 0; // plan15 strict: per-player foodTickTimer
    double fallDist = 0;
    double prevFeetY = -60.0;
    bool airborne = false;
    // survival physic ticks (76)
    std::int32_t airTicks = 300;          // 0..300, drown when 0
    std::int32_t freezeTicks = 0;         // powder snow freeze
    std::int32_t fireTicks = 0;           // burning remainder
    bool isSneaking = false;
    bool isSprinting = false;
    bool isSwimming = false;
    bool isEating = false;
    std::int32_t eatTicks = 0;
    std::uint8_t eatenFoodId = 0;
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
    // stats + advancements
    std::unique_ptr<StatsManager> stats;
    std::unique_ptr<AdvancementManager> advancements;
    std::int64_t joinTick = 0;
    // active status effects (plan3 ポーション)
    std::vector<EffectInstance> effects;
    // chat signing session (plan3 Chat signing)
    bool hasChatSession = false;
    std::vector<std::uint8_t> chatPubKey;
    std::int64_t chatSessionExpiry = 0;
    std::vector<uint8_t> lastSeenSignatures;
    // cookies (plan3 Cookie) — opaque server-defined blobs
    std::unordered_map<std::string, std::vector<std::uint8_t>> cookies;
    // current dimension: 0=overworld, -1=nether, 1=end (plan5 §1)
    std::int8_t dimension = 0;
    std::int64_t portalCooldownUntilTick = -100000;
    // sleeping state (bed)
    bool sleeping = false;
    std::int32_t bedX=0, bedY=0, bedZ=0;
    // client-declared plugin channels
    std::unordered_set<std::string> clientChannels;
    Connection* conn = nullptr;
    // PVP knockback / invuln (items 42)
    std::int32_t hurtCooldown = 0;
    std::int32_t vehicleId = -1; // riding
    std::int32_t arrowsStuck = 0;
    std::int64_t lastEnderPearlTick = -10000;
    std::int64_t invulnUntilTick = 0;
    std::int32_t enchantmentSeed = 0; // plan17 LOW I5: seeded enchanting RNG (Yarn EnchantmentScreenHandler seed)
    // plan35 §5: flight tracking for allow-flight=false kick (simple onGround-based)
    bool isFlying = false;
    std::int32_t flyingTicks = 0;
    AttributeManager attributes;
    // plan32 combat: per-player recipe unlocks for /recipe give|take
    std::unordered_set<std::string> combatRecipeUnlocks;
};

struct BlockPos {
    std::int32_t x=0, y=0, z=0;
    // plan14 §2: offset by face (0:down -Y, 1:up +Y, 2:north -Z, 3:south +Z, 4:west -X, 5:east +X)
    BlockPos offset(int face) const {
        static constexpr int FX[] = {0, 0, 0, 0, -1, 1};
        static constexpr int FY[] = {-1, 1, 0, 0, 0, 0};
        static constexpr int FZ[] = {0, 0, -1, 1, 0, 0};
        int d = (face >= 0 && face < 6) ? face : 0;
        return {x + FX[d], y + FY[d], z + FZ[d]};
    }
};
struct ItemUseContext {
    Player* player = nullptr;
    World* world = nullptr;
    BlockPos hitPos{};
    BlockPos placePos{};
    int face = 0;
    Vec3 cursor{0,0,0};
    float yaw = 0;
    bool isSneaking = false;
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
    void handlePlaceGhostRecipe(std::int32_t recipeId);
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
    void onEnchantItem(ReadBuffer& in);

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
    std::int32_t villagerWindowSeq_ = 100;
    std::int32_t tradingVillager_ = -1;  // villager entity id while trading
};

class Session;
class GameServer {
    friend class Session;
public:
    enum class Dim : std::int8_t { Overworld = 0, Nether = -1, End = 1 };

    explicit GameServer(ServerConfig cfg)
        : cfg_(cfg), startTime_(cfg.startTime),
          world_(cfg_.worldBiome,
                 cfg.levelType == "normal" ? LevelType::Normal : LevelType::Flat,
                 cfg.seed) {
        netherWorld_ = std::make_unique<World>(
            "minecraft:nether_wastes", LevelType::Nether, cfg.seed ^ 0x4E37ULL);
        endWorld_ = std::make_unique<World>(
            "minecraft:the_end", LevelType::End, cfg.seed ^ 0xE11DULL);
        worlds_[0] = &world_;
        worlds_[1] = netherWorld_.get();
        worlds_[2] = endWorld_.get();
        for (auto* w : worlds_) w->setDimensionId(
            w == &world_ ? 0 : (w == netherWorld_.get() ? -1 : 1));
    }
    World& worldFor(std::int8_t dim) {
        switch (dim) {
        case -1: return *netherWorld_;
        case 1: return *endWorld_;
        default: return world_;
        }
    }
    const World& worldFor(std::int8_t dim) const {
        return const_cast<GameServer*>(this)->worldFor(dim);
    }
    ~GameServer() { stop(); }

    void init() {
        data_.load(cfg_.assetsDir);
        gameData_.load(data_);
        whitelist_.load("whitelist.json");
        if (cfg_.whitelist) whitelist_.setEnabled(true);
        loadOps();
        loadBans();
        loadBannedIps();
        recipes_.loadDefaults();
        recipes_.loadDirectory(cfg_.recipesDir);
        entityDataLoader_.loadDirectory("assets/entities");
        for (auto &kv : entityDataLoader_.all()) {
            const auto &def = kv.second;
            std::fprintf(stderr, "[cppfm] entity data: %s max_health=%.1f speed=%.3f\n",
                         def.type.c_str(), def.max_health, def.movement_speed);
        }
        tagManager_.loadDirectory("assets/data/tags");
        tagManager_.applyToRecipeTags(recipes_.tags_);
        recipes_.syncTagsFrom(tagManager_); // plan37 B-03 double sync before recipes (also after loadDirectory)
        lootTables_.loadDirectory("assets/data/loot_tables");
        // Plan13 datapackmanager (advancements/predicates/item_modifiers/functions)
        datapackManager_.loadAll(recipes_, "assets/data", cfg_.worldDir + "/datapacks");
        tagManager_ = datapackManager_.tagManager;
        lootTables_ = datapackManager_.lootTables;
        // final double sync after DatapackManager merged (covers world/datapacks tags)
        tagManager_.applyToRecipeTags(recipes_.tags_);
        recipes_.syncTagsFrom(tagManager_);
        functionEvaluator_.setServer(this);
        initCommands();
        lightEngine_ = std::make_unique<LightEngine>(world_);
        world_.setBiomeCodec(
            [this](const std::string& key) {
                return data_.biomeIndex(key);
            },
            static_cast<std::int32_t>(
                data_.biomeIndex(cfg_.worldBiome)));
        fluidSim_ = std::make_unique<FluidSim>(world_);
        redstone_ = std::make_unique<RedstoneEngine>(world_);
        blockTicks_ = std::make_unique<BlockTickScheduler>(world_, &gamerules_, this);
        blockTicks_->registerBehavior("minecraft:wheat", std::make_unique<CropBehavior>());
        blockTicks_->registerBehavior("minecraft:potatoes", std::make_unique<CropBehavior>());
        blockTicks_->registerBehavior("minecraft:carrots", std::make_unique<CropBehavior>());
        blockTicks_->registerBehavior("minecraft:beetroots", std::make_unique<CropBehavior>());
        blockTicks_->registerBehavior("minecraft:oak_sapling", std::make_unique<SaplingBehavior>());
        blockTicks_->registerBehavior("minecraft:spruce_sapling", std::make_unique<SaplingBehavior>());
        blockTicks_->registerBehavior("minecraft:birch_sapling", std::make_unique<SaplingBehavior>());
        blockTicks_->registerBehavior("minecraft:jungle_sapling", std::make_unique<SaplingBehavior>());
        blockTicks_->registerBehavior("minecraft:bamboo", std::make_unique<StemBehavior>(16));
        blockTicks_->registerBehavior("minecraft:sugar_cane", std::make_unique<StemBehavior>(4));
        blockTicks_->registerBehavior("minecraft:cactus", std::make_unique<StemBehavior>(4));
        blockTicks_->registerBehavior("minecraft:farmland", std::make_unique<FarmlandBehavior>());
        blockTicks_->registerBehavior("minecraft:cocoa", std::make_unique<CocoaBehavior>());
        blockTicks_->registerBehavior("minecraft:sweet_berry_bush", std::make_unique<SweetBerryBehavior>());
        blockTicks_->registerBehavior("minecraft:sweet_berries", std::make_unique<SweetBerryBehavior>());
        blockTicks_->registerBehavior("minecraft:nether_wart", std::make_unique<NetherWartBehavior>());
        blockTicks_->registerBehavior("minecraft:chorus_flower", std::make_unique<ChorusFlowerBehavior>());
        blockTicks_->registerBehavior("minecraft:kelp", std::make_unique<KelpBehavior>());
        blockTicks_->registerBehavior("minecraft:seagrass", std::make_unique<SeagrassBehavior>());
        // kelp_plant has no randomTick (vanilla KelpPlant) — not registered (plan22 B26)
        // tall_seagrass has no randomTick — vanilla TallSeagrass has no randomTick (plan17 §9 B26)
        // bone meal growth for kelp/seagrass via fertilize() (plan29 §10 tall_seagrass)
        blockTicks_->registerBehavior("minecraft:fire", std::make_unique<FireBehavior>());
        blockTicks_->registerBehavior("minecraft:soul_fire", std::make_unique<SoulFireBehavior>());
        blockTicks_->registerBehavior("minecraft:campfire", std::make_unique<CampfireBehavior>());
        blockTicks_->registerBehavior("minecraft:soul_campfire", std::make_unique<CampfireBehavior>());
        blockTicks_->registerBehavior("minecraft:nether_portal", std::make_unique<PortalAgeBehavior>());
        blockTicks_->registerBehavior("minecraft:torchflower_crop", std::make_unique<CropBehavior>());
        blockTicks_->registerBehavior("minecraft:pitcher_crop", std::make_unique<CropBehavior>());
        blockTicks_->registerBehavior("minecraft:pale_oak_leaves", std::make_unique<PaleOakLeavesBehavior>());
        blockTicks_->registerBehavior("minecraft:creaking_heart", std::make_unique<CreakingHeartBehavior>());
        // plan7: ServerProperties typed loading (viewDistance, spawn-protection, etc.)
        {
            ServerProperties sp;
            if (sp.load("server.properties")) {
                cfg_.viewDistance = std::clamp(sp.get<int>("view-distance", cfg_.viewDistance), 2, 32);
                cfg_.simulationDistance = std::clamp(sp.get<int>("simulation-distance", cfg_.simulationDistance), 2, 32);
                cfg_.spawnProtection = std::max(0, sp.get<int>("spawn-protection", cfg_.spawnProtection));
                // W19 maxLoadedChunks polish (plan21 §3): auto max(8192, viewDist²*4) when not configured
                if (sp.has("max-loaded-chunks") || sp.has("maxLoadedChunks")) {
                    cfg_.maxLoadedChunks = std::max(0, sp.get<int>("max-loaded-chunks", sp.get<int>("maxLoadedChunks", cfg_.maxLoadedChunks)));
                } else {
                    cfg_.maxLoadedChunks = std::max(8192, cfg_.viewDistance * cfg_.viewDistance * 4);
                }
                // also mirror to world
                world_.setSimulationDistance(cfg_.simulationDistance);
                if (netherWorld_) netherWorld_->setSimulationDistance(cfg_.simulationDistance);
                if (endWorld_) endWorld_->setSimulationDistance(cfg_.simulationDistance);
            }
        }
        // plan7: World simulation-distance responsibility – engines use World::isPositionInSimulationDistance
        world_.setSimulationDistance(cfg_.simulationDistance);
        if (netherWorld_) netherWorld_->setSimulationDistance(cfg_.simulationDistance);
        if (endWorld_) endWorld_->setSimulationDistance(cfg_.simulationDistance);
        auto simCb = [this](std::int32_t cx, std::int32_t cz) -> bool {
            return this->isChunkInSimulationDistance(cx, cz);
        };
        world_.setSimulationDistanceCallback(simCb);
        if (netherWorld_) netherWorld_->setSimulationDistanceCallback(simCb);
        if (endWorld_) endWorld_->setSimulationDistanceCallback(simCb);

        redstone_->setBlockEntityStore(&blockEntities_);
        redstone_->setTickRef(&tickNo_);
        redstone_->setBroadcastFn([this](std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t s){
            this->queueBlockChange(x,y,z,s);
            this->invalidateChunkCache(x>>4, z>>4);
        });
        world_.setOnBlockChanged([this](std::int32_t x, std::int32_t y,
                                        std::int32_t z, std::uint16_t o,
                                        std::uint16_t n) {
            lightEngine_->onBlockChanged(x, y, z, o, n);
            fluidSim_->touch(x, y, z);
            static constexpr int DX[6] = {1,-1,0,0,0,0};
            static constexpr int DY[6] = {0,0,1,-1,0,0};
            static constexpr int DZ[6] = {0,0,0,0,1,-1};
            for (int d = 0; d < 6; ++d)
                fluidSim_->touch(x + DX[d], y + DY[d], z + DZ[d]);
            redstone_->onBlockChanged(x, y, z);
        });
        // plan6 §9: seed persistence with current border/difficulty
        spawnProtection_ = cfg_.spawnProtection;
        persist_ = std::make_unique<Persistence>(world_, cfg_.worldDir, cfg_.worldBiome);
        persist_->setDifficulty(difficulty_);
        persist_->setWorldBorder(worldBorderDiameter_, worldBorderCenterX_, worldBorderCenterZ_);
        loadOps();
        {   // biome codec maps + chunk extras (block entities)
            std::unordered_map<std::uint16_t, std::string> idxToKey;
            const auto& order = gameData_.order("minecraft:worldgen/biome");
            for (std::size_t i = 0; i < order.size(); ++i)
                idxToKey.emplace(static_cast<std::uint16_t>(i), order[i]);
            persist_->setBiomeCodec(std::move(idxToKey),
                                    static_cast<std::int32_t>(
                                        data_.biomeIndex(cfg_.worldBiome)));
            persist_->setChunkExtras(
                [this](std::int32_t cx, std::int32_t cz, nbt::Value& root) {
                    nbt::Value list = nbt::Value::makeList(nbt::Compound);
                    blockEntities_.writeChunkNbt(cx, cz, list);
                    if (!list.list.empty()) root.set("block_entities", list);
                },
                [this](const nbt::Value& root) {
                    blockEntities_.readChunkNbt(root);
                });
        }
        persist_->setLevelStateProvider(
            [this](nbt::Value& data) {
                namespace nv = nbt;
                nv::Value gr = nv::Value::makeCompound();
                for (auto& [k, v] : gamerules_.all())
                    gr.set(k, nv::Value::makeString(v));
                data.set("GameRules", gr);
                data.set("raining", nv::Value::makeByte(raining() ? 1 : 0));
                data.set("rainTime",
                         nv::Value::makeInt(static_cast<std::int32_t>(
                             (weatherUntilTick_ - tickNo_) / 20)));
                data.set("thundering", nv::Value::makeByte(0));
                data.set("thunderTime", nv::Value::makeInt(6000));
                data.set("DayTime", nv::Value::makeLong(dayTime()));
                data.set("Time", nv::Value::makeLong(tickNo_));
                data.set("Difficulty", nv::Value::makeString(difficulty_));
                data.set("WanderingTraderSpawnDelay", nv::Value::makeInt(wanderingTraderSpawnDelay_));
                data.set("WanderingTraderSpawnChance", nv::Value::makeInt(wanderingTraderSpawnChance_));
                data.set("WanderingTraderId", nv::Value::makeCompound());
            },
            [this](const nbt::Value& data) {
                if (const auto* t = data.get("Time"))
                    tickNo_ = t->l;
                if (const auto* dt = data.get("DayTime"))
                    timeOffset_ += dt->l - dayTime();
                if (const auto* r = data.get("raining"))
                    weather_ = r->b ? Weather::Rain : Weather::Clear;
                if (const auto* rt = data.get("rainTime"))
                    weatherUntilTick_ = tickNo_ + rt->i * 20LL;
                if (const auto* gr = data.get("GameRules"))
                    for (auto& [k, v] : gr->comp) {
                        // W18: handle both String "true"/"3" and Int/String mixed NBT (DataFixer compat)
                        std::string sv;
                        if (v.tag == nbt::String) sv = v.str;
                        else if (v.tag == nbt::Int) sv = std::to_string(v.i);
                        else if (v.tag == nbt::Long) sv = std::to_string(v.l);
                        else if (v.tag == nbt::Byte) sv = v.b ? "true" : "false";
                        else sv = v.str;
                        gamerules_.set(k, sv, false);
                    }
                if (const auto* diff = data.get("Difficulty")) {
                    if (diff->tag==nbt::String) difficulty_ = diff->str;
                    else if (diff->tag==nbt::Byte) {
                        int v = diff->b;
                        if (v==0) difficulty_="peaceful"; else if (v==1) difficulty_="easy";
                        else if (v==2) difficulty_="normal"; else if (v==3) difficulty_="hard";
                    }
                }
                if (const auto* wb = data.get("WorldBorder")) {
                    if (auto* cx = wb->get("CenterX")) worldBorderCenterX_ = cx->d;
                    if (auto* cz = wb->get("CenterZ")) worldBorderCenterZ_ = cz->d;
                    if (auto* sz = wb->get("Size")) {
                        if (sz->tag==nbt::Double) worldBorderDiameter_ = sz->d;
                        else if (sz->tag==nbt::Float) worldBorderDiameter_ = sz->f;
                        else if (sz->tag==nbt::Int) worldBorderDiameter_ = sz->i;
                        else if (sz->tag==nbt::Long) worldBorderDiameter_ = (double)sz->l;
                    }
                }
                if (const auto* d = data.get("WanderingTraderSpawnDelay")) {
                    if (d->tag==nbt::Int) wanderingTraderSpawnDelay_ = d->i;
                    else if (d->tag==nbt::Long) wanderingTraderSpawnDelay_ = (int)d->l;
                }
                if (const auto* c = data.get("WanderingTraderSpawnChance")) {
                    if (c->tag==nbt::Int) wanderingTraderSpawnChance_ = c->i;
                    else if (c->tag==nbt::Long) wanderingTraderSpawnChance_ = (int)c->l;
                }
            });
        persist_->loadLevelData();
        // W16 single level.dat migration: remove legacy DIM level.dat files (strict)
        for (auto sub : {"DIM-1", "DIM1"}) {
            std::string p = cfg_.worldDir + "/" + std::string(sub) + "/level.dat";
            if (std::filesystem::exists(p)) {
                std::fprintf(stderr, "[cppfm] found legacy %s, removing (single level.dat now)\n", p.c_str());
                std::error_code ec; std::filesystem::remove(p, ec);
            }
            std::string pn = cfg_.worldDir + "/" + std::string(sub) + "/level.dat.new";
            if (std::filesystem::exists(pn)) { std::error_code ec; std::filesystem::remove(pn, ec); }
        }
        // sync persistence's worldborder/difficulty (file may have overridden) — include lerp
        difficulty_ = persist_->difficulty();
        worldBorderDiameter_ = persist_->worldBorderDiameter();
        worldBorderCenterX_ = persist_->worldBorderCenterX();
        worldBorderCenterZ_ = persist_->worldBorderCenterZ();
        worldBorderLerpFrom_ = persist_->worldBorderLerpFrom();
        worldBorderLerpTo_ = persist_->worldBorderLerpTo();
        worldBorderLerpMs_ = persist_->worldBorderLerpMs();
        worldBorderLerpRemainingTicks_ = persist_->worldBorderLerpRemainingTicks();
        worldBorderLerpTotalTicks_ = worldBorderLerpRemainingTicks_;
        // plan5 §1: spawn-chunk loader — generate a 5x5 area around spawn with ChunkTicket SPAWN level 31 (ForcedChunks NBT)
        {
            const auto sp = world_.spawnPoint();
            for (int dz = -2; dz <= 2; ++dz)
                for (int dx = -2; dx <= 2; ++dx) {
                    const std::int32_t cx = (sp.x >> 4) + dx;
                    const std::int32_t cz = (sp.z >> 4) + dz;
                    world_.generateChunkIfMissing(cx, cz);
                    world_.addSpawnTicket(cx, cz, tickNo_);
                }
        }
        persist_->start();
        // plan5 §1: per-dimension persistence (DIM-1 / DIM1)
        for (int d = 0; d < 2; ++d) {
            auto& pw = dimPersist_[d];
            const char* sub = d == 0 ? "DIM-1" : "DIM1";
            pw = std::make_unique<Persistence>(worldFor(d == 0 ? -1 : 1),
                                               cfg_.worldDir + "/" + sub, "");
            pw->start();
        }
        rconServer_ = std::make_unique<RconServer>(cfg_.rcon,
            [this](const std::string& cmd){ return dispatchConsole(cmd); });
        const bool rconUp = rconServer_->start();
        std::fprintf(stderr, "[cppfm] RCON %s (enabled=%d port=%u)\n",
                     rconUp ? "listening" : "not started", (int)cfg_.rcon.enabled,
                     cfg_.rcon.port);
        // Plan8 modular split: instantiate managers (facade)
        worldMgr_ = std::make_unique<WorldManager>(world_, *netherWorld_, *endWorld_);
        entityMgr_ = std::make_unique<EntityManager>();
        networkMgr_ = std::make_unique<NetworkManager>(batcher_);
        // HungerManager/CombatManager are stateless, no instance needed
        // Plan7 BossAI
        bossAI_ = std::make_unique<BossAIManager>(*this);
    }
    void runForever();
    void requestStop() {                 // async-signal-safe minimal path
        running_ = false;
        if (listenFd_ >= 0) {
            int fd = listenFd_;
            ::shutdown(fd, SHUT_RDWR);   // wake acceptLoop
        }
    }
    void stop() {
        requestStop();
        std::fprintf(stderr, "[cppfm] stopping tick loop\n");
        stopTickLoop();
        std::fprintf(stderr, "[cppfm] stopping rcon\n");
        if (rconServer_) rconServer_->stop();
        std::fprintf(stderr, "[cppfm] stopping persistence\n");
        if (persist_) persist_->stop();
        // plan28 finish: nether/end persistence workers were never stopped; their
        // ~Persistence (cv_ destroyed before worker_ in reverse member order) then
        // livelocked on the futex during ~GameServer. Stop (and save) them here.
        for (auto& d : dimPersist_) if (d) d->stop();
        std::fprintf(stderr, "[cppfm] closing listen fd\n");
        if (listenFd_ >= 0) { ::close(listenFd_); listenFd_ = -1; }
        std::fprintf(stderr, "[cppfm] stopped cleanly\n");
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
    std::int32_t villagerWindowSeq_ = 100;
    Scoreboard scoreboard;
    TeamsManager teams;
    void scoreboardBroadcast(const std::function<void(WriteBuffer&)>& fn) {
        WriteBuffer b; fn(b);
        broadcastPacketExcept(nullptr, 0, b); // id unused; callers send directly
    }
    void sendObjectiveAll(const Scoreboard::Objective& o, std::int8_t method) {
        WriteBuffer b; scoreboard.writeObjectivePacket(b, o, method);
        broadcastPacketExcept(nullptr, proto::pl::sc::ScoreboardObjective, b);
    }
    void sendScoreAll(const std::string& obj, const std::string& holder,
                      std::int32_t v) {
        WriteBuffer b; scoreboard.writeScorePacket(b, obj, holder, v);
        broadcastPacketExcept(nullptr, proto::pl::sc::ScoreboardScore, b);
    }
    // D26: reset_score 0x49 (wire: string holder + PrefixedOptional<string> objectiveName)
    void sendResetScoreAll(const std::string& holder, const std::string* objective) {
        WriteBuffer b; scoreboard.writeResetScorePacket(b, holder, objective);
        broadcastPacketExcept(nullptr, proto::pl::sc::ResetScore, b);
    }
    void sendResetScoreAllWildcard(const std::string& holder) {
        sendResetScoreAll(holder, nullptr); // wildcard: objective_name present=false
    }
    void sendDisplayAll() {
        WriteBuffer b; scoreboard.writeDisplayPacket(b);
        broadcastPacketExcept(nullptr,
                              proto::pl::sc::ScoreboardDisplayObjective, b);
    }
    void sendTeamsCreate(const Team& t) {
        WriteBuffer b; TeamsManager::writeCreate(b, t);
        broadcastPacketExcept(nullptr, proto::pl::sc::Teams, b);
    }
    void sendTeamsRemove(const std::string& name) {
        WriteBuffer b; TeamsManager::writeRemove(b, name);
        broadcastPacketExcept(nullptr, proto::pl::sc::Teams, b);
    }
    void sendTeamsJoin(const std::string& team, const std::vector<std::string>& members) {
        WriteBuffer b; TeamsManager::writeAddMembers(b, team, members);
        broadcastPacketExcept(nullptr, proto::pl::sc::Teams, b);
    }
    void sendTeamsLeave(const std::string& team, const std::vector<std::string>& members) {
        WriteBuffer b; TeamsManager::writeRemoveMembers(b, team, members);
        broadcastPacketExcept(nullptr, proto::pl::sc::Teams, b);
    }
    RecipeManager& recipes() { return recipes_; }
    brigadier::CommandDispatcher& commands() { return commands_; }
    void initCommands();                             // builds command tree
    api::ServerEvents& events() { return api::events(); }
    LightEngine& lights() { return *lightEngine_; }
    GameRuleManager& gameRules() { return gamerules_; }
    const GameRuleManager& gameRules() const { return gamerules_; }
    BlockTickScheduler* blockTicks() { return blockTicks_.get(); }
    // Resolve a selector string (@a/@e/@p/...) against players & mobs.
    brigadier::SelectorResult resolveSelector(const std::string& raw,
                                              Player* source);
    // Spawn a mob by "minecraft:zombie"-style name at position.
    bool spawnMobByTypeName(const std::string& name, double x, double y, double z);
    // plan14 §2 Spawn eggs UseItemOn: trySpawnEgg handling (itemName endsWith _spawn_egg, spawnPos=pos.offset(face), check air, spawnMobByTypeName, consume if not creative)
    bool trySpawnEgg(Player& p, ItemStack& stack, BlockPos hitPos, int face);
    // Furnace smelting tick (called once per game tick).
    void furnacesTick();
    void brewingTick();
    // Hopper item movement + dispenser ejection (every 8 ticks).
    void hoppersTick();
    // Chunk LRU / simulation distance (plan5 items 6,7,8)
    bool isChunkInSimulationDistance(std::int32_t cx, std::int32_t cz) const;
    void chunksUnloadTick();
    // Direct inventory access helpers used by the hopper simulation.
    ItemStack* containerAt(std::int32_t x, std::int32_t y, std::int32_t z,
                           int& countOut, BlockEntity::Kind& kindOut);
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
    void drainPendingStructureQueues(); // plan36 §5: drain StructureManager pending loot/mobs tick
    void applyDamageToMob(MobEntity& m, float amount, const char* cause);
    void applyDamageToMob(MobEntity& m, float amount, const DamageSource& src);
    void growResinNearHeart(int hx,int hy,int hz);
    // Spawn a mob of `kind` at position and broadcast it.
    void spawnMob(MobKind kind, double x, double y, double z);
    void broadcastMobSpawn(const MobEntity& mob);   // no locking inside
    // Melee hit from a mob onto a player target (uses stats table).
    void mobAttackPlayer(MobEntity& m, Player& target);
    // Feed-to-breed handling when a player right-clicks an animal with food.
    bool tryBreedFeed(Player& p, MobEntity& m);
    // Equipment / riding sync (plan5 30-47) — plan13 §2 dynamic sync, ArmorTrim, HandDropChances
    void sendEquipment(const MobEntity& mob);
    void sendEquipmentSlot(const MobEntity& mob, int slot);
    void broadcastPlayerEquipment(const Player& p);
    void syncEquipmentOnChange(Player& p); // helper for armor/hand changes
    void broadcastSetPassengers(std::int32_t vehicleId);
    void broadcastSetPassengersEmpty(std::int32_t vehicleId);
    void handleMoveVehicle(Player& p, double x, double y, double z, float yaw, float pitch);
    void handleHorseJump(Player& p, int power); // plan13 §3 horse jump
    // XP orbs (経験値システム)
    void spawnXpOrbs(double x, double y, double z, int totalPoints,
                     Player* directTo);
    void xpOrbsTick();
    // Projectiles (arrows / snowballs / pearls) — plan4 P1-A
    void spawnProjectile(ProjectileKind kind, double x, double y, double z,
                         double vx, double vy, double vz,
                         std::int32_t ownerId, bool ownerIsPlayer, bool charged = false);
    void projectilesTick();
    // Rails / minecart physics (plan11 §3) + boat physics (plan13 §3)
    void minecartsTick();
    void boatsTick();
    // Villager trading (plan4 P1-B)
    static const std::vector<struct TradeOffer>& tradeTable();
    bool openTrading(Player& p, MobEntity& villager);
    bool selectTrade(Player& p, std::int32_t index);
    // Progress tracking (stats + advancements)
    void initPlayerProgress(Player& p);
    void savePlayerProgress(Player& p);
    void grantAdvancement(Player& p, const std::string& id);
    void sendAdvancementsTo(Player& p, bool reset);
    void onBlockMined(Player& p, std::uint16_t oldState);
    void onItemObtained(Player& p, const ItemStack& s, const char* how);
    void onMobKilledBy(Player& p, MobKind kind);
    // plan35 §1: trigger evaluation helpers
    void evaluateTickAdvancements(Player& p);
    void evaluateInventoryChanged(Player& p, const ItemStack& s);
    void evaluatePlayerKilledEntity(Player& p, MobKind kind);
    std::vector<AdvancementDefOwned> getMergedAdvancements();
private:
    // plan35 cached merged advancements (story 30 + cppfm 9)
    std::vector<AdvancementDefOwned> cachedMergedAdv_;
    size_t cachedAdvRawSize_ = 0;
public:
    // Explosion (creeper / TNT): destroys blocks & damages entities.
    void explodeAt(double x, double y, double z, float power);
    void spawnPrimedTnt(double x,double y,double z,double vx,double vy,double vz,int fuse=80);
    void tntTick();
    // Lightning strike: charges creepers, spawns bolt visuals (plan8)
    void strikeLightning(double x, double y, double z);
    // Direct-named sound + particle broadcast helpers.
    void broadcastSound(const char* name, double x, double y, double z,
                        float volume = 1.f, float pitch = 1.f,
                        const char* category = "block");
    enum class SoundSource : std::int32_t { Master=0, Music=1, Record=2, Weather=3, Block=4, Hostile=5, Neutral=6, Player=7, Ambient=8, Voice=9 };
    // D22 StopSound 0x71: flags i8 (1=source,2=sound), optional varint source, optional string sound
    void broadcastStopSound(const std::optional<SoundSource>& source,
                            const std::optional<std::string>& sound);
    void broadcastStopSound(SoundSource source, const std::string* soundOrNull);
    void broadcastStopSound(SoundSource source);
    void stopRecord(const std::string& discNameWithoutPrefix); // record category
    void broadcastWorldEvent(std::int32_t eventId, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t data, bool disableRelativeVolume = false);
    // plan26 D20: per-type particle helpers (block/dust/pale_oak_leaves etc) - correct wire (amount + switch)
    void broadcastBlockParticle(double x, double y, double z, std::uint32_t blockState, int count = 10);
    void broadcastDustParticle(double x, double y, double z, std::int32_t rgb, float scale = 1.0f);
    void broadcastPaleOakLeavesParticle(double x, double y, double z); // D19 helper
    // plan34 network: 6 toClient (ActionBar 0x51 prismarine packet_action_bar {text:anonymousNbt}, ServerData 0x50 {motd:anonymousNbt,iconBytes:option<ByteArray>}, HurtAnimation 0x25 {entityId:varint,yaw:f32}, EntitySoundEffect 0x6E {sound:ItemSoundHolder,soundCategory:varint,entityId:varint,volume:f32,pitch:f32,seed:i64}, ChatSuggestions 0x18 {action:varint,entries:array<string>}, SyncEntityPosition 0x20 {entityId:varint,x:f64,y:f64,z:f64,dx:f64,dy:f64,dz:f64,yaw:f32,pitch:f32,onGround:bool})
    void sendActionBar(Player& p, const std::string& text);
    void broadcastActionBar(const std::string& text, Player* except = nullptr);
    void sendServerData(Player& p);
    void broadcastServerData();
    void sendHurtAnimation(Player& p, std::int32_t entityId, float yaw);
    void broadcastHurtAnimation(std::int32_t entityId, float yaw, Player* except = nullptr);
    void broadcastEntitySound(std::int32_t entityId, const std::string& soundName, float volume = 1.f, float pitch = 1.f, SoundSource category = SoundSource::Neutral);
    void sendEntitySound(Player& p, std::int32_t entityId, const std::string& soundName, float volume = 1.f, float pitch = 1.f, SoundSource category = SoundSource::Neutral);
    void sendChatSuggestions(Player& p, std::int32_t action, const std::vector<std::string>& entries);
    void broadcastChatSuggestions(std::int32_t action, const std::vector<std::string>& entries, Player* except = nullptr);
    void sendSyncEntityPosition(Player& p, std::int32_t entityId, double x, double y, double z, double dx=0, double dy=0, double dz=0, float yaw=0, float pitch=0, bool onGround=true);
    void broadcastSyncEntityPosition(std::int32_t entityId, double x, double y, double z, double dx=0, double dy=0, double dz=0, float yaw=0, float pitch=0, bool onGround=true, Player* except=nullptr);
    void sendSyncEntityPosition(Player& p, const MobEntity& mob);
    void broadcastSyncEntityPosition(const MobEntity& mob, Player* except=nullptr);
    void itemsTick();
    void trySpawnMobs();
    void spawnItemDrop(double x,double y,double z,std::uint32_t itemId,std::uint8_t cnt,
                       double vx=0,double vy=0,double vz=0);
    // D11 (plan26 §4): ItemStack overload preserves components (enchant/trim/damage) for Slot type 7
    void spawnItemDrop(double x,double y,double z,const ItemStack& stack,
                       double vx=0,double vy=0,double vz=0);
    void broadcastSpawnItem(const ItemEntity& it);
    bool addToInventory(Player& p, std::uint32_t itemId, std::uint16_t count);
    void resendInventory(Player& p);
    void sendSetHealth(Player& p);

    void applyDamage(Player& p, float amount, const char* cause);
    void applyDamage(Player& p, float amount, const DamageSource& src);
    void killPlayer(Player& p, const char* cause);
    void syncPlayerArmorAttributes(Player& p);
    void addHungerExhaustion(Player& p, float amount);
    void addFoodAndSaturation(Player& p, int food, float sat);
    void handleFoodConsume(Player& p, const std::string& itemName);
    static std::string uuidToHex(const std::array<std::uint8_t,16>& u) {
        std::string h; char x[4];
        for (auto b : u) { snprintf(x,3,"%02x",b); h+=x; }
        return h;
    }
    static std::string uuidToDashed(const std::array<std::uint8_t,16>& u) {
        const std::string h = uuidToHex(u);
        return h.substr(0,8)+"-"+h.substr(8,4)+"-"+h.substr(12,4)+"-"+
               h.substr(16,4)+"-"+h.substr(20,12);
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
    World& worldByDim(std::int8_t d) { return worldFor(d); }
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
                              std::uint16_t state);
    void queueBlockChange(std::int32_t x, std::int32_t y, std::int32_t z,
                          std::uint16_t state);
    void flushBlockBatches();
    void broadcastPlayerChat(Player& sender, const std::string& message, std::int64_t timestamp);
    bool validateFeatureFlags(const std::vector<std::array<std::string,3>>& clientPacks);
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
    void clearChunkCache() {
        std::lock_guard lk(chunkCacheMtx_);
        chunkCache_.clear();
    }

private:
    void acceptLoop();

    ServerConfig cfg_;
    EntityDataLoader entityDataLoader_;
    World world_;
    std::unique_ptr<World> netherWorld_, endWorld_;
    World* worlds_[3] = {};
    // entities
    std::mutex entsMtx_;
    struct MobAiEntry {
        std::unique_ptr<Brain> brain;
        std::unique_ptr<AiContext> ctx;
    };
    std::unordered_map<std::int32_t, MobAiEntry> mobAi_;
    MobAiEntry& aiFor(const std::shared_ptr<MobEntity>& m);
public:
    // Finds an in-love adult partner of the same kind within 8 blocks.
    std::shared_ptr<MobEntity> findLovePartner(const MobEntity& seeker);
    MobEntity* brainTickGuard_ = nullptr;   // set while AI ticks a mob
private:
    std::vector<std::shared_ptr<MobEntity>> mobs_;
    std::vector<std::shared_ptr<ItemEntity>> itemDrops_;
    std::vector<std::shared_ptr<XpOrbEntity>> xpOrbs_;
    std::vector<std::shared_ptr<ProjectileEntity>> projectiles_;
    std::vector<std::shared_ptr<TntEntity>> tntEntities_;
    std::unordered_map<std::int64_t, bool> dispenserPower_;
    std::int64_t tickNo_ = 0;
    std::int64_t timeOffset_ = 0;
    std::int64_t startTime_ = 1000;
    PacketBatcher batcher_;
    std::int64_t lastBlockBatchFlushMs_ = 0;
    std::thread tickThread_;
    std::unique_ptr<Persistence> persist_;
    std::unique_ptr<Persistence> dimPersist_[2];
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
    TagManager tagManager_;
    LootTableEvaluator lootTables_;
    brigadier::CommandDispatcher commands_;          // Brigadier tree
    GameRuleManager gamerules_;
    std::string difficulty_ = "normal";
    double worldBorderDiameter_ = constants::kWorldBorderDiameter;   // vanilla default 5.9999968E7
    double worldBorderCenterX_ = 0, worldBorderCenterZ_ = 0;
    double worldBorderLerpFrom_ = constants::kWorldBorderDiameter;
    double worldBorderLerpTo_ = constants::kWorldBorderDiameter;
    std::int64_t worldBorderLerpRemainingTicks_ = 0;
    std::int64_t worldBorderLerpTotalTicks_ = 0;
    std::int64_t worldBorderLerpMs_ = 0;
    int spawnProtection_ = 16;               // server.properties spawn-protection (default 16)
    std::unordered_set<std::string> ops_;    // ops.json / op list
    std::unordered_set<std::string> bannedPlayers_; // banned-players.json
    std::unordered_set<std::string> bannedIps_; // banned-ips.json
    std::int32_t teleportCounterForTest_ = 1;
    // WanderingTrader scheduling (vanilla WanderingTraderManager)
    int wanderingTraderSpawnDelay_ = 0;
    int wanderingTraderSpawnChance_ = 25;

public:
    // WorldBorder helpers (plan6 §10) — diameter 59999968, Chebyshev square
    bool isInsideBorder(double x, double z) const {
        double half = worldBorderDiameter_ * 0.5;
        return std::abs(x - worldBorderCenterX_) <= half && std::abs(z - worldBorderCenterZ_) <= half;
    }
    void setWorldBorderLerp(double from, double to, std::int64_t remainingTicks) {
        worldBorderLerpFrom_ = from; worldBorderLerpTo_ = to;
        worldBorderLerpRemainingTicks_ = remainingTicks;
        worldBorderLerpTotalTicks_ = remainingTicks;
        worldBorderLerpMs_ = remainingTicks * 50;
        worldBorderDiameter_ = (remainingTicks <= 0) ? to : from;
    }
    bool tickWorldBorder() {
        if (worldBorderLerpRemainingTicks_ <= 0) return false;
        --worldBorderLerpRemainingTicks_;
        worldBorderLerpMs_ = worldBorderLerpRemainingTicks_ * 50;
        if (worldBorderLerpRemainingTicks_ <= 0) {
            worldBorderDiameter_ = worldBorderLerpTo_;
            return true;
        }
        double prog = 1.0 - double(worldBorderLerpRemainingTicks_) / double(worldBorderLerpTotalTicks_ > 0 ? worldBorderLerpTotalTicks_ : 1);
        if (prog < 0) prog = 0;
        if (prog > 1) prog = 1;
        worldBorderDiameter_ = worldBorderLerpFrom_ + (worldBorderLerpTo_ - worldBorderLerpFrom_) * prog;
        return true;
    }
    double worldBorderDamagePerBlock() const { return 0.2; }
    double worldBorderSafeZone() const { return 5.0; }
    double worldBorderDamageBuffer() const { return 5.0; }
    void tickWanderingTrader() {
        // vanilla WanderingTraderManager: gated by doTraderSpawning (or doMobSpawning)
        if (!gamerules_.getBool("doTraderSpawning")) {
            // fallback check legacy name variant
            if (gamerules_.contains("doTraderSpawning") && !gamerules_.getBool("doTraderSpawning")) return;
            // if gamerule missing, default true, so only return when explicitly false
            // also respect doMobSpawning as global kill-switch per audit
            if (gamerules_.contains("doMobSpawning") && !gamerules_.getBool("doMobSpawning")) return;
        }
        if (wanderingTraderSpawnDelay_ > 0) { --wanderingTraderSpawnDelay_; return; }
        int roll = std::rand() % 100;
        bool shouldSpawn = roll < wanderingTraderSpawnChance_;
        if (shouldSpawn) {
            auto players = playersSnapshot();
            if (!players.empty()) {
                auto &p = players[std::rand() % players.size()];
                if (p->inPlay) {
                    double sx = p->x + (std::rand()%48 - 24);
                    double sz = p->z + (std::rand()%48 - 24);
                    double sy = p->y;
                    for (int y = (int)sy + 10; y > (int)sy - 10; --y) {
                        if (world_.getBlock((int)sx, y, (int)sz)==0 && world_.getBlock((int)sx, y-1, (int)sz)!=0) { sy = y; break; }
                    }
                    spawnMob(MobKind::WanderingTrader, sx, sy, sz);
                    wanderingTraderSpawnChance_ = 25;
                }
            }
            wanderingTraderSpawnDelay_ = 24000;
        } else {
            wanderingTraderSpawnChance_ = std::min(75, wanderingTraderSpawnChance_ + 25);
            wanderingTraderSpawnDelay_ = 24000;
        }
    }
    int wanderingTraderSpawnDelay() const { return wanderingTraderSpawnDelay_; }
    int wanderingTraderSpawnChance() const { return wanderingTraderSpawnChance_; }
    std::string difficulty() const { return difficulty_; }
    std::string difficultyPublic() const { return difficulty_; }
    bool isSpawnProtected(std::int32_t x, std::int32_t z) const {
        if (spawnProtection_ <= 0) return false;
        if (ops_.empty()) return false;
        auto sp = world_.spawnPoint();
        int dx = std::abs(x - sp.x);
        int dz = std::abs(z - sp.z);
        return std::max(dx, dz) <= spawnProtection_;
    }
    bool isOp(const std::string& name) const { return ops_.count(name) > 0; }
    void loadOps();
    void saveOps() const;
    bool isBanned(const std::string& name) const { return bannedPlayers_.count(name) > 0; }
    bool isIpBanned(const std::string& ip) const { return bannedIps_.count(ip) > 0; }
    void loadBans();
    void saveBans() const;
    void loadBannedIps();
    void saveBannedIps() const;
    void saveWhitelist() const;
    const std::unordered_set<std::string>& bannedPlayers() const { return bannedPlayers_; }
    const std::unordered_set<std::string>& bannedIps() const { return bannedIps_; }
    std::unordered_set<std::string>& bannedPlayersMut() { return bannedPlayers_; }
    std::unordered_set<std::string>& bannedIpsMut() { return bannedIps_; }
    void kickPlayer(const std::string& name, const std::string& reason);
    void sendWorldBorderTo(Player& p) const;
    void broadcastWorldBorder();
    // Plan7 BossBar
    BossAIManager* bossAI() { return bossAI_.get(); }
    const BossAIManager* bossAI() const { return bossAI_.get(); }
    BossBarManager* bossBars() { return bossAI_ ? &bossAI_->bars() : nullptr; }
private:
    std::unique_ptr<LightEngine> lightEngine_;
    std::unique_ptr<FluidSim> fluidSim_;
    std::unique_ptr<RedstoneEngine> redstone_;
    std::unique_ptr<BlockTickScheduler> blockTicks_;
    // Plan8 modular managers (thin wrappers, keep GameServer as facade)
    // WorldManager/EntityManager/InventoryController/NetworkManager already header-only;
    // HungerManager/CombatManager are real classes with .cpp
    std::unique_ptr<WorldManager> worldMgr_;
    std::unique_ptr<EntityManager> entityMgr_;
    std::unique_ptr<NetworkManager> networkMgr_;
    // Plan7 BossAI / BossBar
    std::unique_ptr<BossAIManager> bossAI_;
    // Block Event Bus (plan6): simple vector of handlers in GameServer
    std::vector<std::function<void(std::int32_t,std::int32_t,std::int32_t,std::uint16_t,std::uint16_t)>> onBlockPlaceHandlers_;
    std::vector<std::function<void(std::int32_t,std::int32_t,std::int32_t,std::uint16_t,std::uint16_t)>> onBlockBreakHandlers_;
    std::vector<std::function<void(std::int32_t,std::int32_t,std::int32_t,std::uint16_t)>> onBlockNeighborChangeHandlers_;
public:
    void addOnBlockPlaceHandler(std::function<void(std::int32_t,std::int32_t,std::int32_t,std::uint16_t,std::uint16_t)> h) { onBlockPlaceHandlers_.push_back(std::move(h)); }
    void addOnBlockBreakHandler(std::function<void(std::int32_t,std::int32_t,std::int32_t,std::uint16_t,std::uint16_t)> h) { onBlockBreakHandlers_.push_back(std::move(h)); }
    void addOnBlockNeighborChangeHandler(std::function<void(std::int32_t,std::int32_t,std::int32_t,std::uint16_t)> h) { onBlockNeighborChangeHandlers_.push_back(std::move(h)); }
    void fireBlockPlaceEvent(std::int32_t x,std::int32_t y,std::int32_t z,std::uint16_t oldSt,std::uint16_t newSt) { for(auto& h: onBlockPlaceHandlers_) h(x,y,z,oldSt,newSt); }
    void fireBlockBreakEvent(std::int32_t x,std::int32_t y,std::int32_t z,std::uint16_t oldSt,std::uint16_t newSt) { for(auto& h: onBlockBreakHandlers_) h(x,y,z,oldSt,newSt); }
    void fireBlockNeighborChangeEvent(std::int32_t x,std::int32_t y,std::int32_t z,std::uint16_t ns) { for(auto& h: onBlockNeighborChangeHandlers_) h(x,y,z,ns); }
    // alias spec names
    void onBlockPlace(std::int32_t x,std::int32_t y,std::int32_t z,std::uint16_t o,std::uint16_t n){ fireBlockPlaceEvent(x,y,z,o,n); }
    void onBlockBreak(std::int32_t x,std::int32_t y,std::int32_t z,std::uint16_t o,std::uint16_t n){ fireBlockBreakEvent(x,y,z,o,n); }
    void onBlockNeighborChange(std::int32_t x,std::int32_t y,std::int32_t z,std::uint16_t ns){ fireBlockNeighborChangeEvent(x,y,z,ns); }
private:
    const std::uint64_t explosionSeed_ = 0x51AB1EULL;
    // weather (本家互換: doWeatherCycle / rain)
    enum class Weather { Clear, Rain } weather_ = Weather::Clear;
    std::int64_t weatherUntilTick_ = 6000 * 20;   // next toggle attempt
public:
    bool raining() const { return weather_ == Weather::Rain; }
    bool thundering() const { return weather_ == Weather::Rain && (tickNo_ % 6000) < 500; } // approximate thunder window
private:
    void weatherTick();
    void setWeather(Weather w, std::int64_t durationTicks);
public:
    void forceWeatherClear() { setWeather(Weather::Clear, 6000 * 20); }
    // plan14 §6: datapack and function evaluator (schedule tick)
    DatapackManager datapackManager_;
    FunctionEvaluator functionEvaluator_;
    DatapackManager& datapackManager() { return datapackManager_; }
    const DatapackManager& datapackManager() const { return datapackManager_; }
    FunctionEvaluator& functionEvaluator() { return functionEvaluator_; }
    const FunctionEvaluator& functionEvaluator() const { return functionEvaluator_; }
    // plan14 §6: called each tick from tickOnce to run due scheduled functions
    void tickScheduledFunctions() { functionEvaluator_.tick(tickNo_); }
private:
    struct CachedChunk { std::uint64_t rev; ChunkBodyRef body; };
    std::unordered_map<std::int64_t, CachedChunk> chunkCache_;
    std::mutex chunkCacheMtx_;
    std::unordered_map<std::int32_t, std::int64_t> ghostThrottle_; // entityId -> last tick for PlaceGhostRecipe 0x39
    // W19 async I/O: ThreadPool for RegionFile zlib offload (Yarn ThreadedAnvilChunkStorage)
    core::ThreadPool ioPool_{4};
    // pending async chunk loads (ChunkPos -> future) polled in tickOnce via pollPendingLoads()
    // kept simple: pending map polled with wait_for(0) to avoid blocking main tick (MC-177729)
    std::unordered_map<std::int64_t, std::future<std::vector<std::uint8_t>>> pendingLoads_;
    void pollPendingLoads() { // W19: poll ready futures (no-op if none)
        for (auto it = pendingLoads_.begin(); it != pendingLoads_.end(); ) {
            if (it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try { (void)it->second.get(); } catch (...) {}
                it = pendingLoads_.erase(it);
            } else ++it;
        }
    }
    std::atomic<bool> running_{true};
    int listenFd_ = -1;
    std::int32_t entityIdCounter_ = 1;
};

} // namespace cppfm
