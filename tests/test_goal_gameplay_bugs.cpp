#include <cstdint>
#include <cstdio>
#include <atomic>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "generated/BlockStates.hpp"
#include "game/World.hpp"
#include "game/GameServer.hpp"
#include "physics/BlockTickScheduler.hpp"
#include "physics/Fluids.hpp"
#include "physics/LightEngine.hpp"
#include "physics/Redstone.hpp"
#include "worldgen/StructureManager.hpp"

using namespace cppfm;

// The standalone goal harness links the gameplay object set without the
// production server translation units.  These no-op sinks keep calls made
// only when a non-null GameServer is supplied out of the harness while still
// exercising every owned behavior through its shipped implementation.
namespace cppfm {
struct WorldMutationContractTestAccess {
    static std::uint32_t mutationOrderWaiters(const World& world) {
        return world.mutationOrderWaiters_.load(std::memory_order_acquire);
    }
};

bool GameServer::isChunkInSimulationDistanceFor(std::int8_t, std::int32_t,
                                                std::int32_t) const { return true; }
void GameServer::broadcastBlockChangeFor(std::int8_t, std::int32_t,
                                         std::int32_t, std::int32_t,
                                         std::uint16_t) {}
void GameServer::broadcastPaleOakLeavesParticleFor(std::int8_t, double,
                                                   double, double) {}
void GameServer::broadcastSoundFor(std::int8_t, const char*, double, double,
                                   double, float, float, const char*,
                                   std::optional<float>) {}
void GameServer::spawnMobFor(std::int8_t, MobKind, double, double, double) {}
}

