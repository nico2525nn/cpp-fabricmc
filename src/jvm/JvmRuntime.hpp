// Embedded Fabric-compatible JVM runtime boundary.
//
// The runtime is a default-on Fabric compatibility boundary.  It remains
// fail-open unless strict mode is requested: a host without a JDK can still
// run the native server, while JNI details, Java references, and class-loader
// state stay behind this interface so game code only sees typed lifecycle and
// event calls.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "JavaObjectCache.hpp"
#include "ModRoutingTable.hpp"
#include "NativeHandleTable.hpp"

namespace cppfm {
class GameServer;
struct Player;
class World;
class MobEntity;
}

namespace cppfm::jvm {

struct JvmConfig {
    bool enabled = false;
    bool strict = false;
    std::string classesDir;
    std::string modsDir = "mods";
    std::string configDir = "config";
    std::string javaHome;
    std::string jvmLibrary;
    // Optional directory of regular dependency JARs used by real Fabric mods
    // (for example the locked server's fastutil library).
    std::string jvmLibrariesDir;
    // KnotLauncher is preferred when present; the fallback provider is kept
    // for the dependency-free compatibility fixture.
    bool preferKnot = true;
    // Queue deadline for native bridge mutations submitted to the authoritative
    // server thread. A still-pending task is cancelled at this deadline; once
    // the task starts, synchronous callers wait for its result rather than
    // reporting failure for a mutation that may still commit.
    std::chrono::milliseconds serverMutationTimeout{250};
    // Fabric callbacks are serialized on the authoritative server thread.
    // This bounds how long a foreign caller waits for a queued callback to
    // start; once running, its synchronous event result is awaited.
    std::chrono::milliseconds serverCallbackTimeout{30000};
};

enum class JvmProvider : std::uint8_t {
    None = 0,
    CompatibilityFallback = 1,
    KnotLauncher = 2,
};

enum class JvmValueKind : std::uint8_t {
    Null = 0,
    Boolean,
    Int,
    Long,
    Float,
    Double,
    String,
    Bytes,
    Handle,
};

// Typed arguments/results for the common native -> transformed-Java path.
// Handle values are opaque NativeHandleTable values, never C++ addresses.
struct JvmValue {
    JvmValueKind kind = JvmValueKind::Null;
    bool booleanValue = false;
    std::int32_t intValue = 0;
    std::int64_t longValue = 0;
    float floatValue = 0.0f;
    double doubleValue = 0.0;
    std::uint64_t handleValue = 0;
    std::string stringValue;
    std::vector<std::uint8_t> bytesValue;

    static JvmValue nullValue() { return {}; }
    static JvmValue boolean(bool value) { JvmValue out; out.kind = JvmValueKind::Boolean; out.booleanValue = value; return out; }
    static JvmValue integer(std::int32_t value) { JvmValue out; out.kind = JvmValueKind::Int; out.intValue = value; return out; }
    static JvmValue longInt(std::int64_t value) { JvmValue out; out.kind = JvmValueKind::Long; out.longValue = value; return out; }
    static JvmValue floating(float value) { JvmValue out; out.kind = JvmValueKind::Float; out.floatValue = value; return out; }
    static JvmValue doubleFloat(double value) { JvmValue out; out.kind = JvmValueKind::Double; out.doubleValue = value; return out; }
    static JvmValue string(std::string value) { JvmValue out; out.kind = JvmValueKind::String; out.stringValue = std::move(value); return out; }
    static JvmValue bytes(std::vector<std::uint8_t> value) { JvmValue out; out.kind = JvmValueKind::Bytes; out.bytesValue = std::move(value); return out; }
    static JvmValue handle(std::uint64_t value) { JvmValue out; out.kind = JvmValueKind::Handle; out.handleValue = value; return out; }
};

struct JvmDispatchResult {
    bool invoked = false;
    bool success = false;
    JvmValue value;
    std::string error;
};

struct JvmStats {
    bool started = false;
    std::size_t discoveredMods = 0;
    std::size_t initializedEntrypoints = 0;
    std::size_t activeHandles = 0;
    std::uint64_t ticks = 0;
    std::uint64_t joins = 0;
    std::uint64_t quits = 0;
    std::uint64_t callbacks = 0;
    std::uint64_t callbackErrors = 0;
    std::size_t transformedMethods = 0;
    std::size_t nativeMethods = 0;
    JvmProvider provider = JvmProvider::None;
    std::uint64_t nativeDispatches = 0;
    std::uint64_t jvmDispatches = 0;
    std::uint64_t dispatchFailures = 0;
    std::uint64_t bridgeExceptions = 0;
};

class JvmRuntime {
public:
    struct Impl;

