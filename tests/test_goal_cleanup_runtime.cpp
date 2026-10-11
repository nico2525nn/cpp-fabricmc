#include "../src/brigadier/Arguments.hpp"
#include "../src/brigadier/Tree.hpp"
#include "../src/jvm/JavaObjectCache.hpp"
#include "../src/jvm/NativeHandleTable.hpp"
#include "../src/worldgen/StructurePlacer.hpp"
#include "../src/game/BlockPredicate.hpp"
#include "../src/game/PlayerLoginHandoff.hpp"
#include "../src/game/SimulationDispatchGuard.hpp"

#include <cstdint>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testNestedArguments() {
    cppfm::brigadier::ParseCtx context;
    auto item = cppfm::brigadier::args::itemPredicateArg();
    cppfm::brigadier::StringReader itemReader(
        "minecraft:stone[foo{bar:[1,2]}]{tag:{nested:true}}");
    require(item.parse(itemReader, context).asStr() == "minecraft:stone" &&
                itemReader.remainingLength() == 0,
            "item predicate did not consume nested data");

    auto nbt = cppfm::brigadier::args::nbtArg();
    cppfm::brigadier::StringReader nbtReader(" {outer:{inner:[1,2,{x:3}]}}");
    require(nbt.parse(nbtReader, context).asStr() ==
                "{outer:{inner:[1,2,{x:3}]}}",
            "NBT parser lost nested data");

    bool rejected = false;
    try {
        cppfm::brigadier::StringReader malformed("{outer:{inner:1}");
        (void)nbt.parse(malformed, context);
    } catch (const cppfm::brigadier::StringReader::ParseError&) {
        rejected = true;
    }
    require(rejected, "unterminated NBT was accepted");

    auto blockPredicate = cppfm::brigadier::args::blockPredicateArg();
    cppfm::brigadier::StringReader tagReader(
        "#minecraft:logs[axis=x,waterlogged=false]");
    require(blockPredicate.parse(tagReader, context).asStr() ==
                "#minecraft:logs[axis=x,waterlogged=false]" &&
                tagReader.remainingLength() == 0,
            "block-tag predicate did not preserve partial state properties");
    bool malformedTagRejected = false;
    try {
        cppfm::brigadier::StringReader malformedTag(
            "#minecraft:logs[axis=x");
        (void)blockPredicate.parse(malformedTag, context);
    } catch (const cppfm::brigadier::StringReader::ParseError&) {
        malformedTagRejected = true;
    }
    require(malformedTagRejected,
            "unterminated block-tag properties were accepted");

    bool trailingPropertyRejected = false;
    try {
        (void)cppfm::block_predicate::parseBlockPredicate(
            "minecraft:oak_log[axis=x,]");
    } catch (const std::runtime_error&) {
        trailingPropertyRejected = true;
    }
    require(trailingPropertyRejected,
            "block predicate silently ignored an empty trailing property");
}

void testFailedCommandBranchRestoresArguments() {
    using namespace cppfm::brigadier;
    CommandDispatcher dispatcher;
    auto incompleteBranch = CommandNode::argument(
        "stale", args::stringWord());
    incompleteBranch->then(CommandNode::literal("not-the-input"));
    dispatcher.root->then(incompleteBranch);

    bool sawStaleArgument = false;
    auto validBranch = CommandNode::argument("selected", args::stringWord());
    validBranch->executable = true;
    validBranch->action = [&](CommandContext& context) {
        sawStaleArgument = context.args.contains("stale");
        return 1;
    };
    dispatcher.root->then(validBranch);

    const auto result = dispatcher.execute("value", CommandSource{});
    require(result.ok && !sawStaleArgument,
            "failed argument branch leaked values into the selected command");
}

void testResourceLocations() {
    using namespace cppfm::brigadier;
    ParseCtx context;
    const auto argument = args::resourceLocation();
    const auto parse = [&](const std::string& text) {
        StringReader reader(text);
        const auto value = argument.parse(reader, context).asStr();
        require(reader.remainingLength() == 0,
                "resource location parser left trailing identifier characters");
        return value;
    };
    require(parse("stone") == "minecraft:stone",
            "resource location defaults an omitted namespace");
    require(parse("mod_id.v2:path/segment-name_0") ==
                "mod_id.v2:path/segment-name_0",
            "resource location accepts the vanilla namespace/path character sets");
    // Identifier validation is character-based: path separators and dots are
    // legal identifier characters. Filesystem path safety is enforced by the
    // file resolver, not by changing command-parser identifier semantics.
    require(parse("mod:../assets//entry") == "mod:../assets//entry",
            "resource location does not impose filesystem segment rules");

    for (const std::string invalid : {"Mod:path", "mod:Upper", "mod:bad+path",
                                      "bad\\namespace:path", "mod:bad\\path",
                                      "mod:", ":path", "mod:path:extra"}) {
        bool rejected = false;
        try {
            StringReader reader(invalid);
            (void)argument.parse(reader, context);
            rejected = reader.remainingLength() != 0;
        } catch (const StringReader::ParseError&) {
            rejected = true;
        }
        require(rejected, "invalid resource location was accepted");
    }

    const std::vector<std::pair<ArgumentType, std::string>> specialized{
        {args::itemStackArg(), "minecraft:Stone"},
        {args::blockStateArg(), "minecraft:Stone"},
        {args::blockPredicateArg(), "#minecraft:Stone"},
        {args::itemPredicateArg(), "#minecraft:Stone"},
        {args::dimensionArg(), "minecraft:Overworld"},
        {args::lootTableArg(), "minecraft:Stone"},
    };
    for (const auto& [argumentType, text] : specialized) {
        bool rejected = false;
        try {
            StringReader reader(text);
            (void)argumentType.parse(reader, context);
        } catch (const StringReader::ParseError&) {
            rejected = true;
        }
        require(rejected,
                "specialized identifier argument accepted a non-vanilla identifier");
    }
}

