#include "game/BlockEvent.hpp"
#include "game/DatapackManager.hpp"

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>

using namespace cppfm;

namespace {
int passed = 0;
int failed = 0;

void check(bool condition, const char* name) {
    if (condition) {
        ++passed;
        std::printf("PASS %s\n", name);
    } else {
        ++failed;
        std::printf("FAIL %s\n", name);
    }
}
}

int main() {
    BlockEventDispatcher dispatcher;
    int legacyCalls = 0;
    dispatcher.addOnBlockPlaceHandler([&](const BlockEvent& event) {
        ++legacyCalls;
        check(event.type == BlockEvent::Type::Place && event.oldState == 2 &&
                  event.newState == 3 && event.x == 4 && event.y == 5 &&
                  event.z == 6,
              "place dispatch preserves legacy event fields");
    });
    check(dispatcher.onBlockPlace(4, 5, 6, 2, 3),
          "place dispatch allows non-cancelled hook path");
    check(legacyCalls == 1, "place dispatch invokes one legacy snapshot");

    auto cancellation = dispatcher.breakHook().subscribeScoped(
        0, [](BlockBreakBlockEvent& event) { event.cancel(); });
    check(!dispatcher.onBlockBreak(1, 2, 3, 7),
          "break dispatch returns false when hook cancels");
    cancellation.reset();

    dispatcher.addOnBlockClickedHandler(
        [](const BlockEvent&) { throw 1; });
    check(!dispatcher.onBlockClicked(1, 2, 3, 4, 5),
          "clicked dispatch fails closed when legacy handler throws");

    int staleCalls = 0;
    dispatcher.addOnBlockPlaceHandler([&](const BlockEvent&) { ++staleCalls; });
    dispatcher.clearLegacyHandlers();
    check(dispatcher.onBlockPlace(1, 2, 3, 0, 1) && staleCalls == 0,
          "legacy callbacks are removable before runtime unload");

    std::mutex callbackMutex;
    std::condition_variable callbackCv;
    bool callbackEntered = false;
    bool releaseCallback = false;
    dispatcher.addOnBlockPlaceHandler([&](const BlockEvent&) {
        std::unique_lock lock(callbackMutex);
        callbackEntered = true;
        callbackCv.notify_all();
        callbackCv.wait(lock, [&] { return releaseCallback; });
    });
    std::thread dispatchThread([&] { dispatcher.onBlockPlace(1, 2, 3, 0, 1); });
    {
        std::unique_lock lock(callbackMutex);
        callbackCv.wait(lock, [&] { return callbackEntered; });
    }
    auto clearFuture = std::async(std::launch::async,
                                  [&] { dispatcher.clearLegacyHandlers(); });
    check(clearFuture.wait_for(std::chrono::milliseconds(20)) ==
              std::future_status::timeout,
          "legacy unload waits for an in-flight callback");
    {
        std::lock_guard lock(callbackMutex);
        releaseCallback = true;
    }
    callbackCv.notify_all();
    dispatchThread.join();
    check(clearFuture.wait_for(std::chrono::seconds(1)) ==
              std::future_status::ready,
          "legacy unload completes after callback release");

    struct ConcurrentEvent {};
    struct ConcurrentHandler {
        explicit ConcurrentHandler(std::atomic<int>& arrivedCount,
                                   std::atomic<int>& overlapCount)
            : arrived(&arrivedCount), overlaps(&overlapCount) {}
        ConcurrentHandler(const ConcurrentHandler& other)
            : arrived(other.arrived), overlaps(other.overlaps) {}
        void operator()(ConcurrentEvent&) {
            if (inCall.test_and_set(std::memory_order_acquire))
                overlaps->fetch_add(1, std::memory_order_relaxed);
            arrived->fetch_add(1, std::memory_order_release);
            while (arrived->load(std::memory_order_acquire) < 2)
                std::this_thread::yield();
            inCall.clear(std::memory_order_release);
        }
        std::atomic_flag inCall = ATOMIC_FLAG_INIT;
        std::atomic<int>* arrived;
        std::atomic<int>* overlaps;
    };
    api::EventHook<ConcurrentEvent> concurrentHook;
    std::atomic<int> concurrentArrivals{0};
    std::atomic<int> concurrentOverlaps{0};
    auto concurrentSubscription = concurrentHook.subscribeScoped(
        0, ConcurrentHandler{concurrentArrivals, concurrentOverlaps});
    std::atomic<bool> startConcurrentFires{false};
    const auto fireConcurrently = [&] {
        while (!startConcurrentFires.load(std::memory_order_acquire))
            std::this_thread::yield();
        ConcurrentEvent event;
        concurrentHook.fire(event);
    };
    std::thread firstFire(fireConcurrently);
    std::thread secondFire(fireConcurrently);
    startConcurrentFires.store(true, std::memory_order_release);
    firstFire.join();
    secondFire.join();
    check(concurrentArrivals.load(std::memory_order_acquire) == 2 &&
              concurrentOverlaps.load(std::memory_order_relaxed) == 0,
          "concurrent fires invoke independent handler snapshots");
    concurrentSubscription.reset();

    struct LifecycleEvent { bool block = false; };
    struct TrackedHandler {
        TrackedHandler(std::atomic<int>& destructionCount,
                       std::atomic<int>& callCount, std::mutex& handlerMutex,
                       std::condition_variable& handlerCv, bool& enteredFlag,
                       bool& releaseFlag)
            : destroyed(&destructionCount), calls(&callCount),
              mutex(&handlerMutex), cv(&handlerCv), entered(&enteredFlag),
              release(&releaseFlag) {}
        TrackedHandler(const TrackedHandler& other)
            : destroyed(other.destroyed), calls(other.calls), mutex(other.mutex),
              cv(other.cv), entered(other.entered), release(other.release) {}
        ~TrackedHandler() {
            destroyed->fetch_add(1, std::memory_order_relaxed);
        }
        void operator()(LifecycleEvent& event) {
            calls->fetch_add(1, std::memory_order_relaxed);
            if (!event.block) return;
            std::unique_lock lock(*mutex);
            *entered = true;
            cv->notify_all();
            cv->wait(lock, [&] { return *release; });
        }
        std::atomic<int>* destroyed;
        std::atomic<int>* calls;
        std::mutex* mutex;
        std::condition_variable* cv;
        bool* entered;
        bool* release;
    };

    api::EventHook<LifecycleEvent> pendingSnapshotHook;
    std::mutex snapshotMutex;
    std::condition_variable snapshotCv;
    bool firstSnapshotHandlerEntered = false;
    bool releaseFirstSnapshotHandler = false;
    pendingSnapshotHook.subscribe(0, [&](LifecycleEvent& event) {
        if (!event.block) return;
        std::unique_lock lock(snapshotMutex);
        firstSnapshotHandlerEntered = true;
        snapshotCv.notify_all();
        snapshotCv.wait(lock, [&] { return releaseFirstSnapshotHandler; });
    });
    std::atomic<int> pendingHandlerDestructions{0};
    std::atomic<int> pendingHandlerCalls{0};
    auto pendingSnapshotSubscription = pendingSnapshotHook.subscribeScoped(
        1, TrackedHandler{pendingHandlerDestructions, pendingHandlerCalls,
                          snapshotMutex, snapshotCv, firstSnapshotHandlerEntered,
                          releaseFirstSnapshotHandler});
    pendingHandlerDestructions.store(0, std::memory_order_relaxed);
    std::thread pendingSnapshotFire([&] {
        LifecycleEvent event{true};
        pendingSnapshotHook.fire(event);
    });
    {
        std::unique_lock lock(snapshotMutex);
        snapshotCv.wait(lock, [&] { return firstSnapshotHandlerEntered; });
    }
    pendingSnapshotSubscription.reset();
    const int destroyedBeforePendingSnapshotRelease =
        pendingHandlerDestructions.load(std::memory_order_relaxed);
    {
        std::lock_guard lock(snapshotMutex);
        releaseFirstSnapshotHandler = true;
    }
    snapshotCv.notify_all();
    pendingSnapshotFire.join();
    check(destroyedBeforePendingSnapshotRelease == 1 &&
              pendingHandlerDestructions.load(std::memory_order_relaxed) == 1 &&
              pendingHandlerCalls.load(std::memory_order_relaxed) == 0,
          "reset destroys handlers held only by a not-yet-invoked snapshot");

    api::EventHook<LifecycleEvent> activeHandlerHook;
    std::mutex activeHandlerMutex;
    std::condition_variable activeHandlerCv;
    bool activeHandlerEntered = false;
    bool releaseActiveHandler = false;
    std::atomic<int> activeHandlerDestructions{0};
    std::atomic<int> activeHandlerCalls{0};
    auto activeHandlerSubscription = activeHandlerHook.subscribeScoped(
        0, TrackedHandler{activeHandlerDestructions, activeHandlerCalls,
                          activeHandlerMutex, activeHandlerCv,
                          activeHandlerEntered, releaseActiveHandler});
    activeHandlerDestructions.store(0, std::memory_order_relaxed);
    std::thread activeHandlerFire([&] {
        LifecycleEvent event{true};
        activeHandlerHook.fire(event);
    });
    {
        std::unique_lock lock(activeHandlerMutex);
        activeHandlerCv.wait(lock, [&] { return activeHandlerEntered; });
    }
    std::thread releaseActiveHandlerThread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        {
            std::lock_guard lock(activeHandlerMutex);
            releaseActiveHandler = true;
        }
        activeHandlerCv.notify_all();
    });
    activeHandlerSubscription.reset();
    const int activeDestructionsAtResetReturn =
        activeHandlerDestructions.load(std::memory_order_relaxed);
    releaseActiveHandlerThread.join();
    activeHandlerFire.join();
    check(activeDestructionsAtResetReturn == 2 &&
              activeHandlerCalls.load(std::memory_order_relaxed) == 1,
          "reset drains running callbacks and destroys their callable copies before returning");

    struct ReentrantEvent { bool block = false; };
    api::EventHook<ReentrantEvent> reentrantHook;
    std::mutex reentrantMutex;
    std::condition_variable reentrantCv;
    bool otherInvocationEntered = false;
    bool resetCallbackEntered = false;
    bool releaseOtherInvocation = false;
    api::Subscription reentrantSubscription;
    reentrantSubscription = reentrantHook.subscribeScoped(
        0, [&](ReentrantEvent& event) {
            if (!event.block) {
                {
                    std::lock_guard lock(reentrantMutex);
                    resetCallbackEntered = true;
                }
                reentrantCv.notify_all();
                reentrantSubscription.reset();
                return;
            }
            std::unique_lock lock(reentrantMutex);
            otherInvocationEntered = true;
            reentrantCv.notify_all();
            reentrantCv.wait(lock, [&] { return releaseOtherInvocation; });
        });
    const auto reentrantEntries =
        api::detail::BusBase::get(typeid(ReentrantEvent)).snapshot();
    const auto reentrantEntry = reentrantEntries.empty()
        ? api::detail::BusBase::EntryRef{}
        : reentrantEntries.front();
    std::thread otherInvocation([&] {
        ReentrantEvent event{true};
        reentrantHook.fire(event);
    });
    {
        std::unique_lock lock(reentrantMutex);
        reentrantCv.wait(lock, [&] { return otherInvocationEntered; });
    }
    auto reentrantReset = std::async(std::launch::async, [&] {
        ReentrantEvent event{false};
        reentrantHook.fire(event);
    });
    {
        std::unique_lock lock(reentrantMutex);
        reentrantCv.wait(lock, [&] { return resetCallbackEntered; });
    }
    bool enteredDrainWait = false;
    const auto drainWaitDeadline = std::chrono::steady_clock::now() +
                                   std::chrono::seconds(1);
    while (reentrantEntry && std::chrono::steady_clock::now() < drainWaitDeadline) {
        {
            std::lock_guard lock(reentrantEntry->mutex);
            enteredDrainWait = reentrantEntry->drainWaiters != 0;
        }
        if (enteredDrainWait) break;
        std::this_thread::yield();
    }
    const bool reentrantResetWaited = enteredDrainWait &&
        reentrantReset.wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::timeout;
    {
        std::lock_guard lock(reentrantMutex);
        releaseOtherInvocation = true;
    }
    reentrantCv.notify_all();
    otherInvocation.join();
    reentrantReset.get();
    check(reentrantResetWaited,
          "reentrant subscription reset drains callbacks on other threads");

    DatapackManager datapacks;
    const auto randomPredicate = json::Value::parse(
        "{\"condition\":\"minecraft:random_chance\",\"chance\":0.25}");
    int randomTrue = 0;
    int randomFalse = 0;
    for (std::uint64_t seed = 1; seed <= 128; ++seed) {
        PredicateContext context;
        context.randomSeed = seed;
        if (datapacks.evaluatePredicateValue(randomPredicate, context)) ++randomTrue;
        else ++randomFalse;
    }
    check(randomTrue > 0 && randomFalse > 0,
          "random chance predicates use seeded randomness instead of a fixed threshold");

    std::printf("goal_cleanup_game: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