namespace {
int passed = 0;
int failed = 0;

void check(bool ok, const char* name) {
    if (ok) {
        ++passed;
        std::printf("PASS %s\n", name);
    } else {
        ++failed;
        std::printf("FAIL %s\n", name);
    }
}

std::uint16_t state(const char* name) {
    const auto* block = gen::blockByName(name);
    return block ? static_cast<std::uint16_t>(block->minState) : 0;
}

std::uint16_t stateWithAge(const char* name, int age) {
    return static_cast<std::uint16_t>(gen::stateWithPropsList(
        name, {{"age", std::to_string(age)}}));
}

std::string prop(std::uint16_t value, const char* key) {
    for (const auto& [name, text] : gen::propsOf(value))
        if (name == key) return std::string(text);
    return {};
}

void schedulerKeyCollision() {
    World world("minecraft:plains", LevelType::Flat, 0);
    BlockTickScheduler scheduler(world, nullptr, nullptr);
    // The former x/y/z XOR key made (0,1,0) collide with
    // (0,0,1<<20), dropping one scheduled update.
    scheduler.schedule(0, 1, 0, 10);
    scheduler.schedule(0, 0, 1 << 20, 10);
    check(scheduler.pendingCount() == 2,
          "block tick positions use collision-free packed coordinates");
}

void randomTickTiming() {
    World world("minecraft:plains", LevelType::Flat, 0);
    RandomTickScheduler scheduler(world, nullptr, nullptr);
    scheduler.tick(100);
    scheduler.scheduleRandomTick(0, -60, 0, 5);
    check(scheduler.nextDueTick() == std::optional<std::int64_t>(105),
          "relative random tick delay uses the current tick");
    scheduler.scheduleRandomTick(1, -60, 0, 5, 200);
    check(scheduler.nextDueTick() == std::optional<std::int64_t>(105),
          "earlier random tick remains ordered by absolute deadline");
}

void cropLightPosition() {
    constexpr int y = -60;
    World world("minecraft:plains", LevelType::Flat, 0);
    world.setBlock(0, y - 1, 0, state("minecraft:farmland"));
    world.setBlock(0, y, 0, stateWithAge("minecraft:wheat", 0));
    world.setBlock(0, y + 1, 0, state("minecraft:stone"));
    LightEngine light(world);
    light.ensureSkyLight(0, 0);
    world.setBlockLightRaw(0, y, 0, 15);
    CropBehavior crop;
    for (int i = 0; i < 256 && prop(world.getBlock(0, y, 0), "age") == "0"; ++i)
        crop.tick(world, 0, y, 0, world.getBlock(0, y, 0), i, nullptr);
    check(prop(world.getBlock(0, y, 0), "age") != "0",
          "crop growth samples light at the crop position");
}

void chorusBlockedAges() {
    constexpr int y = -60;
    World world("minecraft:plains", LevelType::Flat, 0);
    world.setBlock(0, y, 0, stateWithAge("minecraft:chorus_flower", 0));
    world.setBlock(0, y + 1, 0, state("minecraft:stone"));
    world.setBlock(1, y, 0, state("minecraft:stone"));
    world.setBlock(-1, y, 0, state("minecraft:stone"));
    world.setBlock(0, y, 1, state("minecraft:stone"));
    world.setBlock(0, y, -1, state("minecraft:stone"));
    ChorusFlowerBehavior flower;
    std::string firstChanged;
    for (int i = 0; i < 64 && firstChanged.empty(); ++i) {
        const auto before = world.getBlock(0, y, 0);
        flower.tick(world, 0, y, 0, before, i, nullptr);
        const auto after = world.getBlock(0, y, 0);
        if (after != before) firstChanged = prop(after, "age");
    }
    check(!firstChanged.empty() && firstChanged != "5",
          "blocked chorus flowers advance one age instead of dying immediately");
}

void campfireStateSafety() {
    constexpr int y = -60;
    World world("minecraft:plains", LevelType::Flat, 0);
    const auto campfire = static_cast<std::uint16_t>(gen::stateWithPropsList(
        "minecraft:campfire", {{"facing", "north"}, {"lit", "true"},
                                {"signal_fire", "false"}, {"waterlogged", "false"}}));
    world.setBlock(0, y, 0, campfire);
    CampfireBehavior behavior;
    for (int i = 0; i < 32; ++i)
        behavior.tick(world, 0, y, 0, world.getBlock(0, y, 0), i, nullptr);
    check(world.getBlock(0, y, 0) == campfire,
          "lit campfires do not receive invalid age-based fire mutations");
}

void flammableCoverage() {
    const auto& registry = FlammableRegistry::instance();
    check(registry.get("minecraft:bamboo").has_value(),
          "bamboo is recognized as flammable");
    check(registry.get("minecraft:oak_fence").has_value(),
          "wood fences are recognized as flammable");
    check(registry.get("minecraft:red_carpet").has_value(),
          "carpets are recognized as flammable");
    check(registry.get("minecraft:scaffolding").has_value(),
          "scaffolding is recognized as flammable");
}

void repeaterDirectSource() {
    constexpr int y = -60;
    World world("minecraft:plains", LevelType::Flat, 0);
    RedstoneEngine engine(world);
    std::int64_t now = 0;
    engine.setTickRef(&now);
    const auto repeater = static_cast<std::uint16_t>(gen::stateWithPropsList(
        "minecraft:repeater", {{"facing", "east"}, {"delay", "1"},
                                {"locked", "false"}, {"powered", "false"}}));
    world.setBlock(-1, y, 0, state("minecraft:redstone_block"));
    world.setBlock(0, y, 0, repeater);
    engine.onBlockChanged(-1, y, 0);
    engine.onBlockChanged(0, y, 0);
    now = 2;
    engine.tick(now);
    check(prop(world.getBlock(0, y, 0), "powered") == "true",
          "repeaters accept a directly adjacent source at their input");
}

void fluidAndLightBounds() {
    World world("minecraft:plains", LevelType::Flat, 0);
    FluidSim fluids(world);
    fluids.touch(std::numeric_limits<std::int32_t>::max(), -60, 0);
    fluids.touch(0, kMaxY, 0);
    check(fluids.pending() == 0,
          "fluid notifications reject out-of-range coordinates");
    LightEngine light(world);
    light.onBlockChanged(std::numeric_limits<std::int32_t>::max(), -60, 0, 0, state("minecraft:torch"));
    check(light.pendingNodeCount() == 0,
          "light updates reject out-of-range coordinates");
}

void waterLightAttenuation() {
    constexpr int y = -60;
    World world("minecraft:plains", LevelType::Flat, 0);
    const auto glowstone = state("minecraft:glowstone");
    const auto water = static_cast<std::uint16_t>(gen::stateWithPropsList(
        "minecraft:water", {{"level", "0"}}));
    world.setBlock(0, y, 0, glowstone);
    LightEngine light(world);
    light.onBlockChanged(0, y, 0, 0, glowstone);
    light.drain();
    world.setBlock(1, y, 0, water);
    light.onBlockChanged(1, y, 0, 0, water);
    light.drain();
    check(light.blockLightAt(1, y, 0) == 14,
          "water attenuates block light by one level");
}

void netherOpacityRemovalReseedsLight() {
    constexpr int y = 40;
    World world("minecraft:plains", LevelType::Nether, 0);
    const auto stone = state("minecraft:stone");
    const auto glowstone = state("minecraft:glowstone");
    for (int x = -1; x <= 3; ++x)
        for (int dy = -1; dy <= 1; ++dy)
            for (int z = -1; z <= 1; ++z)
                world.setBlock(x, y + dy, z, stone);
    world.setBlock(0, y, 0, glowstone);
    world.setBlock(2, y, 0, 0);

    LightEngine light(world);
    world.setOnBlockChanged(
        [&light](std::int32_t x, std::int32_t y, std::int32_t z,
                 std::uint16_t oldState, std::uint16_t newState) {
            light.onBlockChanged(x, y, z, oldState, newState);
        });
    light.onBlockChanged(0, y, 0, 0, glowstone);
    light.drain();
    const bool initiallyBlocked = light.blockLightAt(2, y, 0) == 0;
    const bool removed = world.setBlock(1, y, 0, 0);
    light.drain();
    check(initiallyBlocked && removed &&
              light.blockLightAt(0, y, 0) == 15 &&
              light.blockLightAt(2, y, 0) == 13,
          "removing an opaque Nether block re-propagates block light without skylight");
}

void competingEmitterLightLevels() {
    constexpr int y = 40;
    World world("minecraft:plains", LevelType::Nether, 0);
    LightEngine light(world);
    const auto stone = state("minecraft:stone");
    const auto weakEmitter = state("minecraft:redstone_torch");
    const auto strongEmitter = state("minecraft:glowstone");
    world.setOnBlockChanged(
        [&light](std::int32_t x, std::int32_t y, std::int32_t z,
                 std::uint16_t oldState, std::uint16_t newState) {
            light.onBlockChanged(x, y, z, oldState, newState);
        });

    for (int x = -1; x <= 3; ++x)
        for (int dy = -1; dy <= 1; ++dy)
            for (int z = -1; z <= 1; ++z)
                world.setBlock(x, y + dy, z, stone);
    world.setBlock(0, y, 0, weakEmitter);
    world.setBlock(1, y, 0, 0);
    world.setBlock(2, y, 0, 0);
    light.drain();
    const bool weakSourceLit = light.blockLightAt(0, y, 0) == 7 &&
                               light.blockLightAt(1, y, 0) == 6;

    world.setBlock(2, y, 0, strongEmitter);
    light.drain();
    const bool strongSourceBrightensWeakEmitterCell =
        light.blockLightAt(2, y, 0) == 15 &&
        light.blockLightAt(1, y, 0) == 14 &&
        light.blockLightAt(0, y, 0) == 13;

    world.setBlock(2, y, 0, 0);
    light.drain();
    const bool weakFieldReturns = light.blockLightAt(0, y, 0) == 7 &&
                                  light.blockLightAt(1, y, 0) == 6 &&
                                  light.blockLightAt(2, y, 0) == 5;
    check(weakSourceLit && strongSourceBrightensWeakEmitterCell &&
              weakFieldReturns,
          "strong neighboring light brightens an emitter cell and removal restores its own field");
}

void lightDrainLinearizesBlockEdits() {
    constexpr int y = 40;
    constexpr int sourceX = 8;
    World world("minecraft:plains", LevelType::Nether, 0);
    LightEngine light(world);
    const auto glowstone = state("minecraft:glowstone");
    world.setOnBlockChanged(
        [&light](std::int32_t x, std::int32_t y, std::int32_t z,
                 std::uint16_t oldState, std::uint16_t newState) {
            light.onBlockChanged(x, y, z, oldState, newState);
        });
    world.setBlock(sourceX, y, 0, glowstone);
    light.drain();
    world.setBlock(sourceX, y, 0, 0);

    auto mutationGate = world.lockMutationGate();
    std::atomic<bool> drainStarted{false};
    std::atomic<bool> drainFinished{false};
    std::thread drainer([&] {
        drainStarted.store(true, std::memory_order_release);
        light.drain();
        drainFinished.store(true, std::memory_order_release);
    });
    while (!drainStarted.load(std::memory_order_acquire))
        std::this_thread::yield();
    const auto waiterDeadline = std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(250);
    while (std::chrono::steady_clock::now() < waiterDeadline &&
           WorldMutationContractTestAccess::mutationOrderWaiters(world) == 0)
        std::this_thread::yield();
    const bool drainWaitedForMutationGate =
        WorldMutationContractTestAccess::mutationOrderWaiters(world) != 0 &&
        !drainFinished.load(std::memory_order_acquire) &&
        light.pendingNodeCount() != 0;
    const bool replacementPublished =
        world.setBlock(sourceX, y, 0, glowstone);
    mutationGate.unlock();
    drainer.join();

    const bool replacementLit = replacementPublished &&
        light.blockLightAt(sourceX, y, 0) == 15 &&
        light.blockLightAt(sourceX + 1, y, 0) == 14;
    const bool removalPublished = world.setBlock(sourceX, y, 0, 0);
    light.drain();
    check(drainWaitedForMutationGate && replacementLit && removalPublished &&
              light.blockLightAt(sourceX, y, 0) == 0 &&
              light.blockLightAt(sourceX + 1, y, 0) == 0,
          "light drain and same-cell emitter edits linearize on the world mutation gate");
}

void structureSideEffectsFollowChunkPublication() {
    worldgen::StructureManager structures(12345);
    structures.clear();
    structures.addSet(worldgen::SMStructureSet{
        "minecraft:village", 0, 0, 1, {}});
    const auto ground = [](std::int32_t, std::int32_t) { return 64; };

    {
        Chunk discarded;
        ChunkBuildWriter writer(discarded);
        structures.generate(writer, 0, 0, ground);
    }
    std::vector<worldgen::StructureManager::PendingMob> discardedMobs;
    std::vector<worldgen::StructureManager::PendingLoot> discardedLoot;
    structures.drainPendingMobs(discardedMobs);
    structures.drainPendingLoot(discardedLoot);
    const bool discardedCandidateHadNoSideEffects =
        discardedMobs.empty() && discardedLoot.empty();

    std::function<void()> publishCallback;
    {
        Chunk accepted;
        ChunkBuildWriter writer(accepted);
        structures.generate(writer, 0, 0, ground);
        publishCallback = writer.takePublishCallback();
    }
    std::vector<worldgen::StructureManager::PendingMob> beforePublishMobs;
    std::vector<worldgen::StructureManager::PendingLoot> beforePublishLoot;
    structures.drainPendingMobs(beforePublishMobs);
    structures.drainPendingLoot(beforePublishLoot);
    const bool effectsWaitedForPublication =
        static_cast<bool>(publishCallback) && beforePublishMobs.empty() &&
        beforePublishLoot.empty();
    invokeChunkPublishCallback(publishCallback);

    std::vector<worldgen::StructureManager::PendingMob> publishedMobs;
    std::vector<worldgen::StructureManager::PendingLoot> publishedLoot;
    structures.drainPendingMobs(publishedMobs);
    structures.drainPendingLoot(publishedLoot);

    World generatedWorld("minecraft:plains", LevelType::Normal, 12345);
    auto* generatedStructures = generatedWorld.structureManager();
    std::vector<worldgen::StructureManager::PendingMob> worldPublishedMobs;
    bool worldPublicationCommittedEffects = false;
    if (generatedStructures) {
        generatedStructures->clear();
        generatedStructures->addSet(worldgen::SMStructureSet{
            "minecraft:village", 0, 0, 1, {}});
        const bool generated = generatedWorld.generateChunkIfMissing(0, 0);
        generatedStructures->drainPendingMobs(worldPublishedMobs);
        worldPublicationCommittedEffects = generated &&
            generatedWorld.hasChunk(0, 0) && !worldPublishedMobs.empty();
    }
    bool delayedLootRetryPreservedRecord = false;
    if (!publishedLoot.empty()) {
        const auto expected = publishedLoot.front();
        structures.requeuePendingLoot(expected, 200);
        const auto chunkX = expected.pos[0] >> 4;
        const auto chunkZ = expected.pos[2] >> 4;
        const bool delayedLootBlocksSave =
            structures.hasPendingLootInChunk(chunkX, chunkZ);
        const bool unrelatedChunkCanSave =
            !structures.hasPendingLootInChunk(chunkX + 7, chunkZ + 9);
        std::vector<worldgen::StructureManager::PendingLoot> tooEarly;
        std::vector<worldgen::StructureManager::PendingLoot> ready;
        structures.drainPendingLoot(tooEarly, 199);
        const bool earlyRetryStillBlocksSave =
            structures.hasPendingLootInChunk(chunkX, chunkZ);
        structures.drainPendingLoot(ready, 200);
        const bool committedRetryReleasesSave =
            !structures.hasPendingLootInChunk(chunkX, chunkZ);
        delayedLootRetryPreservedRecord = tooEarly.empty() &&
            ready.size() == 1 && ready.front().pos == expected.pos &&
            ready.front().lootTable == expected.lootTable &&
            delayedLootBlocksSave && unrelatedChunkCanSave &&
            earlyRetryStillBlocksSave && committedRetryReleasesSave;
    }
    check(discardedCandidateHadNoSideEffects && effectsWaitedForPublication &&
              !publishedMobs.empty() && worldPublicationCommittedEffects &&
              delayedLootRetryPreservedRecord,
          "structure effects publish atomically and delayed unsupported loot remains queued");
}

void configuredFeatureEffectsHaveSingleChunkOwnership() {
    namespace fs = std::filesystem;
    static std::atomic<std::uint64_t> fixtureSequence{0};
    const auto fixtureRoot = fs::temp_directory_path() /
        ("cppfm-structure-effects-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
         "-" + std::to_string(fixtureSequence.fetch_add(1)));
    std::error_code ec;
    const bool ownsFixture = fs::create_directory(fixtureRoot, ec);
    bool fixtureReady = ownsFixture && !ec;
    if (fixtureReady) {
        std::ofstream feature(fixtureRoot / "loot_owner_fixture.json",
                             std::ios::binary | std::ios::trunc);
        feature << R"({
          "name":"example:loot_owner_fixture",
          "type":"example:fixture",
          "placement":{"spacing":32,"separation":8,"salt":177,"frequency":1.0},
          "pieces":[{"name":"example:fixture","weight":1}],
          "palette":{"stone":"minecraft:stone"},
          "loot":[{"pos":"-1,0,0","table":"example:loot_owner_fixture"}],
          "mobs":[{"pos":[-1,0,0],"mob":"example:marker_mob","count":1}]
        })";
        fixtureReady = static_cast<bool>(feature);
    }

    worldgen::StructureManager structures(12345);
    structures.clear();
    const int loadedFixtureCount = fixtureReady
        ? structures.placer().load(fixtureRoot.string()) : 0;
    const auto* placed = structures.placer().getPlaced(
        "example:loot_owner_fixture");
    std::int32_t originCx = 0, originCz = 0;
    bool negativeOriginFound = false;
    if (placed) {
        for (std::int32_t cx = -96; cx <= 96 && !negativeOriginFound; ++cx) {
            for (std::int32_t cz = -96; cz <= 96; ++cz) {
                if (cx < 0 && structures.placer().shouldPlaceAt(
                                  *placed, cx, cz)) {
                    originCx = cx;
                    originCz = cz;
                    negativeOriginFound = true;
                    break;
                }
            }
        }
    }

    if (negativeOriginFound) {
        const auto ground = [](std::int32_t, std::int32_t) { return 64; };
        for (std::int32_t cx = originCx - 3; cx <= originCx + 3; ++cx) {
            for (std::int32_t cz = originCz - 3; cz <= originCz + 3; ++cz) {
                Chunk candidate;
                ChunkBuildWriter writer(candidate);
                structures.generate(writer, cx, cz, ground);
                auto publish = writer.takePublishCallback();
                invokeChunkPublishCallback(publish);
            }
        }
    }

    std::vector<worldgen::StructureManager::PendingLoot> queuedLoot;
    std::vector<worldgen::StructureManager::PendingMob> queuedMobs;
    structures.drainPendingLoot(queuedLoot);
    structures.drainPendingMobs(queuedMobs);
    std::vector<worldgen::StructureManager::PendingLoot> fixtureLoot;
    std::vector<worldgen::StructureManager::PendingMob> fixtureMobs;
    for (const auto& loot : queuedLoot)
        if (loot.lootTable == "example:loot_owner_fixture")
            fixtureLoot.push_back(loot);
    for (const auto& mob : queuedMobs)
        if (mob.mob == "example:marker_mob") fixtureMobs.push_back(mob);

    const std::array<int, 3> expectedPosition{
        originCx * 16 - 1, 64, originCz * 16};
    if (!(fixtureReady && negativeOriginFound && fixtureLoot.size() == 1 &&
          fixtureMobs.size() == 1 &&
          fixtureLoot.front().pos == expectedPosition &&
          fixtureMobs.front().pos == expectedPosition)) {
        std::fprintf(stderr,
                     "configured feature fixture: ready=%d placed=%d originFound=%d origin=(%d,%d) expected=(%d,%d,%d) loot=%zu mobs=%zu\n",
                     fixtureReady && loadedFixtureCount == 1,
                     placed != nullptr, negativeOriginFound,
                     originCx, originCz, expectedPosition[0], expectedPosition[1],
                     expectedPosition[2], fixtureLoot.size(), fixtureMobs.size());
        for (const auto& item : fixtureLoot)
            std::fprintf(stderr, "  loot %s at (%d,%d,%d)\n",
                         item.lootTable.c_str(), item.pos[0], item.pos[1],
                         item.pos[2]);
        for (const auto& mob : fixtureMobs)
            std::fprintf(stderr, "  mob %s at (%d,%d,%d) count=%d\n",
                         mob.mob.c_str(), mob.pos[0], mob.pos[1], mob.pos[2],
                         mob.count);
    }
    check(fixtureReady && loadedFixtureCount == 1 && negativeOriginFound &&
              fixtureLoot.size() == 1 &&
              fixtureMobs.size() == 1 &&
              fixtureLoot.front().pos == expectedPosition &&
              fixtureMobs.front().pos == expectedPosition,
          "configured structure loot and mob effects publish once from their owning negative-coordinate chunk");

    worldgen::SMStructureSet configuredSet{
        "example:loot_owner_fixture", 32, 8, 177, {}};
    structures.addSet(configuredSet);
    std::int32_t setOriginCx = 0, setOriginCz = 0;
    std::int32_t setOriginX = 0, setOriginZ = 0;
    bool setOriginFound = false;
    for (std::int32_t cx = -96; cx <= -1 && !setOriginFound; ++cx) {
        for (std::int32_t cz = -96; cz <= 96; ++cz) {
            const auto candidate = worldgen::smStructureAtChunk(
                configuredSet, 12345, cx, cz);
            if (candidate.present && candidate.originCx == cx &&
                candidate.originCz == cz) {
                setOriginCx = cx;
                setOriginCz = cz;
                setOriginX = candidate.originX;
                setOriginZ = candidate.originZ;
                setOriginFound = true;
                break;
            }
        }
    }
    if (fixtureReady && setOriginFound) {
        const auto ground = [](std::int32_t, std::int32_t) { return 64; };
        for (std::int32_t cx = setOriginCx - 3;
             cx <= setOriginCx + 3; ++cx) {
            for (std::int32_t cz = setOriginCz - 3;
                 cz <= setOriginCz + 3; ++cz) {
                Chunk candidate;
                ChunkBuildWriter writer(candidate);
                structures.generate(writer, cx, cz, ground);
                auto publish = writer.takePublishCallback();
                invokeChunkPublishCallback(publish);
            }
        }
    }
    std::vector<worldgen::StructureManager::PendingLoot> setQueuedLoot;
    std::vector<worldgen::StructureManager::PendingMob> setQueuedMobs;
    structures.drainPendingLoot(setQueuedLoot);
    structures.drainPendingMobs(setQueuedMobs);
    fixtureLoot.clear();
    fixtureMobs.clear();
    for (const auto& loot : setQueuedLoot)
        if (loot.lootTable == "example:loot_owner_fixture")
            fixtureLoot.push_back(loot);
    for (const auto& mob : setQueuedMobs)
        if (mob.mob == "example:marker_mob") fixtureMobs.push_back(mob);
    const std::array<int, 3> expectedSetPosition{
        setOriginX - 1, 64, setOriginZ};
    check(fixtureReady && setOriginFound && fixtureLoot.size() == 1 &&
              fixtureMobs.size() == 1 &&
              fixtureLoot.front().pos == expectedSetPosition &&
              fixtureMobs.front().pos == expectedSetPosition,
          "structure-set loot and mobs are not replayed by surrounding chunks");
    if (ownsFixture) {
        ec.clear();
        fs::remove_all(fixtureRoot, ec);
        check(!ec, "owned configured-feature fixture directory is removed");
    }
}

