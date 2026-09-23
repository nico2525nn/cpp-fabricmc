#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

#include "generated/BlockStates.hpp"
#include "game/World.hpp"
#include "game/GameServer.hpp"
#include "physics/BlockTickScheduler.hpp"
#include "physics/Fluids.hpp"
#include "physics/LightEngine.hpp"
#include "physics/Redstone.hpp"

using namespace cppfm;

// The standalone goal harness links the gameplay object set without the
// production server translation units.  These no-op sinks keep calls made
// only when a non-null GameServer is supplied out of the harness while still
// exercising every owned behavior through its shipped implementation.
namespace cppfm {
bool GameServer::isChunkInSimulationDistanceFor(std::int8_t, std::int32_t,
                                                std::int32_t) const { return true; }
void GameServer::broadcastBlockChangeFor(std::int8_t, std::int32_t,
                                         std::int32_t, std::int32_t,
                                         std::uint16_t) {}
void GameServer::broadcastPaleOakLeavesParticleFor(std::int8_t, double,
                                                   double, double) {}
void GameServer::broadcastSoundFor(std::int8_t, const char*, double, double,
                                   double, float, float, const char*) {}
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

void redstoneBounds() {
    World world("minecraft:plains", LevelType::Flat, 0);
    RedstoneEngine engine(world);
    engine.onBlockChanged(std::numeric_limits<std::int32_t>::max(), -60, 0);
    check(!engine.isPoweredHere(std::numeric_limits<std::int32_t>::max(), -60, 0),
          "redstone queries reject out-of-range coordinates");
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
    redstoneBounds();
    std::printf("GAMEPLAY BUGS: %d PASS %d FAIL\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
