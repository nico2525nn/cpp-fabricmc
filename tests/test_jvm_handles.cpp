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
#include <vector>

static_assert(std::is_same_v<
              decltype(std::declval<cppfm::Player&>().inPlay),
              std::atomic<bool>>);

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
    check((replacement >> 48U) == 1,
          "monotonic non-recycled ids need no retained per-address generation");
    check(handles.valid(replacement, HandleKind::Player), "replacement resolves");
    auto initialBorrow = handles.acquire(replacement, HandleKind::Player);
    auto transferredBorrow = std::move(initialBorrow);
    check(transferredBorrow.get() == &object,
          "scoped handle borrow retains the address and lock after move");
    check(!initialBorrow && initialBorrow.get() == nullptr,
          "moved-from scoped handle borrow cannot expose an unprotected pointer");
    transferredBorrow = {};
    check(handles.valid(replacement, HandleKind::Player),
          "releasing the scoped borrow allows later handle access");
    int neverRegistered = 0;
    check(!handles.invalidate(&neverRegistered, HandleKind::Entity),
          "invalidating an unknown address does not mint a handle");

    NativeHandleTable churnHandles;
    std::vector<int> churnObjects(4096);
    bool churnSucceeded = true;
    for (auto& churnObject : churnObjects) {
        const auto handle = churnHandles.registerObject(
            &churnObject, HandleKind::Entity);
        churnSucceeded = churnSucceeded && handle != 0 &&
            churnHandles.invalidate(&churnObject, HandleKind::Entity);
    }
    check(churnSucceeded && churnHandles.size() == 0,
          "high-churn handle retirement leaves no live address associations");

    int admittedPlayer = 0;
    NativeHandleTable playerHandles;
    cppfm::jvm::PlayerHandleLifetime playerLifetime(playerHandles);
    std::atomic<std::uint8_t> admittedLifecycle{0};
    std::atomic<bool> admitted{true};
    const auto admittedHandle = playerLifetime.activate(
        &admittedPlayer, admittedLifecycle,
        [&] { return admitted.load(std::memory_order_acquire); });
    check(admittedHandle != 0, "admitted player activation publishes a handle");
    check(playerLifetime.isLiveHandle(admittedHandle),
          "only an admitted player handle is eligible for a Java wrapper");
    check(playerLifetime.publish(&admittedPlayer, admittedLifecycle,
                                 [] { return true; }) ==
              admittedHandle,
          "live callbacks reuse a player's opaque handle");
    check(playerLifetime.beginRetirement(&admittedPlayer, admittedLifecycle) ==
              admittedHandle,
          "retirement preserves the current handle for the quit callback");
    check(!playerLifetime.isLiveHandle(admittedHandle),
          "retiring handles remain usable for the cached quit wrapper but cannot create wrappers");
    check(playerLifetime.publish(&admittedPlayer, admittedLifecycle,
                                 [] { return true; }) == 0,
          "retiring players cannot publish a replacement handle");
    check(!playerLifetime.finishRetirement(&admittedPlayer,
                                           admittedLifecycle),
          "retirement tombstone remains while the old handle is valid");
    check(playerLifetime.retire(&admittedPlayer, admittedLifecycle) ==
              admittedHandle,
          "final retirement invalidates the old handle");
    check(!playerHandles.valid(admittedHandle),
          "a retired player handle no longer resolves");
    check(!playerLifetime.isLiveHandle(admittedHandle),
          "invalidated handles cannot create a replacement Java wrapper");
    check(playerLifetime.publish(&admittedPlayer, admittedLifecycle,
                                 [] { return true; }) == 0,
          "a delayed callback cannot reactivate a retired player");
    check(playerLifetime.finishRetirement(&admittedPlayer, admittedLifecycle),
          "roster removal releases the retired-player tombstone");
    std::atomic<std::uint8_t> reactivatedLifecycle{0};
    const auto reactivatedHandle = playerLifetime.activate(
        &admittedPlayer, reactivatedLifecycle, [] { return true; });
    check(reactivatedHandle != 0 && reactivatedHandle != admittedHandle &&
              !playerLifetime.isLiveHandle(admittedHandle) &&
              playerLifetime.isLiveHandle(reactivatedHandle),
          "address reuse publishes a new live identity without reviving the stale handle");
    playerLifetime.retire(&admittedPlayer, reactivatedLifecycle);
    playerLifetime.finishRetirement(&admittedPlayer, reactivatedLifecycle);

    int neverAdmittedPlayer = 0;
    std::atomic<std::uint8_t> neverAdmittedLifecycle{0};
    check(playerLifetime.beginRetirement(&neverAdmittedPlayer,
                                         neverAdmittedLifecycle) == 0 &&
              playerLifetime.activate(&neverAdmittedPlayer,
                                      neverAdmittedLifecycle,
                                      [] { return true; }) == 0,
          "pending retirement blocks first handle publication even before admission");
    check(playerLifetime.finishRetirement(&neverAdmittedPlayer,
                                          neverAdmittedLifecycle) &&
              playerLifetime.activate(&neverAdmittedPlayer,
                                      neverAdmittedLifecycle,
                                      [] { return true; }) == 0,
          "finishing an unpublished retirement removes its marker and closes the old lifecycle");

    int activationRacePlayer = 0;
    NativeHandleTable activationRaceHandles;
    cppfm::jvm::PlayerHandleLifetime activationRaceLifetime(
        activationRaceHandles);
    std::atomic<std::uint8_t> activationRaceLifecycle{0};
    std::atomic<bool> activationRaceLive{true};
    std::latch activationBeforeLock(1);
    std::latch allowActivationToLock(1);
    std::uint64_t activationAfterRetirement = 0;
    std::thread delayedActivation([&] {
        activationAfterRetirement = activationRaceLifetime.activate(
            &activationRacePlayer, activationRaceLifecycle,
            [&] { return activationRaceLive.load(std::memory_order_acquire); },
            [&] {
                activationBeforeLock.count_down();
                allowActivationToLock.wait();
            });
    });
    activationBeforeLock.wait();
    activationRaceLive.store(false, std::memory_order_release);
    (void)activationRaceLifetime.beginRetirement(&activationRacePlayer,
                                                  activationRaceLifecycle);
    const auto activationRaceRetired =
        activationRaceLifetime.retire(&activationRacePlayer,
                                      activationRaceLifecycle);
    const bool activationRaceFinished =
        activationRaceLifetime.finishRetirement(&activationRacePlayer,
                                                 activationRaceLifecycle);
    allowActivationToLock.count_down();
    delayedActivation.join();
    check(activationRaceRetired == std::nullopt && activationRaceFinished &&
              activationAfterRetirement == 0 && activationRaceHandles.size() == 0,
          "activation revalidates roster membership after retirement wins the lock");

    int delayedRetirementPlayer = 0;
    NativeHandleTable delayedRetirementHandles;
    cppfm::jvm::PlayerHandleLifetime delayedRetirementLifetime(
        delayedRetirementHandles);
    std::atomic<std::uint8_t> delayedLifecycle{0};
    const auto delayedRetirementHandle = delayedRetirementLifetime.activate(
        &delayedRetirementPlayer, delayedLifecycle, [] { return true; });
    std::latch retirementBeforeLock(1);
    std::latch allowRetirementToLock(1);
    std::uint64_t delayedBeginResult = 0;
    std::thread delayedBegin([&] {
        delayedBeginResult = delayedRetirementLifetime.beginRetirement(
            &delayedRetirementPlayer, delayedLifecycle, [&] {
                retirementBeforeLock.count_down();
                allowRetirementToLock.wait();
            });
    });
    retirementBeforeLock.wait();
    check(delayedRetirementLifetime.beginRetirement(&delayedRetirementPlayer,
                                                    delayedLifecycle) ==
              delayedRetirementHandle,
          "active retirement retains the handle for its quit callback");
    check(delayedRetirementLifetime.retire(&delayedRetirementPlayer,
                                           delayedLifecycle) ==
              delayedRetirementHandle &&
              delayedRetirementLifetime.finishRetirement(
                  &delayedRetirementPlayer, delayedLifecycle),
          "retirement completes while a duplicate begin call is delayed");
    allowRetirementToLock.count_down();
    delayedBegin.join();
    std::atomic<std::uint8_t> reusedLifecycle{0};
    const auto reusedAfterLateBegin = delayedRetirementLifetime.activate(
        &delayedRetirementPlayer, reusedLifecycle, [] { return true; });
    check(delayedBeginResult == 0 && reusedAfterLateBegin != 0 &&
              reusedAfterLateBegin != delayedRetirementHandle &&
              delayedRetirementHandles.valid(reusedAfterLateBegin),
          "a begin call delayed across finish cannot poison a reused address");
    delayedRetirementLifetime.retire(&delayedRetirementPlayer,
                                     reusedLifecycle);
    delayedRetirementLifetime.finishRetirement(&delayedRetirementPlayer,
                                              reusedLifecycle);

    int concurrentPlayer = 0;
    NativeHandleTable concurrentHandles;
    cppfm::jvm::PlayerHandleLifetime concurrentLifetime(concurrentHandles);
    std::atomic<std::uint8_t> concurrentLifecycle{0};
    const auto concurrentHandle = concurrentLifetime.activate(
        &concurrentPlayer, concurrentLifecycle, [] { return true; });
    std::atomic<bool> observeLiveHandles{true};
    std::atomic<std::size_t> successfulLivenessChecks{0};
    std::latch livenessCheckStarted(1);
    std::thread livenessObserver([&] {
        bool firstCheck = true;
        while (observeLiveHandles.load(std::memory_order_acquire)) {
            const bool live = concurrentLifetime.isLiveHandle(concurrentHandle);
            if (live)
                successfulLivenessChecks.fetch_add(1,
                                                   std::memory_order_relaxed);
            if (firstCheck) {
                livenessCheckStarted.count_down();
                firstCheck = false;
            }
            std::this_thread::yield();
        }
    });
    livenessCheckStarted.wait();
    const auto concurrentlyRetired =
        concurrentLifetime.retire(&concurrentPlayer, concurrentLifecycle);
    observeLiveHandles.store(false, std::memory_order_release);
    livenessObserver.join();
    check(concurrentlyRetired == concurrentHandle &&
              successfulLivenessChecks.load(std::memory_order_relaxed) > 0 &&
              !concurrentLifetime.isLiveHandle(concurrentHandle) &&
              concurrentLifetime.finishRetirement(&concurrentPlayer,
                                                  concurrentLifecycle),
          "concurrent liveness checks finish safely and reject the retired handle");

    int disconnectPlayer = 0;
    NativeHandleTable disconnectHandles;
    cppfm::jvm::PlayerHandleLifetime disconnectLifetime(disconnectHandles);
    std::atomic<std::uint8_t> disconnectLifecycle{0};
    const auto disconnectHandle = disconnectLifetime.activate(
        &disconnectPlayer, disconnectLifecycle, [] { return true; });
    check(disconnectLifetime.beginRetirementForHandle(
              &disconnectPlayer, disconnectHandle, disconnectLifecycle),
          "disconnect retires the exact currently published handle");
    check(!disconnectLifetime.beginRetirementForHandle(
              &disconnectPlayer, disconnectHandle, disconnectLifecycle),
          "duplicate disconnect cannot re-open or re-retire a tombstone");
    check(disconnectLifetime.retire(&disconnectPlayer,
                                    disconnectLifecycle) == disconnectHandle,
          "disconnect retirement keeps the existing quit-callback handle");
    check(disconnectLifetime.finishRetirement(&disconnectPlayer,
                                              disconnectLifecycle),
          "disconnect tombstone clears after handle invalidation and removal");
    check(!disconnectLifetime.beginRetirementForHandle(
              &disconnectPlayer, disconnectHandle, disconnectLifecycle),
          "late disconnect cannot recreate a completed retirement tombstone");

    cppfm::Player cachedPlayer;
    cachedPlayer.inPlay.store(true, std::memory_order_release);
    NativeHandleTable cachedHandles;
    cppfm::jvm::PlayerHandleLifetime cachedLifetime(cachedHandles);
    const auto cachedHandle = cachedLifetime.activate(
        &cachedPlayer, cachedPlayer.jvmHandleLifecycle, [&] {
            return cachedPlayer.inPlay.load(std::memory_order_acquire);
        });
    std::mutex objectCacheMutex;
    cppfm::jvm::JavaObjectCache objectCache;
    int cachedReference = 0;
    constexpr char wrapperType[] = "net/minecraft/Player";
    objectCache.put(nullptr, cachedHandle, wrapperType, &cachedReference);
    std::latch initialCheckPassed(1);
    std::latch allowCacheLookup(1);
    std::latch retirementLinearized(1);
    std::atomic<bool> cacheHitAfterRetirement{false};
    std::thread lookup([&] {
        {
            std::lock_guard lock(objectCacheMutex);
            if (!cachedHandles.valid(cachedHandle)) return;
        }
        initialCheckPassed.count_down();
        allowCacheLookup.wait();
        std::lock_guard lock(objectCacheMutex);
        if (cachedHandles.valid(cachedHandle)) {
            objectCache.with(cachedHandle, wrapperType, [&](void*) {
                cacheHitAfterRetirement.store(true, std::memory_order_release);
            });
        }
    });
    initialCheckPassed.wait();
    std::thread retirement([&] {
        cppfm::jvm::invalidateHandleAndEraseCache(
            objectCacheMutex,
            [&] {
                auto handle = cachedLifetime.retire(
                    &cachedPlayer, cachedPlayer.jvmHandleLifecycle);
                retirementLinearized.count_down();
                return handle;
            },
            [&](std::uint64_t handle) { objectCache.erase(handle); });
    });
    retirementLinearized.wait();
    allowCacheLookup.count_down();
    lookup.join();
    retirement.join();
    check(!cacheHitAfterRetirement.load(std::memory_order_acquire),
          "cache lookup cannot succeed after retirement linearizes");
    check(!cachedHandles.valid(cachedHandle) && objectCache.size() == 0,
          "retirement invalidates the handle and erases its cached wrapper");

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