void hardcodedStructureEffectsHaveSingleChunkOwnership() {
    constexpr std::uint64_t seed = 12345;
    worldgen::StructureManager structures(seed);
    const auto generateAcrossNeighborChunks =
        [&](std::string name, std::uint64_t salt) {
            structures.clear();
            worldgen::SMStructureSet set{
                std::move(name), 32, 8, salt, {}};
            set.maxHoriz = 3;
            structures.addSet(std::move(set));
            const auto& configuredSet = structures.sets().front();

            worldgen::SMStructureAt origin;
            for (std::int32_t cx = -96; cx <= 96 && !origin.present; ++cx) {
                for (std::int32_t cz = -96; cz <= 96; ++cz) {
                    const auto candidate = worldgen::smStructureAtChunk(
                        configuredSet, seed, cx, cz);
                    if (candidate.present) {
                        origin = candidate;
                        break;
                    }
                }
            }

            if (origin.present) {
                const auto ground = [](std::int32_t, std::int32_t) {
                    return 64;
                };
                for (std::int32_t cx =
                         origin.originCx - configuredSet.maxHoriz;
                     cx <= origin.originCx + configuredSet.maxHoriz; ++cx) {
                    for (std::int32_t cz =
                             origin.originCz - configuredSet.maxHoriz;
                         cz <= origin.originCz + configuredSet.maxHoriz;
                         ++cz) {
                        Chunk chunk;
                        ChunkBuildWriter writer(chunk);
                        structures.generate(writer, cx, cz, ground);
                        auto publish = writer.takePublishCallback();
                        invokeChunkPublishCallback(publish);
                    }
                }
            }
            return origin;
        };

    const auto villageOrigin = generateAcrossNeighborChunks(
        "example:village_effect_ownership_fixture", 177);

    std::vector<worldgen::StructureManager::PendingMob> mobs;
    structures.drainPendingMobs(mobs);
    std::vector<worldgen::StructureManager::PendingLoot> loot;
    structures.drainPendingLoot(loot);
    const std::array<std::array<int, 3>, 3> expected{
        std::array<int, 3>{villageOrigin.originX + 2, 65,
                           villageOrigin.originZ + 2},
        std::array<int, 3>{villageOrigin.originX - 2, 65,
                           villageOrigin.originZ - 2},
        std::array<int, 3>{villageOrigin.originX, 65,
                           villageOrigin.originZ}};
    const auto villagers = std::count_if(
        mobs.begin(), mobs.end(), [&](const auto& mob) {
            return mob.mob == "minecraft:villager" &&
                (mob.pos == expected[0] || mob.pos == expected[1]);
        });
    const auto golems = std::count_if(
        mobs.begin(), mobs.end(), [&](const auto& mob) {
            return mob.mob == "minecraft:iron_golem" &&
                mob.pos == expected[2];
        });
    check(villageOrigin.present && villagers == 2 && golems == 1,
          "hardcoded village mobs publish once each when a structure spans generated chunks");

    namespace fs = std::filesystem;
    const auto fixtureRoot = fs::temp_directory_path() /
        ("cppfm-hardcoded-mansion-effects-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code fixtureError;
    const bool fixtureDirectoryCreated =
        fs::create_directory(fixtureRoot, fixtureError);
    bool fixtureFileWritten = false;
    if (fixtureDirectoryCreated && !fixtureError) {
        std::ofstream feature(fixtureRoot / "mansion_effect_fixture.json",
                             std::ios::binary | std::ios::trunc);
        feature << R"({
          "name":"example:mansion_effect_ownership_fixture",
          "type":"example:fixture",
          "placement":{"spacing":32,"separation":8,"salt":233,"frequency":1.0},
          "pieces":[{"name":"mansion","weight":1}],
          "palette":{"planks":"minecraft:dark_oak_planks","log":"minecraft:dark_oak_log","chest":"minecraft:chest"}
        })";
        fixtureFileWritten = static_cast<bool>(feature);
    }
    const auto loadedMansionFixture = fixtureFileWritten
        ? structures.placer().load(fixtureRoot.string()) : 0;
    const auto mansionOrigin = generateAcrossNeighborChunks(
        "example:mansion_effect_ownership_fixture", 233);
    mobs.clear();
    loot.clear();
    structures.drainPendingMobs(mobs);
    structures.drainPendingLoot(loot);
    const std::array<int, 3> expectedChest{
        mansionOrigin.originX + 5, 71, mansionOrigin.originZ + 5};
    const std::array<int, 3> expectedVindicator{
        mansionOrigin.originX + 8, 71, mansionOrigin.originZ + 8};
    const std::array<int, 3> expectedEvoker{
        mansionOrigin.originX + 12, 71, mansionOrigin.originZ + 12};
    const auto mansionLoot = std::count_if(
        loot.begin(), loot.end(), [&](const auto& pending) {
            return pending.lootTable ==
                       "minecraft:chests/woodland_mansion" &&
                   pending.pos == expectedChest;
        });
    const auto vindicators = std::count_if(
        mobs.begin(), mobs.end(), [&](const auto& mob) {
            return mob.mob == "minecraft:vindicator" &&
                   mob.pos == expectedVindicator;
        });
    const auto evokers = std::count_if(
        mobs.begin(), mobs.end(), [&](const auto& mob) {
            return mob.mob == "minecraft:evoker" &&
                   mob.pos == expectedEvoker;
        });
    if (!(fixtureDirectoryCreated && fixtureFileWritten &&
          loadedMansionFixture == 1 && mansionOrigin.present &&
          mansionLoot == 1 && vindicators == 1 && evokers == 1)) {
        std::fprintf(stderr,
            "mansion side-effect fixture: dir=%d file=%d loaded=%d origin=%d loot=%td vindicator=%td evoker=%td\n",
            fixtureDirectoryCreated, fixtureFileWritten, loadedMansionFixture,
            mansionOrigin.present, mansionLoot, vindicators, evokers);
    }
    check(fixtureDirectoryCreated && fixtureFileWritten &&
              loadedMansionFixture == 1 && mansionOrigin.present &&
              mansionLoot == 1 && vindicators == 1 && evokers == 1,
          "hardcoded mansion loot and mob effects publish once across intersecting chunks");
    if (fixtureDirectoryCreated) {
        fixtureError.clear();
        fs::remove_all(fixtureRoot, fixtureError);
        check(!fixtureError,
              "hardcoded mansion side-effect fixture removes its owned directory");
    }
}

