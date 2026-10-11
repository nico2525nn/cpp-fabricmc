// GameServer: protocol state machine (HANDSHAKE→STATUS/LOGIN→CONFIGURATION→PLAY), player registry, world interaction, broadcasting.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>
#include <array>
#include <deque>
#include <future>
#include <functional>
#include "../net/Connection.hpp"
#include "../net/RateLimiter.hpp"   // plan46 §1: SpamTracker (O-13 A3)
#include "../proto/Ids.hpp"
#include "World.hpp"
#include "ChunkCodec.hpp"
#include "ChunkBodyCache.hpp"
#include "ChunkSaveCoordinator.hpp"
#include "ChunkNbtProjection.hpp"
#include "PlayerLoginHandoff.hpp"
#include "PlayerTrackingSequencer.hpp"
#include "PacketOrder.hpp"
#include "EmbeddedData.hpp"
#include "Persistence.hpp"
#include "Entities.hpp"
#include "../net/Rcon.hpp"
#include "ServerConfig.hpp"
#include "../net/Crypto.hpp"
#include "../net/MojangAuth.hpp"
#include "Items.hpp"
#include "ConsumableUseState.hpp"
#include "Constants.hpp"
#include "Containers.hpp"
#include "Recipes.hpp"
#include "TagManager.hpp"
#include "LootTables.hpp"
#include "BlockEntities.hpp"
#include "BlockEntityKinds.hpp"
#include "BlockContainerSlot.hpp"
#include "GameData.hpp"
#include "Xp.hpp"
#include "MobEffects.hpp"
#include "Stats.hpp"
#include "Scoreboard.hpp"
#include "Teams.hpp"
#include "../core/Random.hpp"
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
#include "SessionLock.hpp"
#include "BossAI.hpp"
#include "MenuLogic.hpp"
#include "HungerManager.hpp"
#include "CombatManager.hpp"
#include "DatapackManager.hpp"
#include "FunctionEvaluator.hpp"
#include "../core/ThreadPool.hpp"
#include "../platform/Socket.hpp"
#include "../jvm/JvmRuntime.hpp"
#include <list>

namespace cppfm {

// The simulation gate is recursive because command/event callbacks can re-enter
// the server.  Connection::sendFramed uses the same thread-local ownership
// state to release the gate only while a socket write may block.

class SimulationDispatchGuard {
public:
    explicit SimulationDispatchGuard(std::recursive_mutex& mutex)
        : lock_(mutex) {
        (void)mutex;
        ++activeSimulationDispatchDepth;
    }
    SimulationDispatchGuard(const SimulationDispatchGuard&) = delete;
    SimulationDispatchGuard& operator=(const SimulationDispatchGuard&) = delete;

    void unlock() {
        if (!lock_.owns_lock()) return;
        --activeSimulationDispatchDepth;
        lock_.unlock();
    }
    void lock() {
        if (lock_.owns_lock()) return;
        lock_.lock();
        ++activeSimulationDispatchDepth;
    }
    bool owns_lock() const noexcept { return lock_.owns_lock(); }

    ~SimulationDispatchGuard() {
        if (lock_.owns_lock()) {
            --activeSimulationDispatchDepth;
            lock_.unlock();
        }
    }

private:
    std::unique_lock<std::recursive_mutex> lock_;
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
    std::int32_t maxUses = 12;
    std::int32_t xp = 2;
    float priceMultiplier = 0.05f;
    std::int32_t demand = 0;
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
    // All teleports share one per-player confirmation sequence. The serial
    // mutex keeps packet enqueue order aligned with the latest pending id.
    mutable std::mutex teleportMtx;
    std::int32_t lastTeleportId = 1;
    std::optional<std::int32_t> pendingTeleportId;
    bool inPlay = false;           // finished onEnterPlay: eligible for broadcasts
    // Whether SpawnEntity has been published for this tracked player. Kept
    // distinct from inPlay so a just-admitted or transitioning player cannot
    // leak into another joiner's entity snapshot before its ordered spawn.
    bool trackingSpawnPublished = false;
    // survival state
    std::uint8_t gamemode = 0;     // 0 survival 1 creative 2 adventure 3 spectator
    std::uint8_t previousGamemode = 0;
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
    // Session handlers and the authoritative tick can both touch inventory,
    // XP, and related player state.  Keep this lock recursive for the small
    // model-only helper calls that can nest while a menu operation holds it.
    mutable std::recursive_mutex stateMtx;
    // ContainerSetContent/Slot and cursor packets carry the same optimistic
    // inventory revision.  Serialize their snapshot-and-send sequence so a
    // slower older snapshot cannot reach the client after a newer one.
    mutable std::mutex inventoryPacketMtx;
    // Serialize equipment projections independently from inventory content.
    // The cache is the last enqueued wire snapshot; unchanged inventories and
    // menu resyncs must not fan out duplicate equipment packets.
    mutable std::mutex equipmentPacketMtx;
    mutable std::vector<std::uint8_t> lastEquipmentPacket;
    mutable std::int8_t lastEquipmentPacketDimension = 0;
    mutable bool hasLastEquipmentPacket = false;
    std::array<InvSlot, kPlayerInventorySlotCount> inv{};
    std::array<InvSlot, 27> enderItems{}; // B-14 EnderItems 27 (vanilla EnderChest)
    std::int32_t invStateId = 1;
    bool dead = false;
    struct LoginProp { std::string name, value, signature; };
    std::vector<LoginProp> loginProps;
    // survival dig tracking
    // The connection/session thread and the authoritative tick thread both
    // transition this state. digMtx protects the fields; digTransitionMtx
    // serializes session-side dig actions with the tick's authoritative block
    // break and its packets. When both are needed, acquire digTransitionMtx
    // before digMtx.
    mutable std::mutex digTransitionMtx;
    mutable std::mutex digMtx;
    bool digActive = false;
    std::int32_t digX=0, digY=0, digZ=0;
    std::int64_t digStartTick = 0;
    std::int32_t digTotalTicks = 0;
    std::uint8_t digLastStage = 255;
    std::uint64_t digRevision = 0;
    double sentX = 0, sentY = 0, sentZ = 0;   // last broadcast to others
    float  sentYaw = 0, sentPitch = 0;
    XpState xp{};
    // stats + advancements
    std::unique_ptr<StatsManager> stats;
    std::unique_ptr<AdvancementManager> advancements;
    std::int64_t joinTick = 0;
    std::unordered_set<std::string> enabledTriggerObjectives;
    std::vector<EffectInstance> effects;
    bool hasChatSession = false;
    bool onlineAuthenticated = false;
    std::vector<std::vector<std::uint8_t>> mojangProfileKeys;
    std::vector<std::uint8_t> chatPubKey;
    std::vector<std::uint8_t> chatSessionSignature;
    std::array<std::uint8_t, 16> chatSessionId{};
    std::int32_t chatMessageIndex = 0;
    std::int64_t chatSessionExpiry = 0;
    std::vector<std::int64_t> lastSeenChatSalts;
    std::unordered_set<std::string> scoreboardTags;
    static constexpr std::size_t kMaxScoreboardTags = 1024;

    bool addScoreboardTag(std::string tag) {
        std::lock_guard playerLock(stateMtx);
        if (scoreboardTags.contains(tag) ||
            scoreboardTags.size() >= kMaxScoreboardTags)
            return false;
        return scoreboardTags.insert(std::move(tag)).second;
    }

