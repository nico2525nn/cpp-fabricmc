// Embedded Fabric-compatible JVM runtime boundary.
//
// The runtime is deliberately optional: the normal cppfm executable remains
// C++-only unless `jvm-enabled=true`/`--jvm=true` is supplied.  JNI details,
// Java references, and class-loader state stay behind this interface so game
// code only sees typed lifecycle/event calls.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

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
    // reported and the C++ server may continue without the optional layer.
    bool start(std::string* error = nullptr);
    void stop();
    bool started() const noexcept;
    const std::string& lastError() const noexcept;
    JvmStats stats() const;

    // Synchronous lifecycle and cancellable event boundaries.  A disabled or
    // unavailable runtime is an allow/no-op result so existing C++ behavior is
    // unchanged unless the layer is explicitly active.
    bool onServerTick(std::int64_t tick);
    void onPlayerJoin(Player& player);
    void onPlayerQuit(Player& player);
    bool onChat(Player& player, std::string& message);
    bool onBlockBreak(Player& player, std::int32_t x, std::int32_t y,
                      std::int32_t z, std::uint16_t oldState);
    bool onBlockPlace(Player& player, std::int32_t x, std::int32_t y,
                      std::int32_t z, std::uint16_t newState);
    bool onBlockClicked(Player& player, std::int32_t x, std::int32_t y,
                        std::int32_t z, std::uint16_t state, int face);
    bool onCommand(Player* player, std::string& command);
    bool onEntityDamage(Player* victimPlayer, MobEntity* victimMob,
                        float& amount, const std::string& cause);
    bool onMobSpawn(MobEntity& mob, double x, double y, double z);

    // Handle lifetime is tied to C++ object lifetime.  The Java value is
    // opaque and generation-checked; no raw address crosses JNI.
    std::uint64_t playerHandle(Player& player);
    std::uint64_t worldHandle(World& world);
    void invalidatePlayer(Player& player);

    // Methods called by the registered NativeBridge functions.  They are
    // public only to keep JNI glue independent from the private PImpl.
    std::uint64_t nativeServerHandle() const;
    std::int64_t nativeCurrentTick() const;
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
    bool nativePlayerSendMessage(std::uint64_t handle, const std::string& text,
                                 bool overlay);
    std::uint64_t nativePlayerWorld(std::uint64_t handle) const;
    std::uint64_t nativeServerWorld(int dimension) const;
    std::string nativeWorldName(std::uint64_t handle) const;
    std::int32_t nativeWorldBlock(std::uint64_t handle, std::int32_t x,
                                  std::int32_t y, std::int32_t z) const;
    bool nativeWorldSetBlock(std::uint64_t handle, std::int32_t x,
                             std::int32_t y, std::int32_t z,
                             std::int32_t state);
    bool nativeExecuteCommand(const std::string& command);
    void nativeLog(const std::string& level, const std::string& message) const;
    void nativeSetModStats(std::size_t discovered, std::size_t initialized);
    void nativeRegisterTransformedMethod(const std::string& owner,
                                         const std::string& name,
                                         const std::string& descriptor);

    // JNI call helpers live outside the PImpl implementation so that the
    // C-linkage bridge stays small.  Keep the access narrow and explicit.
    Impl& bridgeImpl() noexcept { return *impl_; }
    const Impl& bridgeImpl() const noexcept { return *impl_; }

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace cppfm::jvm