    JvmRuntime(GameServer& server, JvmConfig config);
    ~JvmRuntime();

    JvmRuntime(const JvmRuntime&) = delete;
    JvmRuntime& operator=(const JvmRuntime&) = delete;

    // Starts one HotSpot VM, registers the native bridge, and loads the Java
    // bootstrap.  When strict=false, a missing JVM/classes directory is
    // reported and the C++ server may continue with its native implementation.
    bool start(std::string* error = nullptr);
    void stop();
    bool started() const noexcept;
    JvmProvider provider() const noexcept;
    std::string lastError() const;
    JvmStats stats() const;

    // Synchronous lifecycle and cancellable event boundaries. A disabled or
    // unavailable runtime is an allow/no-op result so existing C++ behavior is
    // unchanged unless the layer is explicitly active. Callbacks originating
    // from foreign session threads are synchronously marshalled to the
    // authoritative server thread; the simulation gate is released while
    // waiting so callbacks can re-enter native server operations.
    // The server tick
    // boundary selectively routes the shadow MinecraftServer.setTick(J)V body
    // through dispatchTransformed() when Mixin changed that method; the event
    // facade then observes the already-applied tick without calling it again.
    bool onServerTick(std::int64_t tick);
    // Terminal Fabric lifecycle phases. The tick-loop owner dispatches
    // SERVER_STOPPING before ingress closes; teardown dispatches world unload
    // after disconnect and SERVER_STOPPED after persistence closes. stop()
    // supplies idempotent fallbacks for partial/no-loop lifetimes.
    bool onServerWorldsLoaded();
    bool onServerStopping();
    bool onWorldsUnloading();
    bool onServerStopped();
    // Compatibility aggregate for providers that still request one terminal
    // call; production teardown uses the ordered phase methods above.
    bool onServerShutdown();
    bool onPlayerConfiguration(Player& player);
    void onPlayerConfigurationAborted(Player& player);
    void onPlayerJoin(Player& player);
    void onPlayerWorldChange(Player& player, std::int8_t sourceDimension,
                             std::int8_t destinationDimension);
    void beginPlayerRetirement(Player& player);
    void onPlayerQuit(Player& player);
    void finishPlayerRetirement(Player& player);
    void onMobLoad(MobEntity& mob);
    // Returns false only when invalidation must wait for an active re-entrant
    // load callback to unwind; that callback performs the deferred invalidation.
    bool onMobUnload(MobEntity& mob);
    // Roll back an approved spawn that did not reach the authoritative roster.
    void cancelMobSpawn(MobEntity& mob) noexcept;
    bool onChat(Player& player, std::string& message);
    bool onBlockBreak(Player& player, std::int32_t x, std::int32_t y,
                      std::int32_t z, std::uint16_t oldState);
    bool onBlockPlace(Player& player, std::int32_t x, std::int32_t y,
                      std::int32_t z, std::uint16_t newState);
    bool onBlockClicked(Player& player, std::int32_t x, std::int32_t y,
                        std::int32_t z, std::uint16_t state, int face);
    // A Java-registered command returns false after it has been consumed.  If
    // response is supplied, console feedback from that Java command is copied
    // out for RCON; chat callers may leave it null.
    bool onCommand(Player* player, std::string& command,
                   std::string* response = nullptr);
    bool onEntityDamage(Player* victimPlayer, MobEntity* victimMob,
                        float& amount, const std::string& cause);
    bool onEntityAllowDeath(Player* victimPlayer, MobEntity* victimMob,
                            float amount, const std::string& cause);
    void onEntityDamageApplied(Player* victimPlayer, MobEntity* victimMob,
                               float baseDamage, float damageTaken,
                               bool blocked, bool died,
                               const std::string& cause);
    bool onMobSpawn(MobEntity& mob, double x, double y, double z);
    bool onPluginMessage(Player& player, int phase, const std::string& channel,
                         const std::vector<std::uint8_t>& payload);