    std::int8_t dimension = 0;
    std::int64_t portalCooldownUntilTick = -100000;
    // sleeping state (bed)
    bool sleeping = false;
    std::int32_t bedX=0, bedY=0, bedZ=0;
    // Per-player respawn point.  This is deliberately separate from the
    // transient sleeping-bed coordinates: /spawnpoint and a bed survive a
    // reconnect, while sleeping only describes the current animation.
    bool hasRespawnPoint = false;
    std::int32_t respawnX = 0, respawnY = -60, respawnZ = 0;
    std::int8_t respawnDimension = 0;
    float respawnAngle = 0.f;
    // client-declared plugin channels
    std::unordered_set<std::string> clientChannels;
    std::shared_ptr<Connection> conn;
    // PVP knockback / invuln (items 42)
    std::int32_t hurtCooldown = 0;
    // shield blocking ticks (5t to activate) + axe disable timer (100t)
    std::int32_t attackCooldownTicks = 20; // start fully charged
    std::int32_t blockingTicks = 0;
    std::int32_t shieldDisableTicks = 0;
    bool isBlocking = false; // set while holding UseItem with shield (see onUseItem)
    std::int32_t vehicleId = -1; // riding
    std::int32_t arrowsStuck = 0;
    std::int64_t lastEnderPearlTick = -10000;
    std::int64_t invulnUntilTick = 0;
    std::int32_t enchantmentSeed = 0; // plan17 LOW I5: seeded enchanting RNG (Yarn EnchantmentScreenHandler seed)
    bool isFlying = false;
    std::int32_t flyingTicks = 0;
    // Completed after this session's ordered retirement path; replacements
    // wait on the shared future rather than retaining a Session pointer.
    std::shared_future<bool> cleanupCompletion;
    AttributeManager attributes;
    // per-player; viewDistance narrows the chunk send radius (min with server vd).
    struct ClientSettings {
        std::string locale = "en_us";
        std::int32_t viewDistance = 6;   // clamped 2..32 (i8 on the wire)
        std::int32_t chatFlags = 0;
        bool chatColors = true;
        std::uint8_t skinParts = 0x7F;
        std::int32_t mainHand = 1;
        bool textFiltering = false;
        bool serverListing = true;
        std::int32_t particleStatus = 0; // 0 all / 1 decreased / 2 minimal
    } clientSettings;
    bool boatLeftPaddle = false, boatRightPaddle = false;
    // unlock distribution stays UpdateRecipes/RecipeBookAdd).
    std::int32_t recipeBookId = 0;
    bool recipeBookOpen = false, recipeBookFilter = false;
    std::int32_t displayedRecipe = -1;
    std::int32_t advancementTabAction = -1;
    std::string advancementTabId;
    bool difficultyLocked = false;
    std::int32_t lastPlayPongId = 0;
    std::int64_t lastPlayPongMs = 0;
    std::array<std::uint8_t,16> spectateTarget{};
    bool hasSpectateTarget = false;
    std::optional<std::int32_t> beaconPrimary, beaconSecondary;
    struct BookEdit {
        std::int32_t hand = 0;
        std::vector<std::string> pages;
        std::string title;
        bool signedCopy = false;
    };
    BookEdit lastBookEdit;
    std::unordered_set<std::string> combatRecipeUnlocks;
};

// Vanilla face ids: bottom(-Y), top(+Y), north(-Z), south(+Z), west(-X), east(+X).
inline constexpr std::array<int, 6> kBlockFaceOffsetX{0, 0, 0, 0, -1, 1};
inline constexpr std::array<int, 6> kBlockFaceOffsetY{-1, 1, 0, 0, 0, 0};
inline constexpr std::array<int, 6> kBlockFaceOffsetZ{0, 0, -1, 1, 0, 0};

struct BlockPos {
    std::int32_t x=0, y=0, z=0;
    BlockPos offset(int face) const {
        int d = (face >= 0 && face < 6) ? face : 0;
        return {x + kBlockFaceOffsetX[d], y + kBlockFaceOffsetY[d], z + kBlockFaceOffsetZ[d]};
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
    Session(GameServer& srv, std::shared_ptr<Connection> conn,
            std::function<bool()> statusAdmission = {},
            std::function<void()> releaseAdmission = {})
        : srv_(srv), conn_(std::move(conn)),
          statusAdmission_(std::move(statusAdmission)),
          releaseAdmission_(std::move(releaseAdmission)) {}
    ~Session();

    void run();
    GameServer& server() { return srv_; }
    std::int8_t dimension() const { return self_->dimension; }

private:
    friend class GameServer;
    void handleHandshake(ReadBuffer& in);
    void answerLegacyPing();
    void handleStatus();
    void handleLogin();
    void handleConfiguration();
    void handlePlay();
    // loops (packs + finish-ack) share this helper. Only unknown ids throw.
    enum class ConfigWaitResult { Continue, FinishAck, PacksDone, Abort };
    ConfigWaitResult handleOneConfigPacket(ReadBuffer& in);
    void onEnterPlay(SimulationDispatchGuard& simulationLock);
    void tickChunksAround(double px, double pz,
                          SimulationDispatchGuard& simulationLock);
    void sendNextChunkBatch(std::int8_t dimension,
                            SimulationDispatchGuard& simulationLock);

    // play-phase handlers
    void onChatMessage(ReadBuffer& in);
    void onPlayerAction(ReadBuffer& in);
    void onUseItemOn(ReadBuffer& in);
    struct UseItemOnRequest;
    bool handleUseItemOnInteractions(const UseItemOnRequest& request);
    bool handleUseItemOnToolActions(const UseItemOnRequest& request,
                                    const InvSlot& heldItem);
    bool handleUseItemOnDoorAndSlab(const UseItemOnRequest& request,
                                    const InvSlot& heldItem);
    bool handleUseItemOnOccupied(const UseItemOnRequest& request,
                                 const InvSlot& heldItem);
    bool handleUseItemOnEntityItems(const UseItemOnRequest& request,
                                    const InvSlot& heldItem);
    void placeUseItemOnBlock(const UseItemOnRequest& request,
                             const InvSlot& heldItem);
    void onUseItem(ReadBuffer& in);
    bool useBundleItem(std::size_t inventorySlot, std::int32_t sequence);
    bool useTridentItem(ItemStack& stack, std::int32_t sequence);
    bool useBowItem(ItemStack& stack, std::int32_t sequence);
    bool useCrossbowItem(ItemStack& stack, std::int32_t sequence);
    bool useThrowableItem(ItemStack& stack, std::int32_t sequence);
    bool tryStartConsumableUse(std::size_t inventorySlot,
                               ItemStack& stack);
    void publishHandUseMetadata(bool active, bool offhand);
    void cancelConsumableUse();
    void stopAllItemUse();
    void finishConsumableUse();
    void completeConsumableUse(ItemStack& stack);
    void tickConsumableUse();
    void onHeldSlot(ReadBuffer& in);
    // Play-packet bodies extracted from handlePlay (cleanup P3(d)).
    void onAcceptTeleportation(ReadBuffer& in,
                               SimulationDispatchGuard& simulationLock);
    void onKeepAlivePacket(ReadBuffer& in);
    bool onChatCommandSignedPacket(ReadBuffer& in);
    void onChatSessionUpdate(ReadBuffer& in);
    void onCustomPayload(ReadBuffer& in);
    void onPlaceRecipePacket(ReadBuffer& in);
    void onSelectTrade(ReadBuffer& in);
    void onPingRequest(ReadBuffer& in);
    void onPlayerAbilities(ReadBuffer& in);
    void onSetCreativeModeSlot(ReadBuffer& in);
    void onClientCommand(ReadBuffer& in);
    void onPlayerInput(ReadBuffer& in);
    void onMoveVehicle(ReadBuffer& in);
    void onSignUpdate(ReadBuffer& in);
    void onEntityAction(ReadBuffer& in);
    void onClientSettings(ReadBuffer& in);
    void onNameItemPacket(ReadBuffer& in);
    void onBeaconEffectPacket(ReadBuffer& in);
    void onPickItemFromBlock(ReadBuffer& in);
    void onPickItemFromEntity(ReadBuffer& in);
    void onRecipeBookPacket(ReadBuffer& in);
    void onDisplayedRecipe(ReadBuffer& in);
    void onSteerBoat(ReadBuffer& in);
    bool onResourcePackReceive(ReadBuffer& in);
    void onPong(ReadBuffer& in);
    void onAdvancementTab(ReadBuffer& in);
    void onSelectBundleItem(ReadBuffer& in);
    void onSetSlotState(ReadBuffer& in);
    void onDebugSampleSubscription(ReadBuffer& in);
    void onQueryBlockEntityTag(ReadBuffer& in);
    void onQueryEntityNbt(ReadBuffer& in);
    void onLockDifficulty(ReadBuffer& in);
    void onEditBook(ReadBuffer& in);
    void onGenerateStructure(ReadBuffer& in);
    void onUpdateCommandBlock(ReadBuffer& in);
    void onUpdateCommandBlockMinecart(ReadBuffer& in);
    void onUpdateJigsaw(ReadBuffer& in);
    void onUpdateStructureBlock(ReadBuffer& in);
    void onSpectatePacket(ReadBuffer& in);
    void onPlayerLoadedPacket(SimulationDispatchGuard& simulationLock);
    void onUseEntity(ReadBuffer& in);
    struct EntityAttackPlan;
    struct EntityAttackMobResult;
    void handleEntityAttack(std::int32_t target);
    EntityAttackPlan prepareEntityAttackPlan();
    bool handlePlayerEntityAttack(
        std::int32_t target, const EntityAttackPlan& plan);
    EntityAttackMobResult hitMobTarget(
        std::int32_t target, const EntityAttackPlan& plan);
    void applyMobSweep(const EntityAttackPlan& plan,
                       const EntityAttackMobResult& result);
    void applyMobKnockback(const EntityAttackPlan& plan,
                           const EntityAttackMobResult& result);
    void finishMobKill(std::int32_t target, const EntityAttackPlan& plan,
                       const EntityAttackMobResult& result);
    void handleMobEntityAttack(std::int32_t target,
                               const EntityAttackPlan& plan);
    void handleRespawnRequest();
    void sendDeclareCommands();
    void onChatCommand(ReadBuffer& in);
    void dispatchCommand(const std::string& line);
    void broadcastMovement();
    void broadcastSpawnEntity(Player* about);
    void broadcastSpawnEntityUnderTrackingSequencer(Player* about);
    void publishPlayerTrackingAfterDimensionTransfer();
    void synchronizeEquipmentForTrackedPlayers();
    void onMovement(ReadBuffer& in, bool hasPos, bool hasRot,
                    SimulationDispatchGuard& simulationLock);
    void updateMovementPhysics(double oldX, double oldY, double oldZ,
                               bool wasOnGround, bool nowGround,
                               bool hasPosition);
    void resolveMovementLanding();
    bool isFallMitigated() const;
    void trampleFarmlandOnLanding();
    void freezeWaterWithFrostWalker();
    void updateGroundMovementEnchantments();
    void handleMovementPortal(SimulationDispatchGuard& simulationLock);
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
    void kickPlay(const char* jsonReason);
    void sendSystemText(const std::string& text);
    void sendJoinGame();
    void sendTeleport(double x, double y, double z, float yaw, float pitch);
    bool sendChunk(std::int8_t dimension, std::int32_t cx,
                   std::int32_t cz,
                   SimulationDispatchGuard& simulationLock);
    void streamInitialChunks(SimulationDispatchGuard& simulationLock);
    void sendPlayerInfoAddSelf();
    void broadcastPlayerInfoAdd(Player* about);
    void sendInitialInventory();
    void sendAbilities();
    void sendSignBlockEntity(std::int8_t dimension, std::int32_t x,
                             std::int32_t y, std::int32_t z,
                             bool broadcast = true);
    void ack(std::int32_t sequence);
    // container menus (chest/furnace/crafting)
    void onCloseContainer();
    void handleMenuClick(Menu& m, int slot, int button, int mode);
    void handlePlayerInventoryClick(int slot, int button, int mode);
    void sendMenuContent(Menu& m);
    void resyncOpenMenuAt(std::int8_t dimension, std::int64_t blockKey);
    bool menuItemsSerializable(Menu& m);
    void appendEnchantmentProperties(Menu& m,
                                     std::vector<WriteBuffer>& packets);
    void sendSetSlot(std::int32_t windowId, std::int32_t stateId,
                     std::int16_t slot, const ItemStack& s);
    void syncCursorItem();
    void openMenuAt(std::int32_t x, std::int32_t y, std::int32_t z,
                    std::uint16_t stateIdOfBlock);
    void closeOpenMenu(bool sendPacket);
    void onWindowClick(ReadBuffer& in);
    void onEnchantItem(ReadBuffer& in);
    void resyncOpenMenuForBlockChanges(
        std::int8_t dimension,
        const std::unordered_set<std::int64_t>& changedBlockKeys);
    void applyClientSettings(Player::ClientSettings s);
    bool requireOp(int level, const char* what);
    void sendTagQueryResponse(std::int32_t transactionId, const WriteBuffer& nbt);
    void answerBlockNbt(std::int32_t transactionId, std::int32_t x, std::int32_t y, std::int32_t z);
    void answerEntityNbt(std::int32_t transactionId, std::int32_t entityId);
    void onNameItem(const std::string& name);
    void onBeaconEffect(std::optional<std::int32_t> primary, std::optional<std::int32_t> secondary);
    void onSpectate(const std::array<std::uint8_t,16>& target);

    GameServer& srv_;
    std::shared_ptr<Connection> conn_;
    std::function<bool()> statusAdmission_;
    std::function<void()> releaseAdmission_;
    bool playerAdmissionReserved_ = false;
    bool preservePlayerDataOnDisconnect_ = false;
    std::promise<bool> cleanupCompletionPromise_;
    std::shared_future<bool> cleanupCompletionFuture_ =
        cleanupCompletionPromise_.get_future().share();
    enum class State { Handshake, Status, Login, Configuration, Play, Done };

    State state_ = State::Handshake;
    static constexpr std::size_t kDefaultChunkBatchSize = 8;
    static constexpr std::size_t kMaxChunkBatchSize = 32;
    std::shared_ptr<Player> self_ = std::make_shared<Player>();
    ConsumableUseState consumableUse_;
    std::optional<bool> activeUseOffhand_;
    bool registered_ = false;         // present in server registry
    bool chunksStreamed_ = false;
    std::int32_t lastCx_ = INT32_MAX, lastCz_ = INT32_MAX;
    std::optional<std::int8_t> chunkStreamDimension_;
    std::vector<std::pair<std::int32_t, std::int32_t>> pendingChunkStream_;
    std::size_t chunkBatchSize_ = kDefaultChunkBatchSize;
    bool chunkBatchAwaitingAck_ = false;
    double sentX_ = 0, sentY_ = 0, sentZ_ = 0;
    float sentYaw_ = 0, sentPitch_ = 0;
    bool hasSent_ = false;
    std::unordered_set<std::int64_t> sentChunks_;
    // open container menu (chest/furnace/crafting) when any
    // Keep an owning handle in every packet/extension path that continues
    // after releasing stateMtx.  Menu contains views into a block entity and
    // is replaced on re-entrant screen changes, so a raw pointer here would
    // make a concurrent close/reopen a use-after-free.
    std::shared_ptr<Menu> openMenu_;
    // Session-local adapter for the player's 46-slot inventory screen.  The
    // five crafting slots are copied to/from Player::inv around each click so
    // existing inventory snapshots remain authoritative for every caller.
    Menu playerInventoryMenu_;
    ItemStack cursorItem_;
    SpamTracker spam_;
    RateLimitedLog playLogGate_;
    std::int32_t menuWindowCounter_ = 0;
    std::int32_t villagerWindowSeq_ = 100;
    std::int32_t tradingVillager_ = -1;  // villager entity id while trading
    std::array<std::uint8_t, 16> resourcePackUuid_{};
    bool resourcePackRequired_ = false;
    bool resourcePackLoaded_ = false;
};

class GameServer {
    friend class Session;
    friend class HopperTickService;
    friend class jvm::JvmRuntime;
#ifdef CPPFM_WORLD_MUTATION_TEST
    friend struct WorldMutationContractTestAccess;
#endif