void testStructureOrigins() {
    cppfm::worldgen::StructurePlacer placer(0x12345678ULL);
    cppfm::worldgen::PlacedFeature feature;
    feature.spacing = 32;
    feature.separation = 8;
    feature.salt = 0x55AAULL;
    feature.frequency = 1.0;

    bool checked = false;
    for (std::int32_t cx = 0; cx < 320 && !checked; ++cx) {
        for (std::int32_t cz = 0; cz < 320 && !checked; ++cz) {
            if (!placer.shouldPlaceAt(feature, cx, cz)) continue;
            std::int32_t originX = 0;
            std::int32_t originZ = 0;
            require(placer.findOrigin(feature, cx, cz, originX, originZ),
                    "placed origin was not discoverable");
            require(originX == cx && originZ == cz,
                    "origin lookup disagreed with placement check");
            checked = true;
        }
    }
    require(checked, "no deterministic placement candidate found");
}

void testJvmTables() {
    cppfm::jvm::NativeHandleTable handles;
    int object = 0;
    const auto handle = handles.registerObject(&object, cppfm::jvm::HandleKind::Entity);
    require(handle != 0 && handles.valid(handle, cppfm::jvm::HandleKind::Entity),
            "native handle was not registered");
    require(handles.invalidateHandle(handle) && !handles.valid(handle),
            "native handle invalidation failed");

    cppfm::jvm::JavaObjectCache cache;
    cache.put(handle, &object);
    cache.put(nullptr, handle + 1, "Example", &object);
    require(cache.size() == 2, "JVM object cache insertion failed");
    cache.erase(handle);
    require(cache.size() == 1, "JVM object cache erase lost the wrong entry");
    cache.erase(nullptr, handle + 1);
    require(cache.size() == 0, "JVM object cache did not drain all typed entries");
}

void testPlayerHandoffCleanupPolicy() {
    using cppfm::HandoffCleanupPolicy;
    using cppfm::PlayerHandoffReadiness;
    PlayerHandoffReadiness readiness;
    int failuresReported = 0;
    int laterStepsRun = 0;
    const auto reportFailure = [&](std::string_view,
                                   const std::exception* error) {
        require(error != nullptr, "cleanup exception details were lost");
        ++failuresReported;
    };

    cppfm::runPlayerHandoffCleanupStep(
        "player quit event", HandoffCleanupPolicy::BestEffort, readiness,
        [] { throw std::runtime_error("event callback failed"); },
        reportFailure);
    cppfm::runPlayerHandoffCleanupStep(
        "player save", HandoffCleanupPolicy::RequiredForReplacement,
        readiness, [&] { ++laterStepsRun; }, reportFailure);
    require(readiness.ready() && failuresReported == 1 && laterStepsRun == 1,
            "best-effort hook failure incorrectly fenced a saved player");

    cppfm::runPlayerHandoffCleanupStep(
        "required persistence", HandoffCleanupPolicy::RequiredForReplacement,
        readiness, [] { throw std::runtime_error("save failed"); },
        reportFailure);
    require(!readiness.ready() && failuresReported == 2,
            "required cleanup failure did not block unsafe player replacement");

    std::promise<bool> pendingCleanup;
    const auto completion = pendingCleanup.get_future().share();
    const auto shortDeadline = std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(20);
    const auto timedOut = cppfm::waitForPlayerHandoffCleanup(
        completion, shortDeadline);
    require(!timedOut,
            "player handoff wait did not return at its deadline");
    pendingCleanup.set_value(true);
    const auto completed = cppfm::waitForPlayerHandoffCleanup(
        completion, std::chrono::steady_clock::now() +
                        std::chrono::seconds(1));
    require(completed && *completed,
            "completed player cleanup was not observed after a timeout");

    std::promise<bool> failedCleanup;
    failedCleanup.set_value(false);
    const auto failed = cppfm::waitForPlayerHandoffCleanup(
        failedCleanup.get_future().share(),
        std::chrono::steady_clock::now() + std::chrono::seconds(1));
    require(failed && !*failed,
            "unsafe player cleanup result was not preserved");
}

