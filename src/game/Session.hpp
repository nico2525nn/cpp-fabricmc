#pragma once

#include "Containers.hpp"
#include "ConsumableUseState.hpp"
#include "Player.hpp"
#include "SimulationDispatchGuard.hpp"
#include "../api/PluginChannels.hpp"
#include "../net/Connection.hpp"
#include "../net/RateLimiter.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cppfm {

class GameServer;

// Owns one connection's protocol state machine and connection-local state.
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
    std::int8_t dimension() const {
        std::lock_guard playerLock(self_->stateMtx);
        return self_->dimension;
    }
    bool tryMarkBlockEntityDirty(std::int8_t dimension, std::int64_t key) {
        return srv_.blockEntitiesFor(dimension).tryMarkDirty(key);
    }
    void markBlockEntityDirty(std::int8_t dimension, std::int64_t key) {
        srv_.blockEntitiesFor(dimension).markDirty(key);
    }
    void spawnMenuDrop(std::int8_t dimension, double x, double y, double z,
                       const ItemStack& stack) {
        srv_.spawnItemDropFor(dimension, x, y, z, stack, 0, 0.15, 0);
    }
    void notifyMenuItemObtained(Player& player, const ItemStack& stack,
                                const char* how) {
        srv_.onItemObtained(player, stack, how);
    }