    struct DimensionRuntime {
        World world;
        std::unordered_map<std::int64_t, bool> dispenserPowerByPosition;
        // Rails touched by a minecart stay tracked until a later tick clears
        // their powered state after the last cart leaves.
        std::unordered_set<std::int64_t> poweredDetectorRails;
        BlockEntityStore blockEntities;
        std::unique_ptr<LightEngine> lightEngine;
        std::unique_ptr<FluidSim> fluidSim;
        std::unique_ptr<RedstoneEngine> redstone;
        std::unique_ptr<BlockTickScheduler> blockTicks;
        // Persistence owns callbacks into the world, block-entity store, and
        // simulation engines, so it must be destroyed before those targets.
        // Members are destroyed in reverse declaration order.
        std::unique_ptr<Persistence> persistence;

        DimensionRuntime(std::string biome, LevelType levelType,
                         std::uint64_t seed)
            : world(std::move(biome), levelType, seed),
              blockEntities(world.minY(), world.maxY()) {
            if (!blockEntities.bindMutationGate(world.mutationGateIdentity()))
                throw std::logic_error(
                    "failed to bind block entities to World mutation gate");
        }
    };

    // Canonical storage order is Overworld, Nether, End.  Minecraft dimension
    // ids remain 0, -1, 1 respectively; keep that mapping in one place rather
    // than retaining parallel arrays with different index conventions.
    static constexpr std::size_t dimensionRuntimeIndex(
        std::int8_t dimension) noexcept {
        switch (dimension) {
        case -1: return 1;
        case 1: return 2;
        default: return 0;
        }
    }
    DimensionRuntime& dimensionRuntime(std::int8_t dimension) {
        return *dimensionRuntimes_[dimensionRuntimeIndex(dimension)];
    }
    const DimensionRuntime& dimensionRuntime(std::int8_t dimension) const {
        return *dimensionRuntimes_[dimensionRuntimeIndex(dimension)];
    }
    Persistence* persistenceFor(std::int8_t dimension) {
        return dimensionRuntime(canonicalDimension(dimension)).persistence.get();
    }
    const Persistence* persistenceFor(std::int8_t dimension) const {
        return dimensionRuntime(canonicalDimension(dimension)).persistence.get();
    }
    static std::size_t pendingSessionWorkerLimit(std::int32_t maxPlayers) noexcept {
        constexpr std::size_t kMinimum = 64;
        constexpr std::size_t kLoginHeadroom = 32;
        const auto configured = maxPlayers > 0
            ? static_cast<std::size_t>(maxPlayers) : std::size_t{0};
        return std::max(kMinimum, configured + kLoginHeadroom);
    }
public:
    enum class Dim : std::int8_t { Overworld = 0, Nether = -1, End = 1 };

    explicit GameServer(ServerConfig cfg)
        : cfg_(cfg),
          dimensionRuntimes_{std::make_unique<DimensionRuntime>(
              cfg_.worldBiome,
              cfg.levelType == "normal" ? LevelType::Normal : LevelType::Flat,
              cfg.seed)},
          startTime_(cfg.startTime),
          pendingSessionAdmissionGate_(pendingSessionWorkerLimit(cfg.maxPlayers)),
          ioPool_(static_cast<std::size_t>(std::max(1, cfg_.ioWorkerThreads))) {
        difficulty_ = cfg_.difficulty;
        defaultGameMode_.store(cfg_.gameMode, std::memory_order_relaxed);
        dimensionRuntimes_[1] = std::make_unique<DimensionRuntime>(
            "minecraft:nether_wastes", LevelType::Nether,
            cfg.seed ^ 0x4E37ULL);
        dimensionRuntimes_[2] = std::make_unique<DimensionRuntime>(
            "minecraft:the_end", LevelType::End, cfg.seed ^ 0xE11DULL);
        dimensionRuntime(0).world.setDimensionId(0);
        dimensionRuntime(-1).world.setDimensionId(-1);
        dimensionRuntime(1).world.setDimensionId(1);
    }
    bool teleportPlayer(Player& player, double x, double y, double z,
                        std::optional<std::array<float, 2>> rotation = std::nullopt);
    World& worldFor(std::int8_t dim) {
        return dimensionRuntime(dim).world;
    }
    const World& worldFor(std::int8_t dim) const {
        return dimensionRuntime(dim).world;
    }
    std::int8_t commandDimension(
        const brigadier::CommandSource& source) const noexcept {
        if (source.dimensionOverride)
            return canonicalDimension(*source.dimensionOverride);
        const auto* player = static_cast<const Player*>(source.player);
        return player ? canonicalDimension(player->dimension) : 0;
    }
    World& worldForCommand(const brigadier::CommandSource& source) {
        return worldFor(commandDimension(source));
    }
    const World& worldForCommand(
        const brigadier::CommandSource& source) const {
        return worldFor(commandDimension(source));
    }
    // Bind selectors after all command context fields, including an optional
    // `/execute in` override, have been copied into the source.
    void bindCommandSelector(brigadier::CommandSource& source) {
        Player* player = static_cast<Player*>(source.player);
        const auto dimension = commandDimension(source);
        source.resolveSelector = [this, player, dimension](
            const std::string& raw, brigadier::SelectorResult& out) {
            out = resolveSelectorForDimension(raw, player, dimension);
        };
    }
    LightEngine& lightsFor(std::int8_t dim) {
        return *dimensionRuntime(canonicalDimension(dim)).lightEngine;
    }
    const LightEngine& lightsFor(std::int8_t dim) const {
        return *dimensionRuntime(canonicalDimension(dim)).lightEngine;
    }
    FluidSim& fluidsFor(std::int8_t dim) {
        return *dimensionRuntime(canonicalDimension(dim)).fluidSim;
    }
    const FluidSim& fluidsFor(std::int8_t dim) const {
        return *dimensionRuntime(canonicalDimension(dim)).fluidSim;
    }
    RedstoneEngine& redstoneFor(std::int8_t dim) {
        return *dimensionRuntime(canonicalDimension(dim)).redstone;
    }
    const RedstoneEngine& redstoneFor(std::int8_t dim) const {
        return *dimensionRuntime(canonicalDimension(dim)).redstone;
    }
    BlockTickScheduler& blockTicksFor(std::int8_t dim) {
        return *dimensionRuntime(canonicalDimension(dim)).blockTicks;
    }
    const BlockTickScheduler& blockTicksFor(std::int8_t dim) const {
        return *dimensionRuntime(canonicalDimension(dim)).blockTicks;
    }
    ~GameServer() { stop(); }

