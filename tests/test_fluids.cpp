// Fluid simulation regression tests.
// Every assertion drives World + FluidSim and inspects the resulting state;
// these are intentionally small deterministic cases rather than liveness
// checks.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <string_view>
#include <atomic>
#include <thread>

#include "generated/BlockStates.hpp"
#include "game/World.hpp"
#include "physics/Fluids.hpp"

using namespace cppfm;

static int g_pass = 0;
static int g_fail = 0;

static void check(bool condition, const char* name) {
    if (condition) {
        ++g_pass;
        std::printf("  PASS %s\n", name);
    } else {
        ++g_fail;
        std::printf("  FAIL %s\n", name);
    }
}

static std::uint16_t blockState(const char* name) {
    const auto* block = gen::blockByName(name);
    return block ? block->minState : 0;
}

static std::uint16_t fluidState(const char* name, int level) {
    const std::string levelText = std::to_string(level);
    return static_cast<std::uint16_t>(gen::stateWithPropsList(
        name, {{"level", levelText}}));
}

static std::string property(std::uint16_t state, const char* name) {
    for (const auto& [key, value] : gen::propsOf(state))
        if (key == name) return std::string(value);
    return {};
}

static std::uint16_t oakStairsState(bool waterlogged) {
    return static_cast<std::uint16_t>(gen::stateWithPropsList(
        "minecraft:oak_stairs",
        {{"facing", "north"}, {"half", "bottom"},
         {"shape", "straight"},
         {"waterlogged", waterlogged ? "true" : "false"}}));
}

static void test_waterlogging() {
    std::printf("\n[1] waterlogging follows block state properties\n");
    check(WaterloggableHelper::isWaterloggable("minecraft:oak_stairs"),
          "oak stairs expose the waterlogged property");
    check(!WaterloggableHelper::isWaterloggable("minecraft:oak_fence_gate"),
          "fence gates remain non-waterloggable in 1.21.4");
    check(!WaterloggableHelper::isWaterloggable("minecraft:redstone_wall_torch"),
          "wall torches are not confused with waterloggable walls");

    const std::uint16_t slab = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:oak_slab",
                                {{"type", "double"}, {"waterlogged", "false"}}));
    check(!WaterloggableHelper::isWaterloggable(slab),
          "double slabs cannot be waterlogged");
}

static void test_water_flow_and_waterlogging() {
    std::printf("\n[2] water flow and waterlogging\n");
    World world("minecraft:plains", LevelType::Flat, 0);
    FluidSim fluids(world);
    constexpr int y = -60;
    const std::uint16_t stairs = static_cast<std::uint16_t>(
        gen::stateWithPropsList("minecraft:oak_stairs",
                                {{"facing", "north"}, {"half", "bottom"},
                                 {"shape", "straight"},
                                 {"waterlogged", "false"}}));
    world.setBlock(0, y, 0, stairs);
    world.setBlock(-1, y, 0, fluidState("minecraft:water", 0));
    fluids.touch(-1, y, 0);
    fluids.tick(0);
    check(property(world.getBlock(0, y, 0), "waterlogged") == "true",
          "water flows into an empty waterloggable block");

    const auto water = FluidSim::getFluidState(world, -1, y, 0);
    check(water.isStillWater(), "source water remains a still source after flow");
}

static void test_source_conversion() {
    std::printf("\n[3] water source conversion\n");
    World world("minecraft:plains", LevelType::Flat, 0);
    FluidSim fluids(world);
    constexpr int y = -60;
    world.setBlock(0, y, 0, fluidState("minecraft:water", 0));
    world.setBlock(2, y, 0, fluidState("minecraft:water", 0));
    world.setBlock(1, y, 0, fluidState("minecraft:water", 1));
    fluids.touch(1, y, 0);
    fluids.tick(0);
    check(FluidSim::getFluidState(world, 1, y, 0).isStillWater(),
          "flowing water between two sources converts to a source");
}

static void test_fluid_interaction() {
    std::printf("\n[4] water/lava interaction\n");
    {
        World world("minecraft:plains", LevelType::Flat, 0);
        FluidSim fluids(world);
        constexpr int y = -60;
        world.setBlock(0, y, 0, fluidState("minecraft:water", 0));
        world.setBlock(1, y, 0, fluidState("minecraft:lava", 0));
        fluids.checkInteraction(world, 1, y, 0,
                                FluidSim::getFluidState(world, 0, y, 0),
                                FluidSim::getFluidState(world, 1, y, 0));
        check(world.getBlock(1, y, 0) == blockState("minecraft:obsidian"),
              "lava source touching water becomes obsidian");
    }
    {
        World world("minecraft:plains", LevelType::Flat, 0);
        FluidSim fluids(world);
        constexpr int y = -60;
        world.setBlock(0, y, 0, fluidState("minecraft:water", 0));
        world.setBlock(1, y, 0, fluidState("minecraft:lava", 1));
        fluids.checkInteraction(world, 1, y, 0,
                                FluidSim::getFluidState(world, 0, y, 0),
                                FluidSim::getFluidState(world, 1, y, 0));
        check(world.getBlock(1, y, 0) == blockState("minecraft:cobblestone"),
              "flowing lava touching water becomes cobblestone");
    }
}

