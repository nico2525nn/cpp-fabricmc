#include "game/Player.hpp"
#include "jvm/HandleCacheTransaction.hpp"
#include "jvm/JavaObjectCache.hpp"
#include "jvm/NativeHandleTable.hpp"
#include "jvm/ModRoutingTable.hpp"
#include "jvm/PlayerHandleLifetime.hpp"

#include <atomic>
#include <cstdio>
#include <latch>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

static_assert(std::is_same_v<
              decltype(std::declval<cppfm::Player&>().inPlay),
              std::atomic<bool>>,
              "Player::inPlay must be atomic across session/JVM threads");

using cppfm::jvm::DispatchPath;
using cppfm::jvm::HandleKind;
using cppfm::jvm::ModRoutingTable;
using cppfm::jvm::NativeHandleTable;

int main() {
    int failures = 0;
    auto check = [&](bool condition, const char* name) {
        if (!condition) {
            std::fprintf(stderr, "FAIL %s\n", name);
            ++failures;
        }
    };

    int object = 0;
    NativeHandleTable handles;
    const auto player = handles.registerObject(&object, HandleKind::Player);
    check(player != 0, "register player");
    check(handles.registerObject(&object, HandleKind::Player) == player,
          "same live object has stable identity");
    check(handles.findHandle(&object, HandleKind::Player).value_or(0) == player,
          "address lookup returns the live opaque handle");
    check(handles.valid(player, HandleKind::Player), "live handle resolves");
    check(!handles.valid(player, HandleKind::World), "kind mismatch fails closed");
    check(handles.invalidate(&object, HandleKind::Player), "invalidate live handle");
    check(!handles.findHandle(&object, HandleKind::Player).has_value(),
          "address lookup is empty after invalidation");
    check(!handles.valid(player), "stale handle fails after invalidation");
    const auto replacement = handles.registerObject(&object, HandleKind::Player);
    check(replacement != player, "address reuse gets a new opaque id");
    check(handles.valid(replacement, HandleKind::Player), "replacement resolves");
    int neverRegistered = 0;
    check(!handles.invalidate(&neverRegistered, HandleKind::Entity),
          "invalidating an unknown address does not mint a handle");

    int admittedPlayer = 0;
    NativeHandleTable playerHandles;
    cppfm::jvm::PlayerHandleLifetime playerLifetime(playerHandles);
    std::atomic<bool> admitted{true};
    const auto admittedHandle = playerLifetime.activate(
        &admittedPlayer, [&] { return admitted.load(std::memory_order_acquire); });
    check(admittedHandle != 0, "admitted player activation publishes a handle");
    check(playerLifetime.publish(&admittedPlayer, [] { return true; }) ==
              admittedHandle,
          "live player callback reuses its published handle");
    admitted.store(false, std::memory_order_release);
    check(playerLifetime.publish(&admittedPlayer, [&] {
              return admitted.load(std::memory_order_acquire);
          }) == 0,
          "player outside the live roster cannot publish a handle");
    admitted.store(true, std::memory_order_release);
    check(playerLifetime.publish(&admittedPlayer, [&] {
              return admitted.load(std::memory_order_acquire);
          }) == admittedHandle,
          "live player can reuse its handle after a transient live check");
    check(playerLifetime.beginRetirement(&admittedPlayer) == admittedHandle,
          "retirement retains the existing handle for the quit callback");
    check(playerLifetime.publish(&admittedPlayer, [] { return true; }) == 0,
          "retiring player rejects callback handle publication");
    check(!playerLifetime.finishRetirement(&admittedPlayer),
          "retirement tombstone cannot clear while its handle is still valid");
    const auto invalidatedHandle = playerLifetime.retire(&admittedPlayer);
    check(invalidatedHandle == admittedHandle,
          "final retirement invalidates the published player handle");
    check(!playerHandles.valid(admittedHandle),
          "retired player handle no longer resolves");
    check(playerLifetime.publish(&admittedPlayer, [] { return true; }) == 0,
          "post-invalidation callback cannot re-register a player handle");
    check(playerLifetime.finishRetirement(&admittedPlayer),
          "retirement tombstone is released after roster removal");

    int notYetActivatedPlayer = 0;
    NativeHandleTable notYetActivatedHandles;
    cppfm::jvm::PlayerHandleLifetime notYetActivatedLifetime(
        notYetActivatedHandles);
    check(notYetActivatedLifetime.beginRetirement(&notYetActivatedPlayer) == 0,
          "retirement closes publication even before first handle activation");
    check(notYetActivatedLifetime.activate(
              &notYetActivatedPlayer, [] { return true; }) == 0,
          "delayed join callback cannot reactivate a retiring player");
    check(!notYetActivatedLifetime.retire(&notYetActivatedPlayer).has_value(),
          "unpublished retiring player completes retirement without a handle");
    check(notYetActivatedLifetime.finishRetirement(&notYetActivatedPlayer),
          "unpublished player tombstone is released after roster removal");

    cppfm::Player racedPlayer;
    racedPlayer.inPlay.store(true, std::memory_order_release);
    NativeHandleTable racedHandles;
    cppfm::jvm::PlayerHandleLifetime racedLifetime(racedHandles);
    std::atomic<bool> rosterMember{true};
    const auto racedHandle = racedLifetime.activate(
        &racedPlayer, [&] {
            return rosterMember.load(std::memory_order_acquire) &&
                   racedPlayer.inPlay.load(std::memory_order_acquire);
        });
    std::latch callbackReady(1);
    std::latch allowPublication(1);
    std::atomic<std::uint64_t> callbackHandle{0};
    std::thread callback([&] {
        callbackReady.count_down();
        allowPublication.wait();
        callbackHandle.store(
            racedLifetime.publish(&racedPlayer, [&] {
                // Deliberately model the stale roster-membership-only check:
                // the player remains in PlayerDirectory until after quit.
                return rosterMember.load(std::memory_order_acquire);
            }),
            std::memory_order_release);
    });
    callbackReady.wait();
    check(racedLifetime.beginRetirement(&racedPlayer) == racedHandle,
          "concurrent callback observes the retiring player's old handle");
    racedPlayer.inPlay.store(false, std::memory_order_release);
    check(racedLifetime.beginRetirement(&racedPlayer) == racedHandle &&
              racedHandles.valid(racedHandle),
          "quit callback retains a valid handle after inPlay becomes false");
    check(racedLifetime.retire(&racedPlayer) == racedHandle,
          "quit callback return invalidates while roster membership remains");
    allowPublication.count_down();
    callback.join();
    check(callbackHandle.load(std::memory_order_acquire) == 0,
          "callback delayed across retirement cannot publish a fresh handle");
    check(racedLifetime.activate(&racedPlayer, [&] {
              // Even an explicit activation cannot trust roster membership
              // alone during the inPlay=false / directory-removal window.
              return rosterMember.load(std::memory_order_acquire);
          }) == 0,
          "retired tombstone blocks stale join activation before roster removal");
    check(!racedHandles.valid(racedHandle),
          "raced retirement leaves no resolvable player handle");
    rosterMember.store(false, std::memory_order_release);
    check(racedLifetime.finishRetirement(&racedPlayer),
          "directory removal permits final tombstone cleanup");

    cppfm::Player cachedPlayer;
    cachedPlayer.inPlay.store(true, std::memory_order_release);
    NativeHandleTable cachedHandles;
    cppfm::jvm::PlayerHandleLifetime cachedLifetime(cachedHandles);
    const auto cachedHandle = cachedLifetime.activate(
        &cachedPlayer, [&] {
            return cachedPlayer.inPlay.load(std::memory_order_acquire);
        });
    std::mutex objectCacheMutex;
    cppfm::jvm::JavaObjectCache objectCache;
    int cachedReference = 0;
    constexpr char wrapperType[] =
        "net/minecraft/server/network/ServerPlayerEntity";
    objectCache.put(nullptr, cachedHandle, wrapperType, &cachedReference);
    std::latch passedInitialCheck(1);
    std::latch continueCacheLookup(1);
    std::latch retiredWhileCacheLocked(1);
    std::latch allowCacheErase(1);
    std::latch secondCheckStarting(1);
    std::atomic<bool> cacheHitAfterRetirement{false};
    std::thread wrapperLookup([&] {
        {
            std::lock_guard lock(objectCacheMutex);
            check(cachedHandles.valid(cachedHandle),
                  "wrapper's initial handle check passes before retirement");
        }
        passedInitialCheck.count_down();
        continueCacheLookup.wait();
        secondCheckStarting.count_down();
        std::lock_guard lock(objectCacheMutex);
        if (cachedHandles.valid(cachedHandle)) {
            objectCache.with(cachedHandle, wrapperType, [&](void*) {
                cacheHitAfterRetirement.store(true, std::memory_order_release);
            });
        }
    });
    passedInitialCheck.wait();
    std::thread cacheRetirement([&] {
        cppfm::jvm::invalidateHandleAndEraseCache(
            objectCacheMutex,
            [&] {
                const auto handle = cachedLifetime.retire(&cachedPlayer);
                retiredWhileCacheLocked.count_down();
                allowCacheErase.wait();
                return handle;
            },
            [&](std::uint64_t handle) { objectCache.erase(handle); });
    });
    retiredWhileCacheLocked.wait();
    continueCacheLookup.count_down();
    secondCheckStarting.wait();
    allowCacheErase.count_down();
    wrapperLookup.join();
    cacheRetirement.join();
    check(!cacheHitAfterRetirement.load(std::memory_order_acquire),
          "wrapper cannot hit cached object after handle retirement linearizes");
    check(!cachedHandles.valid(cachedHandle) && objectCache.size() == 0,
          "retirement atomically invalidates handle and erases cached wrapper");

    ModRoutingTable routing;
    const std::string serverOwner = "net.minecraft.server.MinecraftServer";
    routing.markTransformed(serverOwner, "setTick", "(J)V", 101);
    check(routing.path("net/minecraft/server/MinecraftServer", "setTick", "(J)V") ==
              DispatchPath::JvmTransformed,
          "MinecraftServer tick route selects transformed body");
    routing.markNative(serverOwner, "setTick", "(J)V", 100);
    check(routing.path(serverOwner, "setTick", "(J)V") == DispatchPath::NativeFast,
          "MinecraftServer tick route restores native path");
    check(routing.path("net/minecraft/World", "tick", "()V") ==
              DispatchPath::NativeFast,
          "unmodified method uses native fast path");
    routing.markTransformed("net/minecraft/World", "tick", "()V");
    check(routing.path("net/minecraft/World", "tick", "()V") ==
              DispatchPath::JvmTransformed,
          "transformed method routes through JVM");
    routing.markNative("net/minecraft/World", "tick", "()V");
    check(routing.path("net/minecraft/World", "tick", "()V") ==
              DispatchPath::NativeFast,
          "native override restores fast path");

    std::printf("JVM handle/routing checks: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