    void init();
    void runForever();
    void requestStop() noexcept {        // minimal path used by console signal handlers
        running_.store(false, std::memory_order_release);
        if (const auto fd = listenFd_.load(std::memory_order_acquire);
            platform::isValid(fd)) {
            platform::shutdownSocket(fd);   // wake acceptLoop
        }
    }
    void stop();
    bool shutdownPersistenceSucceeded() const noexcept {
        return shutdownPersistenceSucceeded_.load(std::memory_order_acquire);
    }
    Persistence& persistence() { return *dimensionRuntime(0).persistence; }
    bool savePlayerData(const std::string& uuidHex, Player& p);
    bool loadPlayerData(const std::string& uuidHex, Player& p,
                        bool* dataFilePresent = nullptr,
                        bool* unsupportedItemData = nullptr,
                        bool* unsafeExistingData = nullptr);
    void invalidateRespawnPointsAt(std::int8_t dimension,
                                   std::int32_t x, std::int32_t y,
                                   std::int32_t z);
    void saveLevelData();
    void loadLevelData();
    auto& mobsForTest() { return mobs_; }
    void addMob(const std::shared_ptr<MobEntity>& mob) {
        if (!mob) return;
        std::lock_guard lk(entsMtx_);
        mobs_.push_back(mob);
    }
    bool removeMob(const std::shared_ptr<MobEntity>& mob) {
        if (!mob) return false;
        std::lock_guard lk(entsMtx_);
        const auto oldSize = mobs_.size();
        mobs_.erase(std::remove_if(mobs_.begin(), mobs_.end(),
                                   [&](const auto& candidate) {
                                       return candidate == mob;
                                   }),
                    mobs_.end());
        return mobs_.size() != oldSize;
    }
    Whitelist& whitelist() { return whitelist_; }
    BlockEntityStore& blockEntities() { return dimensionRuntime(0).blockEntities; }
    BlockEntityStore& blockEntitiesFor(std::int8_t dimension) {
        return dimensionRuntime(canonicalDimension(dimension)).blockEntities;
    }
    const BlockEntityStore& blockEntitiesFor(std::int8_t dimension) const {
        return dimensionRuntime(canonicalDimension(dimension)).blockEntities;
    }
    bool chunkFitsAnvilEntry(std::int8_t dimension, std::int32_t cx,
                             std::int32_t cz);
    bool blockEntityCandidatesFitAnvilEntry(
        std::int8_t dimension,
        const std::vector<BlockEntityStore::SnapshotOverride>& candidates);
    bool blockMutationCandidatesFitAnvilEntries(
        std::int8_t dimension,
        const std::vector<World::BlockUpdate>& blockUpdates,
        const std::vector<BlockEntityStore::NbtReplacement>& replacements);
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
    void initChatCommands();
    void initAdminCommands();
    void initWorldCommands();
    void initPlayerCommands();
    void initDataCommands();
    void initMiscCommands();
    void initExecuteCommands();
    void initScoreboardCommands();
    api::ServerEvents& events() { return api::events(); }
    LightEngine& lights() { return *dimensionRuntime(0).lightEngine; }
    GameRuleManager& gameRules() { return gamerules_; }
    const GameRuleManager& gameRules() const { return gamerules_; }
    BlockTickScheduler* blockTicks() { return dimensionRuntime(0).blockTicks.get(); }
    // Resolve a selector string (@a/@e/@p/...) against players & mobs.
    brigadier::SelectorResult resolveSelector(const std::string& raw,
                                              Player* source);
    brigadier::SelectorResult resolveSelectorForDimension(
        const std::string& raw, Player* source, std::int8_t dimension);
    // Spawn a mob by "minecraft:zombie"-style name at position.
    bool spawnMobByTypeName(const std::string& name, double x, double y, double z);
    bool spawnMobByTypeNameFor(std::int8_t dimension, const std::string& name,
                               double x, double y, double z);
    bool trySpawnEgg(Player& p, ItemStack& stack, BlockPos hitPos, int face);
    // Furnace smelting tick (called once per game tick).
    void furnacesTick();
    void furnacesTickFor(std::int8_t dimension);
    void brewingTick();
    void brewingTickFor(std::int8_t dimension);
    // Hopper item movement + dispenser ejection (every HOPPER_TRANSFER_INTERVAL_TICKS).
    static constexpr int HOPPER_TRANSFER_INTERVAL_TICKS = 8;
    void hoppersTick();
    void hoppersTickFor(std::int8_t dimension);
    // Crafter redstone edge/crafting/output simulation (every game tick).
    void craftersTick();
    void craftersTickFor(std::int8_t dimension);
    bool isChunkInSimulationDistanceFor(std::int8_t dimension,
                                        std::int32_t cx,
                                        std::int32_t cz) const;
    bool isChunkInSimulationDistance(std::int32_t cx, std::int32_t cz) const;
    void chunksUnloadTick();
    // Send the experience bar + level to one player.
    static void sendSetExperience(Player& p);
    // Apply / expire status effects for all living things (per tick).
    void effectsTick();
    // Console command dispatch (shared by chat /commands and RCON)
    std::string dispatchConsole(const std::string& line);
    inline static thread_local std::string* consoleCapture_ = nullptr;