static void test_state_classification() {
    std::printf("\n[5] fluid state classification\n");
    World world("minecraft:plains", LevelType::Flat, 0);
    constexpr int y = -60;
    world.setBlock(0, y, 0, fluidState("minecraft:water", 0));
    world.setBlock(1, y, 0, fluidState("minecraft:water", 1));
    world.setBlock(2, y, 0, fluidState("minecraft:water", 15));
    world.setBlock(3, y, 0, fluidState("minecraft:lava", 8));

    const auto source = FluidSim::getFluidState(world, 0, y, 0);
    const auto flowing = FluidSim::getFluidState(world, 1, y, 0);
    const auto falling = FluidSim::getFluidState(world, 2, y, 0);
    const auto lavaFalling = FluidSim::getFluidState(world, 3, y, 0);
    check(source.isStillWater(), "level 0 is a still water source");
    check(flowing.isWater() && !flowing.falling && flowing.level == 1,
          "levels 1 through 7 are flowing water");
    check(falling.isWater() && falling.falling && falling.level == 8,
          "levels 8 through 15 are the clamped falling state");
    check(lavaFalling.isLava() && lavaFalling.falling && lavaFalling.level == 8,
          "falling lava uses the same canonical level");
}

static void test_directional_interaction_and_replaceable() {
    std::printf("\n[6] directional interaction and replaceable blocks\n");
    constexpr int y = -60;
    {
        World world("minecraft:plains", LevelType::Flat, 0);
        FluidSim fluids(world);
        world.setBlock(0, y, 0, fluidState("minecraft:lava", 8));
        world.setBlock(0, y - 1, 0, fluidState("minecraft:water", 0));
        fluids.touch(0, y, 0);
        fluids.tick(0);
        check(world.getBlock(0, y - 1, 0) == blockState("minecraft:stone"),
              "falling lava onto a water source makes stone");
    }
    {
        World world("minecraft:plains", LevelType::Flat, 0);
        FluidSim fluids(world);
        world.setBlock(0, y, 0, fluidState("minecraft:water", 0));
        world.setBlock(1, y, 0, blockState("minecraft:fire"));
        world.setBlock(0, y, 1, blockState("minecraft:glass"));
        fluids.touch(0, y, 0);
        fluids.tick(0);
        check(FluidSim::getFluidState(world, 1, y, 0).isWater(),
              "water replaces an explicitly replaceable fire block");
        check(world.getBlock(0, y, 1) == blockState("minecraft:glass"),
              "water does not replace transparent non-replaceable glass");
    }
    {
        World world("minecraft:plains", LevelType::Flat, 0);
        FluidSim fluids(world);
        world.setBlock(0, y, 0, fluidState("minecraft:lava", 8));
        world.setBlock(0, y - 1, 0, oakStairsState(true));
        fluids.touch(0, y, 0);
        fluids.tick(0);
        check(property(world.getBlock(0, y - 1, 0), "waterlogged") == "true",
              "falling lava stops at a waterlogged block without making stone");
    }
}

static void test_nether_water_and_lava_timing() {
    std::printf("\n[7] Nether fluid rules\n");
    constexpr int y = -60;
    World world("minecraft:plains", LevelType::Nether, 0);
    world.setDimensionId(-1);
    FluidSim fluids(world);
    world.setBlock(0, y - 1, 0, blockState("minecraft:netherrack"));
    world.setBlock(0, y, 0, fluidState("minecraft:lava", 0));
    world.setBlock(1, y, 0, 0);
    world.setBlock(2, y, 0, 0);
    fluids.touch(0, y, 0);
    fluids.tick(0);
    check(world.getBlock(2, y, 0) == 0,
          "Nether lava does not spread before its ten-tick delay");
    fluids.tick(10);
    check(FluidSim::getFluidState(world, 2, y, 0).isLava(),
          "Nether lava follows the dimension-specific scheduled delay");

    World waterWorld("minecraft:plains", LevelType::Nether, 0);
    waterWorld.setDimensionId(-1);
    FluidSim water(waterWorld);
    waterWorld.setBlock(0, y, 0, fluidState("minecraft:water", 0));
    water.touch(0, y, 0);
    water.tick(0);
    check(waterWorld.getBlock(0, y, 0) == 0,
          "an actual water LiquidBlock evaporates in the Nether");
}

static void test_concurrent_notifications() {
    std::printf("\n[8] concurrent callback notifications and fluid ticks\n");
    World world("minecraft:plains", LevelType::Flat, 0);
    FluidSim fluids(world);
    constexpr int notificationCount = 1000;
    std::atomic<bool> go{false};

    std::thread callbackThread([&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int i = 0; i < notificationCount; ++i)
            fluids.touch(i & 15, -60, (i >> 4) & 15);
    });
    std::thread tickThread([&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        for (std::int64_t now = 0; now < notificationCount; ++now)
            fluids.tick(now);
    });
    go.store(true, std::memory_order_release);
    callbackThread.join();
    tickThread.join();

    check(fluids.pending() <= static_cast<std::size_t>(notificationCount),
          "concurrent touch/tick preserves a bounded scheduling queue");

    World dedupWorld("minecraft:plains", LevelType::Flat, 0);
    FluidSim dedup(dedupWorld);
    for (int i = 0; i < notificationCount; ++i)
        dedup.touch(2, -60, 2);
    check(dedup.pending() == 1,
          "repeated fluid notifications for one block are coalesced");
    dedup.tick(0);
    check(dedup.pending() == 0,
          "coalesced fluid notification is removed after processing");
}

int main() {
    std::printf("=== test_fluids ===\n");
    test_waterlogging();
    test_water_flow_and_waterlogging();
    test_source_conversion();
    test_fluid_interaction();
    test_state_classification();
    test_directional_interaction_and_replaceable();
    test_nether_water_and_lava_timing();
    test_concurrent_notifications();
    std::printf("\n=== FLUIDS: %d PASS %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
