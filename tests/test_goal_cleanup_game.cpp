#include "game/BlockEvent.hpp"
#include "game/DatapackManager.hpp"

#include <chrono>
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