    // ticking & entities (Phase 3/4)
    void startTickLoop();
    void stopTickLoop();
    void tickOnce();
    void survivalTick();
    void mobsTick();
    void spawnSlimeSplit(MobEntity& m); // slime/magma-cube death split (was 3x copy-paste)
    void drainPendingStructureQueues(); // plan36 §5: drain StructureManager pending loot/mobs tick
    bool flushPendingStructureLootForShutdown() noexcept;
    bool applyDamageToMob(MobEntity& m, float amount, const char* cause);
    bool applyDamageToMob(MobEntity& m, float amount, const DamageSource& src, int breachLv = 0);
    void growResinNearHeart(int hx,int hy,int hz);
    void growResinNearHeartFor(std::int8_t dimension, int hx, int hy, int hz);
    // Spawn a mob of `kind` at position and broadcast it.
    void spawnMob(MobKind kind, double x, double y, double z);
    void spawnMobFor(std::int8_t dimension, MobKind kind,
                     double x, double y, double z);
    void broadcastMobSpawn(const MobEntity& mob);   // no locking inside
    // Melee hit from a mob onto a player target (uses stats table).
    void mobAttackPlayer(MobEntity& m, Player& target);
    // Feed-to-breed handling when a player right-clicks an animal with food.
    bool tryBreedFeed(Player& p, MobEntity& m, std::size_t inventorySlot);
    void sendEquipment(const MobEntity& mob);
    void sendEquipmentSlot(const MobEntity& mob, int slot);
    void broadcastPlayerEquipment(const Player& p, bool force = false);
    void sendPlayerEquipmentTo(const Player& subject, const Player& viewer);
    void syncEquipmentOnChange(Player& p); // helper for armor/hand changes
    void broadcastSetPassengers(std::int32_t vehicleId);
    void broadcastSetPassengersEmptyFor(std::int8_t dimension,
                                        std::int32_t vehicleId);
    void handleMoveVehicle(Player& p, double x, double y, double z, float yaw, float pitch);
    void handleHorseJump(Player& p, int power); // plan13 §3 horse jump
    // XP orbs (経験値システム)
    void spawnXpOrbs(double x, double y, double z, int totalPoints,
                     Player* directTo);
    void spawnXpOrbsFor(std::int8_t dimension, double x, double y, double z,
                        int totalPoints, Player* directTo);
    void xpOrbsTick();
    std::shared_ptr<ProjectileEntity> spawnProjectile(ProjectileKind kind, double x, double y, double z,
                         double vx, double vy, double vz,
                         std::int32_t ownerId, bool ownerIsPlayer, bool charged = false);
    std::shared_ptr<ProjectileEntity> spawnProjectileFor(
        std::int8_t dimension, ProjectileKind kind, double x, double y,
        double z, double vx, double vy, double vz, std::int32_t ownerId,
        bool ownerIsPlayer, bool charged = false);
    void projectilesTick();
    void minecartsTick();
    void boatsTick();
    static const std::vector<struct TradeOffer>& tradeTable();
    bool openTrading(Player& p, MobEntity& villager);
    bool selectTrade(Player& p, std::int32_t index,
                     std::int32_t villagerEntityId = -1);
    // Progress tracking (stats + advancements)
    void initPlayerProgress(Player& p);
    bool savePlayerProgress(Player& p);
    void grantAdvancement(Player& p, const std::string& id);
    void sendAdvancementsTo(Player& p, bool reset);
    void onBlockMined(Player& p, std::uint16_t oldState);
    void onItemObtained(Player& p, const ItemStack& s, const char* how);
    void onMobKilledBy(Player& p, MobKind kind);
    void evaluateTickAdvancements(Player& p);
    void evaluateInventoryChanged(Player& p, const ItemStack& s);
    void evaluatePlayerKilledEntity(Player& p, MobKind kind);
    PredicateContext basePredicateContext(Player& p);
    void evaluateLocationTrigger(Player& p);
    void onPlacedBlock(Player& p, int x, int y, int z, std::uint16_t state);
    void onConsumeItem(Player& p, const ItemStack& stack);
    void onBredAnimals(Player* p);
    void onEnterBlock(Player* p, int x, int y, int z);
    void onItemUsedOnBlock(Player* p, int x, int y, int z, const ItemStack& item);
    void onEffectsChanged(Player* p);
    void onItemEnchanted(Player& p, const std::string& itemName, int levels);
    void onBucketFilled(Player& p, const std::string& filledName);
    void onVillagerTraded(Player& p, const std::string& soldId, int count);
    std::vector<AdvancementDefOwned> getMergedAdvancements();
 private:
    void publishPlayerEffectChanges(
        Player& player, std::int32_t entityId, std::int8_t dimension,
        const std::shared_ptr<Connection>& connection,
        const std::vector<std::uint8_t>& removed,
        const std::vector<EffectInstance>& updated,
        bool stateChanged);
    // Command registration is kept private and split by tree branch so each
    // registration unit remains reviewable without changing the public API.
    void initChatCommandsPart01();
    void initChatCommandsPart02();
    void initChatCommandsPart03();
    void initChatCommandsPart04();
    void initAdminCommandsPart01();
    void initAdminCommandsPart02();
    void initAdminCommandsPart03();
    void initAdminCommandsPart04();
    void initAdminCommandsPart05();
    void initAdminCommandsPart06();
    void initAdminCommandsPart07();
    void initAdminCommandsPart08();
    void initAdminCommandsPart09();
    void initAdminCommandsPart10();
    void initAdminCommandsPart11();
    void initMiscCommandsPart01();
    void initMiscCommandsPart02();
    void initMiscCommandsPart03();
    void initMiscCommandsPart04();
    void initMiscCommandsPart05();
    void initMiscCommandsPart06();
    void initMiscCommandsPart07();
    void initMiscCommandsPart08();
    void addTimeSetSubcommands(const brigadier::NodePtr& time);
    void addTimeQueryAndAddSubcommands(const brigadier::NodePtr& time);
    void initGameRuleCommand();
    void initForceLoadCommand();
    void initSetBlockCommand();
    void initWeatherCommand();
    void addWorldBorderSizeSubcommand(const brigadier::NodePtr& worldBorder);
    void addWorldBorderManagementSubcommands(
        const brigadier::NodePtr& worldBorder);
    void initDifficultyCommand();
    void initFillCommand();
    void initCloneCommand();
    void initLocateStructureCommand(const brigadier::NodePtr& locate);
    void initLocateBiomeCommand(const brigadier::NodePtr& locate);
    void initLocatePoiCommand(const brigadier::NodePtr& locate);
    void initPlaceCommand();
    void initSpreadPlayersCommand();
    void initEnchantCommand();
    void initSetWorldSpawnCommand();
    void initDefaultGameModeCommand();
    void initJigsawGenerateCommand();
    void initPlayerCommandsPart01();
    void initPlayerCommandsPart02();
    void initPlayerCommandsPart03();
    void initPlayerCommandsPart04();
    void initPlayerCommandsPart05();
    void initPlayerCommandsPart06();
    void initPlayerCommandsPart07();
    void initPlayerCommandsPart08();
    void initPlayerCommandsPart10();
    void initPlayerCommandsPart11();
    void initPlayerCommandsPart13();
    void initPlayerCommandsPart14();
    void initPlayerCommandsPart15();
    void initPlayerCommandsPart16();
    void initPlayerCommandsPart17();
    void initPlayerCommandsPart18();
    void initPlayerCommandsPart19();
    void initPlayerCommandsPart20();
    void initPlayerCommandsPart21();
    void initPlayerCommandsPart22();
    void initPlayerAttributeCommands();
    std::optional<Attribute> resolveAttributeCommand(const std::string& raw) const;
    std::pair<Player*, Attribute> attributeCommandHead(brigadier::CommandContext& c);
    void sendAttributeCommandUpdate(Player& p);
    void initAttributeGetCommands(const brigadier::NodePtr& attrArg);
    void initAttributeBaseCommands(const brigadier::NodePtr& attrArg);
    void initAttributeModifierCommands(const brigadier::NodePtr& attrArg);
    void initDataFunctionCommands();
    void initDataDatapackCommands();
    void initDataReloadCommands();
    void initDataScheduleCommands();
    void initDataReturnCommands();
    void initDataStorageCommands();
    void initDataGetCommands(const brigadier::NodePtr& data);
    void initDataModifyCommands(const brigadier::NodePtr& data);
    void initDataRemoveCommands(const brigadier::NodePtr& data);
    void initDataMergeCommands(const brigadier::NodePtr& data,
                               const brigadier::NodePtr& merge);
    void initDataLootCommands();
    void initDataAdvancementCommands();
    void initDataRecipeCommands();
    void initDataItemCommands();
    void initDataLootSourceCommands();
    void initDataItemReplaceCommands(
        const brigadier::NodePtr& replaceLit,
        const std::function<ItemStack*(Player&, const std::string&)>& slotToPlayerStack,
        const std::function<BlockContainerSlotAccess(
            std::int8_t, const brigadier::BlockPosI&, const std::string&)>& slotToBlockStack,
        const std::function<ItemStack(const std::string&, int)>& parseItemStack);
    void initDataItemModifyCommands(
        const brigadier::NodePtr& modifyLit,
        const std::function<BlockContainerSlotAccess(
            std::int8_t, const brigadier::BlockPosI&, const std::string&)>& slotToBlockStack);
    void initDataItemRemoveCommands(
        const brigadier::NodePtr& removeLit,
        const std::function<BlockContainerSlotAccess(
            std::int8_t, const brigadier::BlockPosI&, const std::string&)>& slotToBlockStack);
    void initExecuteRunCommands(const brigadier::NodePtr& exec);
    void initExecuteAsCommands(const brigadier::NodePtr& exec);
    void initExecuteAtCommands(const brigadier::NodePtr& exec);
    void initExecutePositionedCommands(const brigadier::NodePtr& exec);
    void initExecuteAnchoredCommands(const brigadier::NodePtr& exec);
    void initExecuteRotatedCommands(const brigadier::NodePtr& exec);
    void initExecuteFacingCommands(const brigadier::NodePtr& exec);
    void initExecuteInCommands(const brigadier::NodePtr& exec);
    void initExecuteAlignCommands(const brigadier::NodePtr& exec);
    void initExecuteConditionCommands(const brigadier::NodePtr& exec,
                                      const std::string& word, bool isUnless);
    void initExecuteStoreCommands(const brigadier::NodePtr& exec);
    void initScoreboardObjectiveCommands(const brigadier::NodePtr& obj);
    void initScoreboardPlayerCommands(const brigadier::NodePtr& players);
    void initScoreboardObjectiveRemoveCommands(const brigadier::NodePtr& obj);
    void initScoreboardCommandsPart2();
    void initScoreboardCommandsPart3();
    void initScoreboardCommandsPart4();
    mutable std::mutex advMergeMtx_;
    std::vector<AdvancementDefOwned> cachedMergedAdv_;
    size_t cachedAdvRawSize_ = 0;
public:
    // Explosion (creeper / TNT): destroys blocks & damages entities.
    void explodeAt(double x, double y, double z, float power);
    void explodeAtFor(std::int8_t dimension, double x, double y, double z,
                      float power);
    void spawnPrimedTnt(double x,double y,double z,double vx,double vy,double vz,int fuse=80);
    void spawnPrimedTntFor(std::int8_t dimension, double x, double y, double z,
                           double vx, double vy, double vz, int fuse=80);
    void tntTick();
    void strikeLightning(double x, double y, double z);
    void strikeLightningFor(std::int8_t dimension, double x, double y,
                            double z);
    // Direct-named sound + particle broadcast helpers.
    void broadcastSound(const char* name, double x, double y, double z,
                        float volume = 1.f, float pitch = 1.f,
                        const char* category = "block");
    void broadcastSoundFor(std::int8_t dimension, const char* name,
                           double x, double y, double z, float volume = 1.f,
                           float pitch = 1.f,
                           const char* category = "block",
                           std::optional<float> fixedRange = std::nullopt);
    enum class SoundSource : std::int32_t { Master=0, Music=1, Record=2, Weather=3, Block=4, Hostile=5, Neutral=6, Player=7, Ambient=8, Voice=9 };
    // D22 StopSound 0x71: flags i8 (1=source,2=sound), optional varint source, optional string sound
    void broadcastStopSound(const std::optional<SoundSource>& source,
                            const std::optional<std::string>& sound);
    void broadcastStopSound(SoundSource source, const std::string* soundOrNull);
    void broadcastStopSound(SoundSource source);
    void broadcastWorldEvent(std::int32_t eventId, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t data, bool disableRelativeVolume = false);
    void broadcastWorldEventFor(std::int8_t dimension, std::int32_t eventId,
                                std::int32_t x, std::int32_t y,
                                std::int32_t z, std::int32_t data,
                                bool disableRelativeVolume = false);
    void broadcastPaleOakLeavesParticle(double x, double y, double z); // D19 helper
    void broadcastPaleOakLeavesParticleFor(std::int8_t dimension,
                                           double x, double y, double z);
    void sendActionBar(Player& p, const std::string& text);
    void sendServerData(Player& p);
    void broadcastHurtAnimationFor(std::int8_t dimension,
                                   std::int32_t entityId, float yaw,
                                   Player* except = nullptr);
    void broadcastEntitySoundFor(std::int8_t dimension, std::int32_t entityId,
                                 const std::string& soundName,
                                 float volume = 1.f, float pitch = 1.f,
                                 SoundSource category = SoundSource::Neutral);
    void sendChatSuggestions(Player& p, std::int32_t action, const std::vector<std::string>& entries);
    void broadcastChatSuggestions(std::int32_t action, const std::vector<std::string>& entries, Player* except = nullptr);
    void sendSyncEntityPosition(Player& p, std::int32_t entityId, double x, double y, double z, double dx=0, double dy=0, double dz=0, float yaw=0, float pitch=0, bool onGround=true);
    void sendSyncEntityPosition(Player& p, const MobEntity& mob);
    void broadcastSyncEntityPosition(const MobEntity& mob, Player* except=nullptr);
    void sendMapData(Player& p, int mapId, uint8_t scale=2, bool locked=false);
    void broadcastMoveMinecart(std::int32_t entityId, double x, double y, double z, float yaw, float pitch, Player* except=nullptr);
    void sendSelectAdvancementTab(Player& p, const std::string& tabId);
    void itemsTick();
    void trySpawnMobs();
    static std::array<int,7> spawnGroupCaps() { return {70,10,15,5,20,5,5}; }
    // Hostile gate as implemented in trySpawnMobs: effLight<=7 at night/rain/ thunder, never on peaceful. (Vanilla uses light 0; the <=7
    // band is a documented simplification — see test_time_growth + SOAK_24H notes.)
    static bool hostileSpawnLightOk(double effLight, bool night, bool rain,
                                    bool thunder, const std::string& difficulty) {
        if (difficulty == "peaceful") return false;
        return effLight <= 7.0 && (night || rain || thunder);
    }
    void spawnItemDropFor(std::int8_t dimension, double x, double y, double z,
                          std::uint32_t itemId, std::uint8_t cnt,
                          double vx=0, double vy=0, double vz=0);
    void spawnItemDropFor(std::int8_t dimension, double x, double y, double z,
                          const ItemStack& stack, double vx=0, double vy=0,
                          double vz=0);
    void broadcastSpawnItem(const ItemEntity& it);
    void broadcastItemMetadata(const ItemEntity& it);
    bool addToInventory(Player& p, std::uint32_t itemId, std::uint16_t count);
    bool addStackToInventory(Player& p, const ItemStack& stack,
                             std::uint16_t count);
    void resendInventory(Player& p);
    void sendSetHealth(Player& p);

    void applyDamage(Player& p, float amount, const char* cause);
    void applyDamage(Player& p, float amount, const DamageSource& src, int breachLv = 0);
    void killPlayer(Player& p, const char* cause);
    void syncPlayerArmorAttributes(Player& p);
    void addHungerExhaustion(Player& p, float amount);
    void addFoodAndSaturation(Player& p, int food, float sat);
    bool tryRandomTeleport(Player& player, float diameter);
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
    void broadcastDigStageAt(Player& p, std::int8_t dimension,
                             std::int32_t x, std::int32_t y, std::int32_t z,
                             std::int8_t stage);
    void tickDigs();