void testPlayerHandoffLeaseDeadline() {
    cppfm::PlayerLoginHandoffCoordinator coordinator;
    const auto uuid = std::string("same-player-uuid");
    auto expired = coordinator.enterUntil(
        "expired-player-uuid",
        cppfm::PlayerLoginHandoffCoordinator::Clock::now() -
            std::chrono::milliseconds(1));
    require(!expired, "expired identity lease deadline was accepted");

    auto owner = coordinator.enterUntil(
        uuid, cppfm::PlayerLoginHandoffCoordinator::Clock::now() +
                  std::chrono::seconds(1));
    require(owner.has_value() && !owner->waited(),
            "first player handoff did not acquire its identity lease immediately");

    const auto waitStarted = cppfm::PlayerLoginHandoffCoordinator::Clock::now();
    auto contender = coordinator.enterUntil(
        uuid, waitStarted + std::chrono::milliseconds(20));
    const auto waitElapsed =
        cppfm::PlayerLoginHandoffCoordinator::Clock::now() - waitStarted;
    require(!contender && waitElapsed < std::chrono::seconds(1),
            "same-UUID identity lease wait exceeded its deadline");

    auto independent = coordinator.enterUntil(
        "different-player-uuid",
        cppfm::PlayerLoginHandoffCoordinator::Clock::now() +
            std::chrono::seconds(1));
    require(independent.has_value() && !independent->waited(),
            "one player identity blocked an unrelated handoff");

    auto nameOwner = coordinator.enterUntil(
        "name:ReusedPlayer",
        cppfm::PlayerLoginHandoffCoordinator::Clock::now() +
            std::chrono::seconds(1));
    require(nameOwner.has_value() && !nameOwner->waited(),
            "name-based replacement could not acquire its independent handoff lease");
    auto nameContender = coordinator.enterUntil(
        "name:ReusedPlayer",
        cppfm::PlayerLoginHandoffCoordinator::Clock::now() +
            std::chrono::milliseconds(20));
    require(!nameContender,
            "same-name replacement was not serialized behind the incumbent handoff");
}

void testNestedSimulationDispatchRelease() {
    std::recursive_mutex mutex;
    cppfm::SimulationDispatchGuard outer(mutex);
    cppfm::SimulationDispatchGuard inner(mutex);
    std::promise<void> attempted;
    std::promise<void> acquired;
    auto attemptedFuture = attempted.get_future();
    auto acquiredFuture = acquired.get_future();
    std::thread waiter;
    bool depthClearedDuringCallback = false;
    bool waiterAcquiredDuringCallback = false;

    cppfm::SimulationDispatchGuard::withCurrentLockReleased([&] {
        depthClearedDuringCallback =
            cppfm::activeSimulationDispatchDepth == 0;
        waiter = std::thread([&] {
            attempted.set_value();
            std::lock_guard lock(mutex);
            acquired.set_value();
        });
        attemptedFuture.wait();
        waiterAcquiredDuringCallback =
            acquiredFuture.wait_for(std::chrono::seconds(1)) ==
            std::future_status::ready;
    });

    const bool callbackRestoredGuards =
        outer.owns_lock() && inner.owns_lock() &&
        cppfm::activeSimulationDispatchDepth == 2;

    bool callbackThrew = false;
    try {
        inner.withLockReleased([] {
            throw std::runtime_error("expected callback failure");
        });
    } catch (const std::runtime_error&) {
        callbackThrew = true;
    }
    const bool exceptionRestoredGuards =
        outer.owns_lock() && inner.owns_lock() &&
        cppfm::activeSimulationDispatchDepth == 2;
    inner.unlock();
    outer.unlock();
    if (waiter.joinable()) waiter.join();
    require(depthClearedDuringCallback && waiterAcquiredDuringCallback,
            "nested simulation guard kept the dispatch mutex held in callback");
    require(callbackRestoredGuards && callbackThrew && exceptionRestoredGuards,
            "nested simulation guards were not restored after callback exception");
}

} // namespace

int main() {
    try {
        testNestedArguments();
        testFailedCommandBranchRestoresArguments();
        testResourceLocations();
        testStructureOrigins();
        testJvmTables();
        testPlayerHandoffCleanupPolicy();
        testPlayerHandoffLeaseDeadline();
        testNestedSimulationDispatchRelease();
        std::cout << "goal-cleanup-runtime: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "goal-cleanup-runtime: FAIL: " << error.what() << '\n';
        return 1;
    }
}