void concurrentLightQueueTransfer() {
    constexpr int y = 40;
    constexpr std::array<std::int32_t, 4> sourceX{{8, 72, 136, 200}};
    World world("minecraft:plains", LevelType::Nether, 0);
    LightEngine light(world);
    // Seal each two-cell test corridor inside opaque blocks. This prevents
    // naturally generated Nether light sources from masking failed removal.
    const auto stone = state("minecraft:stone");
    for (const auto x : sourceX) {
        for (int dx = -1; dx <= 2; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz)
                    world.setBlock(x + dx, y + dy, dz, stone);
        world.setBlock(x + 1, y, 0, 0);
    }
    world.setOnBlockChanged(
        [&light](std::int32_t x, std::int32_t y, std::int32_t z,
                 std::uint16_t oldState, std::uint16_t newState) {
            light.onBlockChanged(x, y, z, oldState, newState);
        });

    const auto glowstone = state("minecraft:glowstone");
    const auto applyConcurrently = [&](std::uint16_t block) {
        std::atomic<std::size_t> ready{0};
        std::atomic<bool> start{false};
        std::atomic<bool> drainerStarted{false};
        std::atomic<bool> writersDone{false};
        std::atomic<bool> writesSucceeded{true};
        std::thread drainer([&] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            light.drain();
            drainerStarted.store(true, std::memory_order_release);
            while (!writersDone.load(std::memory_order_acquire)) {
                light.drain();
                std::this_thread::yield();
            }
        });
        std::vector<std::thread> writers;
        writers.reserve(sourceX.size());
        for (const auto x : sourceX) {
            writers.emplace_back([&, x] {
                ready.fetch_add(1, std::memory_order_release);
                while (!start.load(std::memory_order_acquire))
                    std::this_thread::yield();
                while (!drainerStarted.load(std::memory_order_acquire))
                    std::this_thread::yield();
                const std::uint16_t alternate =
                    block == 0 ? glowstone : std::uint16_t{0};
                // Repeated opposite transitions make the drainer contend with
                // stale remove/add work instead of testing only one enqueue.
                for (int i = 0; i < 16; ++i) {
                    if (!world.setBlock(x, y, 0, block) ||
                        !world.setBlock(x, y, 0, alternate)) {
                        writesSucceeded.store(false, std::memory_order_relaxed);
                        return;
                    }
                    std::this_thread::yield();
                }
                if (!world.setBlock(x, y, 0, block))
                    writesSucceeded.store(false, std::memory_order_relaxed);
            });
        }
        while (ready.load(std::memory_order_acquire) < sourceX.size() + 1)
            std::this_thread::yield();
        start.store(true, std::memory_order_release);
        for (auto& writer : writers) writer.join();
        writersDone.store(true, std::memory_order_release);
        drainer.join();

        // The producer can publish just after the draining thread's final
        // queue swap. Finish on the owning test thread before checking state.
        for (int attempt = 0; attempt < 4 &&
             (light.pendingNodeCount() != 0 || light.pendingSkyCount() != 0);
             ++attempt)
            light.drain();
        return writesSucceeded.load(std::memory_order_relaxed) &&
               light.pendingNodeCount() == 0 && light.pendingSkyCount() == 0;
    };

    const bool placed = applyConcurrently(glowstone);
    bool allEmittersLit = true;
    for (const auto x : sourceX)
        allEmittersLit = allEmittersLit &&
                         light.blockLightAt(x, y, 0) == 15 &&
                         light.blockLightAt(x + 1, y, 0) == 14;
    check(placed && allEmittersLit,
          "concurrent edits propagate block light while the tick drains queues");

    const bool removed = applyConcurrently(0);
    bool allEmittersDark = true;
    for (const auto x : sourceX) {
        allEmittersDark = allEmittersDark &&
                          light.blockLightAt(x, y, 0) == 0 &&
                          light.blockLightAt(x + 1, y, 0) == 0;
        if (light.blockLightAt(x, y, 0) != 0 ||
            light.blockLightAt(x + 1, y, 0) != 0)
            std::printf("light removal diagnostic x=%d source=%u adjacent=%u\n",
                        x, light.blockLightAt(x, y, 0),
                        light.blockLightAt(x + 1, y, 0));
    }
    check(removed && allEmittersDark,
          "concurrent light removals clear neighboring propagated light");

    const auto source = sourceX.front();
    const bool rapidlyReplaced =
        world.setBlock(source, y, 0, glowstone) &&
        world.setBlock(source, y, 0, 0) &&
        world.setBlock(source, y, 0, glowstone);
    for (int attempt = 0; attempt < 4 &&
         light.pendingNodeCount() != 0; ++attempt)
        light.drain();
    const bool replacementLit =
        light.blockLightAt(source, y, 0) == 15 &&
        light.blockLightAt(source + 1, y, 0) == 14;
    check(rapidlyReplaced && replacementLit &&
              light.pendingNodeCount() == 0,
          "a rapid emitter remove-and-replace keeps its current light level");
    const bool replacementRemoved = world.setBlock(source, y, 0, 0);
    for (int attempt = 0; attempt < 4 &&
         light.pendingNodeCount() != 0; ++attempt)
        light.drain();
    check(replacementRemoved && light.blockLightAt(source, y, 0) == 0 &&
              light.blockLightAt(source + 1, y, 0) == 0,
          "removing a rapidly replaced emitter clears its light field");

    World skyWorld("minecraft:plains", LevelType::Flat, 0);
    LightEngine skyLight(skyWorld);
    constexpr int skyY = 100;
    skyWorld.setBlock(0, skyY, 0, 0);
    skyLight.ensureSkyLight(0, 0);
    skyWorld.setSimulationDistanceCallback(
        [](std::int32_t cx, std::int32_t cz) {
            return cx == 0 && cz == 0;
        });
    skyWorld.setOnBlockChanged(
        [&skyLight](std::int32_t x, std::int32_t y, std::int32_t z,
                    std::uint16_t oldState, std::uint16_t newState) {
            skyLight.onBlockChanged(x, y, z, oldState, newState);
        });
    std::atomic<bool> skyEditDone{false};
    std::atomic<bool> skyEditsSucceeded{true};
    std::thread skyDrainer([&] {
        while (!skyEditDone.load(std::memory_order_acquire)) {
            skyLight.drain();
            std::this_thread::yield();
        }
    });
    std::thread skyEditor([&] {
        for (int i = 0; i < 6; ++i) {
            const bool placedStone = skyWorld.setBlock(0, skyY, 0, stone);
            const bool clearedStone = skyWorld.setBlock(0, skyY, 0, 0);
            if (!placedStone || !clearedStone)
                skyEditsSucceeded.store(false, std::memory_order_relaxed);
        }
    });
    skyEditor.join();
    skyEditDone.store(true, std::memory_order_release);
    skyDrainer.join();
    for (int attempt = 0; attempt < 8 &&
         (skyLight.pendingSkyCount() != 0 ||
          !skyWorld.hasSkyLightCache(0, 0)); ++attempt)
        skyLight.drain();
    check(skyEditsSucceeded.load(std::memory_order_relaxed) &&
          skyLight.pendingSkyCount() == 0 &&
              skyWorld.hasSkyLightCache(0, 0),
          "concurrent sky edits leave a valid cache and drain queued rebuilds");
}