    const ServerConfig& config() const { return cfg_; }
    std::uint8_t defaultGameMode() const noexcept {
        return defaultGameMode_.load(std::memory_order_acquire);
    }
    void setDefaultGameMode(std::uint8_t mode) noexcept {
        if (mode <= 3) defaultGameMode_.store(mode, std::memory_order_release);
    }
    jvm::JvmRuntime* jvmRuntime() { return jvmRuntime_.get(); }
    const jvm::JvmRuntime* jvmRuntime() const { return jvmRuntime_.get(); }
    World& world() { return dimensionRuntime(0).world; }
    World& worldByDim(std::int8_t d) { return worldFor(d); }
    EmbeddedData& data() { return data_; }
    bool running() const { return running_; }

    // Execute a short native operation on the authoritative server thread.
    // Calls made by that thread run inline. Foreign callers wait at most the
    // requested timeout; a still-pending request is cancelled and never runs
    // later. A request already running may finish after false is returned, so
    // callbacks must own all captured state and callers must treat timeout as
    // an indeterminate result, not proof that no side effect occurred.
    bool runOnServerThread(
        std::function<void()> task,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(250));
    std::size_t pendingServerThreadTasksForTest() const {
        std::lock_guard lock(serverThreadTasksMtx_);
        return static_cast<std::size_t>(std::count_if(
            serverThreadTasks_.begin(), serverThreadTasks_.end(), [](const auto& task) {
                return task && task->state.load(std::memory_order_acquire) ==
                                   ServerThreadTask::State::Pending;
            }));
    }

    using PlayerRef = std::shared_ptr<Player>;
    std::vector<PlayerRef> playersSnapshot() const {
        std::lock_guard lk(playersMtx_);
        return players_;
    }
    std::vector<std::shared_ptr<MobEntity>> mobsSnapshot() const {
        std::lock_guard lk(entsMtx_);
        return mobs_;
    }
    std::int64_t tickNoForTest() const { return tickNo_; }
    std::size_t playerCount() {
        std::lock_guard lk(playersMtx_);
        return players_.size();
    }
    std::int32_t nextEntityId() { return entityIdCounter_++; }
    bool reservePlayerAdmission() {
        std::lock_guard admissionLock(playerAdmissionMtx_);
        std::lock_guard playersLock(playersMtx_);
        const auto maximum = static_cast<std::size_t>(std::max(0, cfg_.maxPlayers));
        if (players_.size() >= maximum ||
            pendingPlayerAdmissions_ >= maximum - players_.size())
            return false;
        ++pendingPlayerAdmissions_;
        return true;
    }
    bool reservePlayerAdmissionReplacing(
        const std::array<std::uint8_t, 16>& uuid) {
        std::lock_guard admissionLock(playerAdmissionMtx_);
        std::lock_guard playersLock(playersMtx_);
        const auto maximum = static_cast<std::size_t>(std::max(0, cfg_.maxPlayers));
        std::size_t replacedPlayers = 0;
        for (const auto& player : players_) {
            if (!player) continue;
            // UUID is assigned before a Player can enter the roster and is
            // immutable afterwards; avoid taking player-state locks while
            // admission and roster locks are held.
            if (player->uuid == uuid) ++replacedPlayers;
        }
        replacedPlayers = std::min(replacedPlayers, players_.size());
        const auto effectivePlayers = players_.size() - replacedPlayers;
        if (effectivePlayers >= maximum ||
            pendingPlayerAdmissions_ >= maximum - effectivePlayers)
            return false;
        ++pendingPlayerAdmissions_;
        return true;
    }
    void releasePlayerAdmission() noexcept {
        std::lock_guard admissionLock(playerAdmissionMtx_);
        if (pendingPlayerAdmissions_ != 0) --pendingPlayerAdmissions_;
    }
    void addPlayer(PlayerRef p, bool consumesReservation = false) {
        if (!p) return;
        std::lock_guard admissionLock(playerAdmissionMtx_);
        kickDuplicate(*p);   // plan45 B6 W-13(c): vanilla kicks the older session
        std::lock_guard lk(playersMtx_);
        players_.push_back(std::move(p));
        if (consumesReservation && pendingPlayerAdmissions_ != 0)
            --pendingPlayerAdmissions_;
    }
    void kickDuplicate(const Player& incoming) {
        std::lock_guard admissionLock(playerAdmissionMtx_);
        std::array<std::uint8_t, 16> incomingUuid{};
        std::string incomingName;
        {
            std::lock_guard incomingLock(incoming.stateMtx);
            incomingUuid = incoming.uuid;
            incomingName = incoming.name;
        }
        std::vector<PlayerRef> victims;
        for (auto& e : playersSnapshot()) {
            if (!e || e.get() == &incoming) continue;
            bool duplicate = false;
            {
                std::lock_guard existingLock(e->stateMtx);
                duplicate = e->uuid == incomingUuid || e->name == incomingName;
            }
            if (duplicate) victims.push_back(e);
        }
        for (auto& v : victims) {
            WriteBuffer kick;
            nbt::writeTextComponent(kick, "You logged in from another location");
            std::shared_ptr<Connection> connection;
            std::string name;
            {
                std::lock_guard victimLock(v->stateMtx);
                v->inPlay = false;
                connection = v->conn;
                name = v->name;
            }
            if (connection) {
                connection->trySendPacket(proto::pl::sc::Disconnect, kick);
                connection->close();
            }
            std::fprintf(stderr, "[cppfm] duplicate login %s: kicked older session\n",
                         name.c_str());
        }
        if (!victims.empty()) {
            std::lock_guard lk(playersMtx_);
            std::erase_if(players_, [&](const PlayerRef& e) {
                return std::find(victims.begin(), victims.end(), e) != victims.end();
            });
        }
    }
    void removePlayer(const Player* p) {
        std::lock_guard lk(playersMtx_);
        std::erase_if(players_, [p](const PlayerRef& e) { return e.get() == p; });
    }

    PlayerLoginHandoffCoordinator::Lease enterPlayerLoginHandoff(
        const std::string& uuidHex) {
        return playerLoginHandoff_.enter(uuidHex);
    }
    bool playerLoginHandoffBlocked(const std::string& uuidHex) const {
        return playerLoginHandoff_.blocked(uuidHex);
    }
    void blockPlayerLoginHandoff(const std::string& uuidHex) {
        playerLoginHandoff_.block(uuidHex);
    }