    // Handle lifetime is tied to C++ object lifetime.  The Java value is
    // opaque and generation-checked; no raw address crosses JNI.
    std::uint64_t playerHandle(Player& player);
    std::uint64_t worldHandle(World& world);
    void invalidatePlayer(Player& player);
    void invalidateEntity(MobEntity& entity);

    // C++ execution paths use this boundary when a transformed Java method is
    // registered.  NativeFast means that the caller keeps its native path;
    // transformed methods are invoked synchronously and type-checked.
    bool shouldUseJvm(const std::string& owner, const std::string& name,
                      const std::string& descriptor) const;
    JvmDispatchResult dispatchTransformed(
        const std::string& owner, const std::string& name,
        const std::string& descriptor, std::uint64_t receiverHandle,
        const std::vector<JvmValue>& arguments = {});

    // Methods called by the registered NativeBridge functions.  They are
    // public only to keep JNI glue independent from the private PImpl.
    std::uint64_t nativeServerHandle() const;
    std::int64_t nativeCurrentTick() const;
    bool nativePlayerHandleLive(std::uint64_t handle) const;
    bool nativeEntityHandleCreatable(std::uint64_t handle) const;
    bool nativeEntityBeginRetirement(std::uint64_t handle);
    std::string nativePlayerName(std::uint64_t handle) const;
    std::string nativePlayerUuid(std::uint64_t handle) const;
    std::int32_t nativePlayerEntityId(std::uint64_t handle) const;
    std::int32_t nativePlayerGameMode(std::uint64_t handle) const;
    bool nativePlayerSneaking(std::uint64_t handle) const;
    std::int32_t nativeOnlinePlayerCount() const;
    std::uint64_t nativeOnlinePlayerHandle(std::size_t index);
    std::int32_t nativePlayerHeldSlot(std::uint64_t handle) const;
    std::int32_t nativePlayerInventoryItemId(std::uint64_t handle,
                                             std::int32_t slot) const;
    std::int32_t nativePlayerInventoryItemCount(std::uint64_t handle,
                                                std::int32_t slot) const;
    std::string nativePlayerInventoryItemName(std::uint64_t handle,
                                              std::int32_t slot) const;
    bool nativePlayerSetInventoryItemCount(std::uint64_t handle,
                                           std::int32_t slot,
                                           std::int32_t count);
    bool nativePlayerSetInventoryItem(std::uint64_t handle,
                                      std::int32_t slot,
                                      std::int32_t itemId,
                                      std::int32_t count);
    double nativePlayerCoordinate(std::uint64_t handle, int axis) const;
    bool nativePlayerSetPosition(std::uint64_t handle, double x, double y,
                                 double z);
    bool nativePlayerTeleportToWorld(std::uint64_t playerHandle,
                                     std::uint64_t worldHandle,
                                     double x, double y, double z,
                                     float yaw, float pitch);
    bool nativePlayerSendMessage(std::uint64_t handle, const std::string& text,
                                 bool overlay);
    std::uint64_t nativePlayerWorld(std::uint64_t handle) const;
    std::string nativeEntityType(std::uint64_t handle) const;
    std::int32_t nativeEntityTypeId(std::uint64_t handle) const;
    bool nativeEntityIsLiving(std::uint64_t handle) const;
    bool nativeEntityIsHostile(std::uint64_t handle) const;
    bool nativeEntityIsAnimal(std::uint64_t handle) const;
    bool nativeEntityIsFish(std::uint64_t handle) const;
    float nativeEntityHealth(std::uint64_t handle) const;
    float nativeEntityMaxHealth(std::uint64_t handle) const;
    bool nativeEntitySetHealth(std::uint64_t handle, float health);
    bool nativeEntityDamage(std::uint64_t handle, float amount,
                            const std::string& cause);
    bool nativeEntityRemove(std::uint64_t handle, int reason);
    bool nativeEntityDead(std::uint64_t handle) const;
    std::uint64_t nativeEntityWorld(std::uint64_t handle) const;
    double nativeEntityCoordinate(std::uint64_t handle, int axis) const;
    bool nativeEntitySetPosition(std::uint64_t handle, double x, double y,
                                 double z);
    float nativeEntityRotation(std::uint64_t handle, int axis) const;
    bool nativeEntitySetRotation(std::uint64_t handle, int axis,
                                 float degrees);
    std::int32_t nativeEntityCount() const;
    std::uint64_t nativeEntityHandle(std::size_t index);
    std::uint64_t nativeServerWorld(int dimension) const;
    std::string nativeWorldName(std::uint64_t handle) const;
    bool nativeWorldHasChunk(std::uint64_t handle, std::int32_t chunkX,
                             std::int32_t chunkZ) const;
    std::int32_t nativeWorldBlock(std::uint64_t handle, std::int32_t x,
                                  std::int32_t y, std::int32_t z) const;
    bool nativeWorldSetBlock(std::uint64_t handle, std::int32_t x,
                             std::int32_t y, std::int32_t z,
                             std::int32_t state, std::int32_t flags = 3,
                             std::int32_t maxUpdateDepth = 512);
    std::int64_t nativeWorldTime(std::uint64_t handle) const;
    std::int32_t nativeRegistryItemId(const std::string& name) const;
    std::string nativeRegistryItemName(std::int32_t id) const;
    std::int32_t nativeRegistryBlockState(const std::string& name) const;
    std::string nativeRegistryBlockName(std::int32_t state) const;
    std::int32_t nativeRegistryEntryCount(const std::string& registry) const;
    std::string nativeRegistryEntryName(const std::string& registry,
                                        std::int32_t id) const;
    bool nativePlayerSendPluginMessage(
        std::uint64_t handle, const std::string& channel,
        const std::vector<std::uint8_t>& payload, int phase);
    bool nativePlayerDisconnect(std::uint64_t handle,
                                const std::string& reason);
    std::string nativeServerSetting(const std::string& key) const;
    bool nativeExecuteCommand(const std::string& command);
    void nativeLog(const std::string& level, const std::string& message) const;
    void nativeSetModStats(std::size_t discovered, std::size_t initialized);
    void nativeRegisterTransformedMethod(const std::string& owner,
                                         const std::string& name,
                                         const std::string& descriptor);
    int nativeRoutePath(const std::string& owner, const std::string& name,
                        const std::string& descriptor) const;
    std::uint64_t nativeRouteHash(const std::string& owner,
                                  const std::string& name,
                                  const std::string& descriptor) const;