private:
    friend class GameServer;
    void handleHandshake(ReadBuffer& in);
    void answerLegacyPing();
    void handleStatus();
    void handleLogin();
    void handleConfiguration();
    void handlePlay();
    enum class ConfigWaitResult { Continue, FinishAck, PacksDone, Abort };
    ConfigWaitResult handleOneConfigPacket(
        ReadBuffer& in, SimulationDispatchGuard& simulationLock);
    void onEnterPlay(SimulationDispatchGuard& simulationLock);
    void tickChunksAround(double px, double pz,
                          SimulationDispatchGuard& simulationLock);
    void sendNextChunkBatch(std::int8_t dimension,
                            SimulationDispatchGuard& simulationLock);

    void onChatMessage(ReadBuffer& in, SimulationDispatchGuard& simulationLock);
    void onPlayerAction(ReadBuffer& in,
                        SimulationDispatchGuard& simulationLock);
    void onUseItemOn(ReadBuffer& in,
                     SimulationDispatchGuard& simulationLock);
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
    bool tryStartConsumableUse(std::size_t inventorySlot, ItemStack& stack);
    void publishHandUseMetadata(bool active, bool offhand);
    void cancelConsumableUse();
    void stopAllItemUse();
    void finishConsumableUse();
    void completeConsumableUse(ItemStack& stack);
    void tickConsumableUse();
    void onHeldSlot(ReadBuffer& in);
    void onAcceptTeleportation(ReadBuffer& in,
                               SimulationDispatchGuard& simulationLock);
    void onKeepAlivePacket(ReadBuffer& in);
    bool onChatCommandSignedPacket(
        ReadBuffer& in, SimulationDispatchGuard& simulationLock);
    void onChatSessionUpdate(ReadBuffer& in);
    void onCustomPayload(ReadBuffer& in,
                         SimulationDispatchGuard& simulationLock);
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
    void onUseEntity(ReadBuffer& in,
                     SimulationDispatchGuard& simulationLock);
    struct EntityAttackPlan;
    struct EntityAttackMobResult;
    void handleEntityAttack(std::int32_t target,
                            SimulationDispatchGuard& simulationLock);
    EntityAttackPlan prepareEntityAttackPlan();
    bool handlePlayerEntityAttack(std::int32_t target,
                                  const EntityAttackPlan& plan,
                                  SimulationDispatchGuard& simulationLock);
    EntityAttackMobResult hitMobTarget(std::int32_t target,
                                       const EntityAttackPlan& plan,
                                       SimulationDispatchGuard& simulationLock);
    void applyMobSweep(const EntityAttackPlan& plan,
                       const EntityAttackMobResult& result,
                       SimulationDispatchGuard& simulationLock);
    void applyMobKnockback(const EntityAttackPlan& plan,
                           const EntityAttackMobResult& result);
    void finishMobKill(std::int32_t target, const EntityAttackPlan& plan,
                       const EntityAttackMobResult& result,
                       SimulationDispatchGuard& simulationLock);
    void handleMobEntityAttack(std::int32_t target,
                               const EntityAttackPlan& plan,
                               SimulationDispatchGuard& simulationLock);
    void handleRespawnRequest();
    void sendDeclareCommands();
    void onChatCommand(ReadBuffer& in,
                       SimulationDispatchGuard& simulationLock);
    void dispatchCommand(const std::string& line,
                         SimulationDispatchGuard& simulationLock);
    void broadcastMovement();
    void broadcastSpawnEntity(Player* about);
    void broadcastSpawnEntityUnderTrackingSequencer(Player* about);
    void publishPlayerTrackingAfterDimensionTransfer();
    void synchronizeEquipmentForTrackedPlayers();
    void onMovement(ReadBuffer& in, bool hasPos, bool hasRot,
                    SimulationDispatchGuard& simulationLock);
    void updateMovementPhysics(double oldX, double oldY, double oldZ,
                               bool wasOnGround, bool nowGround,
                               bool hasPosition,
                               SimulationDispatchGuard& simulationLock);
    void resolveMovementLanding(
        SimulationDispatchGuard& simulationLock);
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
                         const api::ChannelRegistry::Payload& body, int phase,
                         SimulationDispatchGuard& simulationLock);
    void sendPluginPayload(int phase, const std::string& channel,
                           const std::vector<std::uint8_t>& body);

    void disconnectIn(const char* jsonReason);
    void kickPlay(const char* jsonReason);
    void sendSystemText(const std::string& text);
    void sendJoinGame();
    void sendTeleport(double x, double y, double z, float yaw, float pitch);
    bool sendChunk(std::int8_t dimension, std::int32_t cx, std::int32_t cz,
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
    void onCloseContainer();
    void handleMenuClick(Menu& m, int slot, int button, int mode);
    void handlePlayerInventoryClick(int slot, int button, int mode);
    void sendMenuContent(Menu& m);
    void resyncOpenMenuAt(std::int8_t dimension, std::int64_t blockKey);
    bool menuItemsSerializable(Menu& m);
    void appendEnchantmentProperties(Menu& m,
                                     std::vector<WriteBuffer>& packets);
    void sendSetSlot(std::int32_t windowId, std::int32_t stateId,
                     std::int16_t slot, const ItemStack& stack);
    void syncCursorItem();
    void openMenuAt(std::int32_t x, std::int32_t y, std::int32_t z,
                    std::uint16_t stateIdOfBlock);
    void closeOpenMenu(bool sendPacket);
    void onWindowClick(ReadBuffer& in);
    void onEnchantItem(ReadBuffer& in);
    void resyncOpenMenuForBlockChanges(
        std::int8_t dimension,
        const std::unordered_set<std::int64_t>& changedBlockKeys);
    void applyClientSettings(Player::ClientSettings settings);
    bool requireOp(int level, const char* what);
    void sendTagQueryResponse(std::int32_t transactionId,
                              const WriteBuffer& nbt);
    void answerBlockNbt(std::int32_t transactionId, std::int32_t x,
                        std::int32_t y, std::int32_t z);
    void answerEntityNbt(std::int32_t transactionId, std::int32_t entityId);
    void onNameItem(const std::string& name);
    void onBeaconEffect(std::optional<std::int32_t> primary,
                        std::optional<std::int32_t> secondary);
    void onSpectate(const std::array<std::uint8_t,16>& target);

    GameServer& srv_;
    std::shared_ptr<Connection> conn_;
    std::function<bool()> statusAdmission_;
    std::function<void()> releaseAdmission_;
    std::optional<std::uint64_t> playerAdmissionReservation_;
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
    bool registered_ = false;
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
    std::shared_ptr<Menu> openMenu_;
    Menu playerInventoryMenu_;
    ItemStack cursorItem_;
    SpamTracker spam_;
    RateLimitedLog playLogGate_;
    std::int32_t menuWindowCounter_ = 0;
    std::int32_t villagerWindowSeq_ = 100;
    std::int32_t tradingVillager_ = -1;
    std::array<std::uint8_t, 16> resourcePackUuid_{};
    bool resourcePackRequired_ = false;
    bool resourcePackLoaded_ = false;
};

} // namespace cppfm