    void broadcastSystemText(const std::string& text, Player* except = nullptr) {
        WriteBuffer body;
        nbt::writeTextComponent(body, text);
        body.boolean(false);
        broadcastPacketExcept(except, proto::pl::sc::SystemChat, body);
    }
    template <typename Operation>
    void serializePlayerTrackingPublication(Operation&& operation) {
        playerTrackingSequencer_.serialize(
            std::forward<Operation>(operation));
    }
    // Caller must hold serializePlayerTrackingPublication() across the related
    // state transition and any spawn publication.
    void broadcastPlayerTrackingRemovalUnsequenced(
        const Player& subject, std::int8_t dimension) {
        WriteBuffer remove;
        remove.varint(1);
        remove.varint(subject.entityId);
        broadcastPlayerTrackingPacketInDimension(
            dimension, &subject, proto::pl::sc::RemoveEntities, remove);
    }
    void broadcastPlayerTrackingPacketInDimension(
        const std::int8_t dimension, const Player* except,
        const std::uint8_t packetId, const WriteBuffer& body) {
        runWithoutMobStateLock([&] {
            const auto targetDimension = canonicalDimension(dimension);
            for (const auto& player : playersSnapshot()) {
                if (!player || player.get() == except) continue;
                std::shared_ptr<Connection> connection;
                bool inPlay = false;
                std::int8_t playerDimension = 0;
                {
                    std::lock_guard playerLock(player->stateMtx);
                    inPlay = player->inPlay;
                    playerDimension = player->dimension;
                    connection = player->conn;
                }
                if (!inPlay || !connection ||
                    canonicalDimension(playerDimension) != targetDimension)
                    continue;
                // A live client that misses either side of its tracked-entity
                // update cannot be repaired safely with later deltas. Disconnect
                // that recipient instead of preserving a divergent entity set.
                if (!connection->trySendPacket(packetId, body))
                    connection->close();
            }
        });
    }
    void removePlayerTrackingAfterFailedDimensionChange(
        Player& subject, std::int8_t sourceDimension,
        std::int8_t targetDimension) {
        serializePlayerTrackingPublication([&] {
            {
                std::lock_guard playerLock(subject.stateMtx);
                subject.inPlay = false;
                subject.trackingSpawnPublished = false;
            }
            broadcastPlayerTrackingRemovalUnsequenced(
                subject, sourceDimension);
            if (canonicalDimension(sourceDimension) !=
                canonicalDimension(targetDimension)) {
                broadcastPlayerTrackingRemovalUnsequenced(
                    subject, targetDimension);
            }
        });
    }
    void broadcastPacketExcept(const Player* except, std::uint8_t id, const WriteBuffer& body) {
        // Keep chunk snapshots and their deltas in one ordered stream.
        const bool chunkDependent = requiresChunkPacketOrdering(id);
        runWithoutMobStateLock([&] {
            const auto players = playersSnapshot();
            // Brain actions historically use the dimension-less broadcast helper.
            // While a mob brain is executing, scope those packets to the mob's
            // dimension so a Nether event cannot leak to Overworld clients.  The
            // pointer is set only around the synchronous brain call in mobsTick.
            const MobEntity* sourceMob = brainTickGuard_;
            const bool hasSourceDimension = sourceMob != nullptr;
            const std::int8_t sourceDimension =
                hasSourceDimension ? canonicalDimension(sourceMob->dimension) : 0;
            // Recipient state and frames remain serialized; Connection owns
            // the asynchronous socket handoff.
            for (auto& p : players) {
                if (!p || p.get() == except) continue;
                std::shared_ptr<Connection> connection;
                bool inPlay = false;
                std::int8_t dimension = 0;
                {
                    std::lock_guard playerLock(p->stateMtx);
                    inPlay = p->inPlay;
                    dimension = p->dimension;
                    connection = p->conn;
                }
                if (!inPlay || !connection) continue;
                if (hasSourceDimension && canonicalDimension(dimension) != sourceDimension)
                    continue;
                if (chunkDependent) connection->trySendPacketLowPriority(id, body);
                else connection->trySendPacket(id, body);
            }
        });
    }
    void broadcastPacketExceptInDimension(std::int8_t dimension,
                                          const Player* except,
                                          std::uint8_t id,
                                          const WriteBuffer& body) {
        const bool chunkDependent = requiresChunkPacketOrdering(id);
        runWithoutMobStateLock([&] {
            const auto target = canonicalDimension(dimension);
            const auto players = playersSnapshot();
            for (auto& p : players) {
                if (!p || p.get() == except) continue;
                std::shared_ptr<Connection> connection;
                bool inPlay = false;
                std::int8_t playerDimension = 0;
                {
                    std::lock_guard playerLock(p->stateMtx);
                    inPlay = p->inPlay;
                    playerDimension = p->dimension;
                    connection = p->conn;
                }
                if (!inPlay || !connection ||
                    canonicalDimension(playerDimension) != target) continue;
                if (chunkDependent) connection->trySendPacketLowPriority(id, body);
                else connection->trySendPacket(id, body);
            }
        });
    }
    void broadcastBlockChange(std::int32_t x, std::int32_t y, std::int32_t z,
                              std::uint16_t state);
    void broadcastBlockChangeFor(std::int8_t dimension, std::int32_t x,
                                 std::int32_t y, std::int32_t z,
                                 std::uint16_t state);
    void queueBlockChange(std::int32_t x, std::int32_t y, std::int32_t z,
                          std::uint16_t state);
    void queueBlockChangeFor(std::int8_t dimension, std::int32_t x,
                             std::int32_t y, std::int32_t z,
                             std::uint16_t state);
    void flushBlockBatches();
    void broadcastPlayerChat(Player& sender, const std::string& message, std::int64_t timestamp);
    using ChunkBodyRef = ChunkBodyCache::BodyRef;
    using ChunkCacheSnapshotTicket = ChunkBodyCache::SnapshotTicket;
    using ChunkCacheStats = ChunkBodyCache::Stats;
    // Kept as aliases here for the existing server-facing API. The key and
    // its save/load coordinator are owned by the chunk-I/O module.
    using DimensionChunkKey = chunk_io_detail::DimensionChunkKey;
    using DimensionChunkKeyHash = chunk_io_detail::DimensionChunkKeyHash;
    static std::int8_t canonicalDimension(std::int8_t dim) noexcept {
        return chunk_io_detail::canonicalDimension(dim);
    }
    static DimensionChunkKey dimensionChunkKey(std::int8_t dim,
                                                std::int32_t cx,
                                                std::int32_t cz) noexcept {
        return chunk_io_detail::dimensionChunkKey(dim, cx, cz);
    }
    struct ChunkIoStats { std::size_t ioQueueDepth = 0, pendingLoads = 0; int viewDistance = 32; };
    double chunkCacheHitRate() const {
        return chunkBodyCache_.hitRate();
    }
    ChunkCacheStats chunkCacheStats() const {
        return chunkBodyCache_.stats();
    }
    ChunkIoStats chunkIoStats() const {
        std::lock_guard lk(pendingLoadsMtx_);
        int vd = cfg_.viewDistance;
        return {ioPool_.pending(), pendingLoads_.size(), vd};
    }
    bool getCachedChunk(std::int32_t cx, std::int32_t cz, std::uint32_t biomeIdx,
                        ChunkBodyRef& out) {
        return getCachedChunkFor(0, cx, cz, biomeIdx, out);
    }
    bool getCachedChunkFor(std::int8_t dim, std::int32_t cx, std::int32_t cz,
                           std::uint32_t biomeIdx, ChunkBodyRef& out) {
        (void)biomeIdx; // retained for API/source compatibility; body is keyed by dim/pos.
        return chunkBodyCache_.get(dim, cx, cz, out);
    }
    ChunkCacheSnapshotTicket beginChunkCacheSnapshot(
        std::int8_t dim, std::int32_t cx, std::int32_t cz) {
        return chunkBodyCache_.beginSnapshot(dim, cx, cz);
    }
    template <typename Publish>
    bool publishCachedChunkForSnapshot(
        const ChunkCacheSnapshotTicket& ticket, Publish&& publish) {
        return chunkBodyCache_.publishCached(
            ticket, std::forward<Publish>(publish));
    }
    template <typename Publish>
    bool publishChunkForSnapshot(const ChunkCacheSnapshotTicket& ticket,
                                 std::uint64_t rev, ChunkBodyRef body,
                                 Publish&& publish) {
        (void)rev; // retained for the server-facing cache API
        return chunkBodyCache_.publish(
            ticket, std::move(body), std::forward<Publish>(publish));
    }
    bool storeChunkForSnapshot(const ChunkCacheSnapshotTicket& ticket,
                               std::uint64_t rev, ChunkBodyRef body) {
        (void)rev; // retained for the server-facing cache API
        return chunkBodyCache_.storeForSnapshot(ticket, std::move(body));
    }
    void storeChunk(std::int32_t cx, std::int32_t cz, std::uint64_t rev, ChunkBodyRef body) {
        storeChunkFor(0, cx, cz, rev, std::move(body));
    }
    void storeChunkFor(std::int8_t dim, std::int32_t cx, std::int32_t cz,
                       std::uint64_t rev, ChunkBodyRef body) {
        (void)rev; // cache identity is dimension and coordinates only
        chunkBodyCache_.store(dim, cx, cz, std::move(body));
    }
    void invalidateChunkCache(std::int32_t cx, std::int32_t cz) {
        invalidateChunkCacheFor(0, cx, cz);
    }
    void invalidateChunkCacheFor(std::int8_t dim, std::int32_t cx, std::int32_t cz) {
        chunkBodyCache_.invalidate(dim, cx, cz);
    }
    void clearChunkCache() {
        chunkBodyCache_.clear();
    }
    void demandChunkAsync(std::int32_t cx, std::int32_t cz);
    void demandChunkAsyncFor(std::int8_t dim, std::int32_t cx, std::int32_t cz);
    void saveChunkAsync(std::int32_t cx, std::int32_t cz);
    void saveChunkAsyncFor(std::int8_t dim, std::int32_t cx, std::int32_t cz);
    std::size_t chunkCacheSize() const {
        return chunkBodyCache_.size();
    }
    std::size_t chunkCacheBytes() const {
        return chunkBodyCache_.bytes();
    }
    std::size_t ioQueueDepth() const { return ioPool_.pending(); }
    std::size_t pendingLoadsSize() const {
        std::lock_guard lk(pendingLoadsMtx_);
        return pendingLoads_.size();
    }
    std::size_t chunkCacheHits() const { return chunkBodyCache_.hits(); }
    std::size_t chunkCacheMisses() const { return chunkBodyCache_.misses(); }

private:
    struct ServerThreadTask {
        enum class State : std::uint8_t {
            Pending,
            Running,
            Completed,
            Failed,
            Cancelled,
        };

        explicit ServerThreadTask(std::function<void()> operation)
            : task(std::move(operation)) {}

        std::function<void()> task;
        std::atomic<State> state{State::Pending};
        std::mutex waitMtx;
        std::condition_variable waitCv;
    };

    static constexpr std::size_t kMaxServerThreadTaskQueue = 1024;
    static constexpr std::size_t kMaxServerThreadTasksPerTick = 256;

    bool isCurrentServerThread() const noexcept;
    bool isCurrentTickThread() const noexcept;
    void initializeDimensionEngines();
    void drainLightUpdatesFor(std::int8_t dimension);
    void configureDimensionEngines(std::int8_t dimension);
    void loadStartupData();
    static void registerDefaultBlockTickBehaviors(
        BlockTickScheduler& scheduler);
    void configurePrimaryPersistence();
    void initializePersistedWorld();
    void initializeOptionalServices();
    void stopIngress();
    void stopWorkersAndJvm();
    bool stopPersistenceAndFlush();
    void closeListenerAndReleaseWorldLock(bool persistenceSucceeded);
    inline static thread_local const GameServer* sessionThreadOwner_ = nullptr;
    class SessionThreadScope {
    public:
        explicit SessionThreadScope(const GameServer* owner) noexcept
            : previous_(sessionThreadOwner_) {
            sessionThreadOwner_ = owner;
        }
        ~SessionThreadScope() { sessionThreadOwner_ = previous_; }
        SessionThreadScope(const SessionThreadScope&) = delete;
        SessionThreadScope& operator=(const SessionThreadScope&) = delete;

    private:
        const GameServer* previous_;
    };
    void claimServerThreadForBootstrap() noexcept;
    void bindServerThread() noexcept;
    void releaseServerThread() noexcept;
    void drainServerThreadTasks() noexcept;
    void cancelServerThreadTasks() noexcept;
    void waitForServerThreadTasks();

    mutable std::mutex serverThreadTasksMtx_;
    std::deque<std::shared_ptr<ServerThreadTask>> serverThreadTasks_;
    std::condition_variable serverThreadTaskIdleCv_;
    std::size_t pendingServerThreadTaskCleanups_ = 0;
    std::thread::id serverThreadId_{};
    std::thread::id tickThreadId_{};
    bool serverThreadAccepting_ = false;
    std::atomic<std::size_t> activeServerThreadTasks_{0};

    using ChunkSaveCoordinator = chunk_io_detail::ChunkSaveCoordinator;

    struct PendingChunkLoad {
        std::shared_ptr<ChunkSaveCoordinator::Stamp> stamp;
        std::uint64_t generation = 0;
        std::future<Persistence::ChunkBytesRead> future;
    };

    void acceptLoop();
    bool registerSessionThread(
        std::thread worker,
        const std::shared_ptr<std::atomic<bool>>& finished);
    void joinSessionThreads();
    void registerActiveConnection(const std::shared_ptr<Connection>& connection);
    void unregisterActiveConnection(const std::shared_ptr<Connection>& connection);
    void stopClientConnections();
    void joinJanitorThread();
    void invalidateJvmMob(const std::shared_ptr<MobEntity>& mob);