    // Called by the native KnotLauncher.installBridge(Class<?>) method.  The
    // erased parameters are JNIEnv*/jclass when JNI is enabled.
    bool installNativeBridge(void* rawEnv, void* rawClass);

    // JNI call helpers live outside the PImpl implementation so that the
    // C-linkage bridge stays small.  Keep the access narrow and explicit.
    Impl& bridgeImpl() noexcept { return *impl_; }
    const Impl& bridgeImpl() const noexcept { return *impl_; }

private:
    bool dispatchFinalLifecycleCallback(std::uint8_t stage);
    std::unique_ptr<Impl> impl_;
};

// Owns the provisional Java handle created for a spawn callback. Every early
// return before roster publication invalidates it automatically; commit only
// after the native entity has been registered successfully.
using MobSpawnAdmissionRollback =
    void (*)(JvmRuntime&, MobEntity&) noexcept;

class MobSpawnAdmission final {
public:
    MobSpawnAdmission(JvmRuntime* runtime, MobEntity& mob,
                      MobSpawnAdmissionRollback rollback = nullptr) noexcept
        : runtime_(runtime), mob_(&mob), rollback_(rollback) {}

    ~MobSpawnAdmission() noexcept {
        if (rollbackArmed_ && runtime_) {
            if (rollback_)
                rollback_(*runtime_, *mob_);
            else
                runtime_->cancelMobSpawn(*mob_);
        }
    }

    MobSpawnAdmission(const MobSpawnAdmission&) = delete;
    MobSpawnAdmission& operator=(const MobSpawnAdmission&) = delete;

    bool approve(double x, double y, double z) {
        if (attempted_) return false;
        attempted_ = true;
        if (!runtime_) {
            accepted_ = true;
            return true;
        }
        // Arm rollback before onMobSpawn: handle publication precedes several
        // allocating lifecycle operations that may throw.
        rollbackArmed_ = true;
        accepted_ = runtime_->onMobSpawn(*mob_, x, y, z);
        return accepted_;
    }

    void commit() noexcept {
        accepted_ = false;
        rollbackArmed_ = false;
    }

private:
    JvmRuntime* runtime_;
    MobEntity* mob_;
    MobSpawnAdmissionRollback rollback_;
    bool attempted_ = false;
    bool accepted_ = false;
    bool rollbackArmed_ = false;
};

} // namespace cppfm::jvm