void redstoneBounds() {
    World world("minecraft:plains", LevelType::Flat, 0);
    RedstoneEngine engine(world);
    engine.onBlockChanged(std::numeric_limits<std::int32_t>::max(), -60, 0);
    check(!engine.isPoweredHere(std::numeric_limits<std::int32_t>::max(), -60, 0),
          "redstone queries reject out-of-range coordinates");
}

void mobDeathSideEffectsHaveSingleOwner() {
    MobEntity mob;
    check(!mob.tryClaimDeathHandling(),
          "living mobs cannot claim death-side-effect ownership");
    {
        std::lock_guard entityLock(*mob.stateMtx);
        mob.dead = true;
    }

    constexpr int kContenders = 16;
    std::atomic<bool> start{false};
    std::atomic<int> winners{0};
    std::vector<std::thread> contenders;
    contenders.reserve(kContenders);
    for (int i = 0; i < kContenders; ++i) {
        contenders.emplace_back([&] {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            if (mob.tryClaimDeathHandling())
                winners.fetch_add(1, std::memory_order_relaxed);
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& contender : contenders) contender.join();
    check(winners.load(std::memory_order_relaxed) == 1 &&
              !mob.tryClaimDeathHandling(),
          "concurrent tick/session/projectile handlers can claim a mob death only once");
}
}

int main() {
    schedulerKeyCollision();
    randomTickTiming();
    cropLightPosition();
    chorusBlockedAges();
    campfireStateSafety();
    flammableCoverage();
    repeaterDirectSource();
    fluidAndLightBounds();
    waterLightAttenuation();
    netherOpacityRemovalReseedsLight();
    competingEmitterLightLevels();
    lightDrainLinearizesBlockEdits();
    structureSideEffectsFollowChunkPublication();
    configuredFeatureEffectsHaveSingleChunkOwnership();
    hardcodedStructureEffectsHaveSingleChunkOwnership();
    concurrentLightQueueTransfer();
    redstoneBounds();
    mobDeathSideEffectsHaveSingleOwner();
    std::printf("GAMEPLAY BUGS: %d PASS %d FAIL\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