    ServerConfig cfg_;
    EntityDataLoader entityDataLoader_;
    std::array<std::unique_ptr<DimensionRuntime>, 3> dimensionRuntimes_;
    // entities
    mutable std::mutex entsMtx_;
    struct MobAiEntry {
        std::unique_ptr<Brain> brain;
        std::unique_ptr<AiContext> ctx;
    };
    mutable std::mutex mobAiMtx_;
    std::unordered_map<std::int32_t, std::shared_ptr<MobAiEntry>> mobAi_;
    std::shared_ptr<MobAiEntry> aiFor(const std::shared_ptr<MobEntity>& m);
public:
    // Finds an in-love adult partner of the same kind within 8 blocks.
    std::shared_ptr<MobEntity> findLovePartner(const MobEntity& seeker);
    void eraseMobAi(std::int32_t entityId);
    void noteMobHurt(std::int32_t entityId, std::int32_t attackerEntityId,
                     std::int32_t lootingLevel = 0);
    LootContext mobLootContextForDeath(std::int32_t entityId,
                                      bool entityOnFire,
                                      float explosionRadius = 0.f) const;
    void spawnMobLootFor(MobKind kind, std::int8_t dimension,
                         double x, double y, double z,
                         const LootContext& context);
private:
    // AI compatibility wrappers use this only to infer a dimension for legacy
    // calls such as broadcastSound().  It must be thread-local: network/JVM
    // callbacks can run concurrently with the game tick.
    inline static thread_local const MobEntity* brainTickGuard_ = nullptr;
    std::vector<std::shared_ptr<MobEntity>> mobs_;
    std::vector<std::shared_ptr<ItemEntity>> itemDrops_;
    std::vector<std::shared_ptr<XpOrbEntity>> xpOrbs_;
    mutable std::mutex projectilesMtx_;
    std::vector<std::shared_ptr<ProjectileEntity>> projectiles_;
    std::vector<std::shared_ptr<TntEntity>> tntEntities_;
    std::atomic<std::int64_t> tickNo_{0};
    std::int64_t timeOffset_ = 0;
    std::int64_t startTime_ = 1000;
    PacketBatcher batcher_;
    std::int64_t lastBlockBatchFlushMs_ = 0;
    std::thread tickThread_;
    std::thread janitorThread_;
    struct SessionThread {
        std::thread worker;
        std::shared_ptr<std::atomic<bool>> finished;
    };
    mutable std::mutex sessionThreadsMtx_;
    std::vector<SessionThread> sessionThreads_;
    bool sessionThreadsStopping_ = false;
    SessionAdmissionGate pendingSessionAdmissionGate_;
    SessionAdmissionGate statusAdmissionGate_{16};
    mutable std::mutex activeConnectionsMtx_;
    std::vector<std::shared_ptr<Connection>> activeConnections_;
    std::mutex stopCvMtx_;
    std::condition_variable stopCv_;
    SessionLock sessionLock_; // world/session.lock guard
    PlayerLoginHandoffCoordinator playerLoginHandoff_;
    PlayerTrackingSequencer playerTrackingSequencer_;
    bool initialized_ = false;
    Whitelist whitelist_;
    std::unique_ptr<RconServer> rconServer_;
    crypto::RsaKeyPair loginKeys_;
    std::vector<std::uint8_t> loginVerifyToken_;
  public:
    std::vector<std::uint8_t>& loginVerifyToken() { return loginVerifyToken_; }
    EmbeddedData data_;
    GameData gameData_;                                 // parsed registry orders
    std::vector<PlayerRef> players_;
    mutable std::recursive_mutex playerAdmissionMtx_;
    std::size_t pendingPlayerAdmissions_ = 0; // protected by playerAdmissionMtx_
    mutable std::mutex playersMtx_;
    RecipeManager recipes_;                          // crafting/smelting data
    TagManager tagManager_;
    LootTableEvaluator lootTables_;
    brigadier::CommandDispatcher commands_;          // Brigadier tree
    GameRuleManager gamerules_;
    std::string difficulty_ = "normal";
    std::atomic<std::uint8_t> defaultGameMode_{0};
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
    // WanderingTrader scheduling (vanilla WanderingTraderManager)
    int wanderingTraderSpawnDelay_ = 0;
    int wanderingTraderSpawnChance_ = 25;

public:
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
        if (gamerules_.contains("doTraderSpawning") &&
            !gamerules_.getBool("doTraderSpawning")) return;
        // Also respect doMobSpawning as the global spawn kill-switch.
        if (gamerules_.contains("doMobSpawning") &&
            !gamerules_.getBool("doMobSpawning")) return;
        if (wanderingTraderSpawnDelay_ > 0) { --wanderingTraderSpawnDelay_; return; }
        int roll = nextRandom() % 100;
        bool shouldSpawn = roll < wanderingTraderSpawnChance_;
        if (shouldSpawn) {
            auto players = playersSnapshot();
            if (!players.empty()) {
                auto &p = players[nextRandom() % players.size()];
                if (p->inPlay) {
                    double sx = p->x + (nextRandom()%48 - 24);
                    double sz = p->z + (nextRandom()%48 - 24);
                    double sy = p->y;
                    for (int y = (int)sy + 10; y > (int)sy - 10; --y) {
                        if (worldFor(0).getBlock((int)sx, y, (int)sz)==0 && worldFor(0).getBlock((int)sx, y-1, (int)sz)!=0) { sy = y; break; }
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
        auto sp = worldFor(0).spawnPoint();
        int dx = std::abs(x - sp.x);
        int dz = std::abs(z - sp.z);
        return std::max(dx, dz) <= spawnProtection_;
    }
    bool isOp(const std::string& name) const { return ops_.count(name) > 0; }
    static bool isValidPlayerName(const std::string& n) {
        if (n.size() < 3 || n.size() > 16) return false;
        for (char c : n) {
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '_';
            if (!ok) return false;
        }
        return true;
    }
    // clamp to the vanilla 2..32 window before it touches chunk logic.
    static int clampClientViewDistance(int v) {
        if (v < 2) return 2;
        if (v > 32) return 32;
        return v;
    }
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
    BossAIManager* bossAI() { return bossAI_.get(); }
    const BossAIManager* bossAI() const { return bossAI_.get(); }
    BossBarManager* bossBars() { return bossAI_ ? &bossAI_->bars() : nullptr; }
private:
    // HungerManager/CombatManager are real classes with .cpp implementations.
    std::unique_ptr<BossAIManager> bossAI_;
    std::unique_ptr<jvm::JvmRuntime> jvmRuntime_;
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
    DatapackManager datapackManager_;
    FunctionEvaluator functionEvaluator_;
    DatapackManager& datapackManager() { return datapackManager_; }
    const DatapackManager& datapackManager() const { return datapackManager_; }
    void refreshDatapackConsumers() {
        tagManager_ = datapackManager_.tagManager;
        lootTables_ = datapackManager_.lootTables;
        recipes_.clear();
        recipes_.loadDefaults();
        recipes_.syncTagsFrom(tagManager_);
        recipes_.loadDirectory(cfg_.recipesDir);
        {
            std::lock_guard cacheLock(advMergeMtx_);
            cachedMergedAdv_.clear();
            cachedAdvRawSize_ = 0;
        }
    }
    FunctionEvaluator& functionEvaluator() { return functionEvaluator_; }
    const FunctionEvaluator& functionEvaluator() const { return functionEvaluator_; }
    void tickScheduledFunctions() {
        std::lock_guard commandLock(commandDispatchMtx_);
        functionEvaluator_.tick(tickNo_);
    }
private:
    void registerSession(Session* session) {
        if (!session) return;
        SimulationDispatchGuard simulation(simulationDispatchMtx_);
        activeSessions_.insert(session);
    }
    void unregisterSession(Session* session) {
        if (!session) return;
        SimulationDispatchGuard simulation(simulationDispatchMtx_);
        activeSessions_.erase(session);
    }
    void resyncOpenMenusAtBlocks(
        std::int8_t dimension,
        const std::unordered_set<std::int64_t>& changedBlockKeys) {
        if (changedBlockKeys.empty()) return;
        SimulationDispatchGuard simulation(simulationDispatchMtx_);
        dimension = canonicalDimension(dimension);
        for (Session* session : activeSessions_) {
            if (!session) continue;
            session->resyncOpenMenuForBlockChanges(
                dimension, changedBlockKeys);
        }
    }
    ChunkBodyCache chunkBodyCache_;
    std::unordered_map<std::int32_t, std::int64_t> ghostThrottle_; // entityId -> last tick for PlaceGhostRecipe 0x39
    // W19/B-07 async I/O: ThreadPool for RegionFile zlib offload (Yarn ThreadedAnvilChunkStorage)
    core::ThreadPool ioPool_;
    std::shared_ptr<ChunkSaveCoordinator> chunkSaveCoordinator_ =
        std::make_shared<ChunkSaveCoordinator>();
    // pending async chunk loads (ChunkPos -> future) polled in tickOnce via pollPendingLoads()
    std::unordered_map<DimensionChunkKey, PendingChunkLoad,
                       DimensionChunkKeyHash> pendingLoads_;
    mutable std::mutex pendingLoadsMtx_;
    void pollPendingLoads(); // B-07: drain ready futures and install chunks (defined in GameServer_tick.cpp)
    std::atomic<bool> running_{false};
    // Reject new command ingress once teardown starts.  This is separate
    // from running_: embedded callers and tests legitimately dispatch
    // commands before the network loop has been started.
    std::atomic<bool> shutdownStarted_{false};
    std::atomic<bool> shutdownPersistenceSucceeded_{true};
    // Every authoritative ingress (tick, packet, console, and RCON command)
    // takes this lock before touching world/player state.  The recursive mutex
    // permits command callbacks and scheduled functions to re-enter safely.
    // The narrower command lock serializes command registration/evaluation.
    mutable std::recursive_mutex simulationDispatchMtx_;
    mutable std::recursive_mutex commandDispatchMtx_;
    // Guarded by simulationDispatchMtx_: entries remain valid until the
    // session unregisters in its teardown path.
    std::unordered_set<Session*> activeSessions_;
    std::atomic<platform::socket_t> listenFd_{platform::invalid_socket};
    AcceptGate acceptGate_{20};
    std::atomic<std::int32_t> entityIdCounter_{1};
    std::atomic<int> nextMapId_{1}; // plan42 MapData allocation
};

} // namespace cppfm
