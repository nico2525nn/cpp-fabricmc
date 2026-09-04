cmake_minimum_required(VERSION 3.20)
project(cppfabricmc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -pipe)

add_executable(cppfm
    src/main.cpp
    src/core/NBT.cpp
    src/game/GameServer.cpp
    src/game/GameServer_tick.cpp
    src/game/GameServer_combat.cpp
    src/game/GameServer_items.cpp
    src/game/GameServer_world.cpp
    src/game/GameServer_session.cpp
    src/game/GameServer_core.cpp
    src/game/Recipes.cpp
    src/game/MenuInteraction.cpp
    src/game/Commands.cpp
    src/game/commands_chat.cpp
    src/game/commands_admin.cpp
    src/game/commands_world.cpp
    src/game/commands_player.cpp
    src/game/commands_data.cpp
    src/game/commands_misc.cpp
    src/game/commands_execute.cpp
    src/game/commands_scoreboard.cpp
    src/game/Containers.cpp
    src/physics/LightEngine.cpp
    src/physics/Fluids.cpp
    src/physics/Redstone.cpp
    src/physics/BlockTickScheduler.cpp
    src/game/WorldGen.cpp
    src/worldgen/DensityFunction.cpp
    src/worldgen/MultiNoise.cpp
    src/worldgen/Structures.cpp
    src/worldgen/StructurePlacer.cpp
    src/worldgen/ChunkGenerator.cpp
    src/worldgen/StructureManager.cpp
    src/game/WorldDataManager.cpp
    src/game/AiBrain.cpp
    src/game/Stats.cpp
    src/net/PacketBatcher.cpp
    src/game/BehaviorTree.cpp
    src/game/EntityData.cpp
    src/game/BossAI.cpp
    src/game/MenuLogic.cpp
    src/game/MobSpawner.cpp
    src/game/HungerManager.cpp
    src/game/CombatManager.cpp
    src/game/CombatShield.cpp
    src/game/InventoryController.cpp
    src/game/FunctionEvaluator.cpp
)

target_include_directories(cppfm PRIVATE src src/generated)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
target_link_libraries(cppfm PRIVATE Threads::Threads ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto)

add_executable(test_golden tests/test_golden.cpp src/core/NBT.cpp
    src/game/WorldGen.cpp
    src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp
    src/worldgen/Structures.cpp src/worldgen/StructurePlacer.cpp
    src/worldgen/ChunkGenerator.cpp src/worldgen/StructureManager.cpp src/game/WorldDataManager.cpp)
target_include_directories(test_golden PRIVATE src src/generated)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(test_golden PRIVATE ZLIB::ZLIB OpenSSL::Crypto)

enable_testing()
add_executable(test_native
    tests/TestClient.cpp
    tests/native_integration.cpp
    src/core/NBT.cpp
    src/game/GameServer.cpp
    src/game/GameServer_tick.cpp
    src/game/GameServer_combat.cpp
    src/game/GameServer_items.cpp
    src/game/GameServer_world.cpp
    src/game/GameServer_session.cpp
    src/game/GameServer_core.cpp
    src/game/WorldGen.cpp
    src/game/Recipes.cpp
    src/game/MenuInteraction.cpp
    src/game/Containers.cpp
    src/game/Commands.cpp
    src/game/commands_chat.cpp
    src/game/commands_admin.cpp
    src/game/commands_world.cpp
    src/game/commands_player.cpp
    src/game/commands_data.cpp
    src/game/commands_misc.cpp
    src/game/commands_execute.cpp
    src/game/commands_scoreboard.cpp
    src/game/AiBrain.cpp
    src/game/Stats.cpp
    src/physics/LightEngine.cpp
    src/physics/Fluids.cpp
    src/physics/Redstone.cpp
    src/physics/BlockTickScheduler.cpp
    src/worldgen/DensityFunction.cpp
    src/worldgen/MultiNoise.cpp
    src/worldgen/Structures.cpp
    src/worldgen/StructurePlacer.cpp
    src/worldgen/ChunkGenerator.cpp
    src/worldgen/StructureManager.cpp
    src/game/WorldDataManager.cpp
    src/net/PacketBatcher.cpp
    src/game/BehaviorTree.cpp
    src/game/EntityData.cpp
    src/game/BossAI.cpp
    src/game/MenuLogic.cpp
    src/game/MobSpawner.cpp
    src/game/HungerManager.cpp
    src/game/CombatManager.cpp
    src/game/CombatShield.cpp
    src/game/InventoryController.cpp
    src/game/FunctionEvaluator.cpp)
target_include_directories(test_native PRIVATE src src/generated tests)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(test_native PRIVATE Threads::Threads ZLIB::ZLIB OpenSSL::Crypto)

# self-contained: spawns the server itself on a random port
add_test(NAME native COMMAND test_native $<TARGET_FILE:cppfm>
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(native PROPERTIES TIMEOUT 300)

# plan28 D26: header-only ResetScore 0x49 wire + wildcard reset lock
add_executable(test_scoreboard_reset tests/test_scoreboard_reset.cpp)
target_include_directories(test_scoreboard_reset PRIVATE src src/generated)
add_test(NAME scoreboard_reset COMMAND test_scoreboard_reset)
set_tests_properties(scoreboard_reset PROPERTIES TIMEOUT 30)

add_executable(test_spec_wire tests/test_spec_wire.cpp
    src/core/NBT.cpp src/game/Stats.cpp
    src/game/WorldGen.cpp src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp
    src/worldgen/Structures.cpp src/worldgen/StructurePlacer.cpp
    src/worldgen/ChunkGenerator.cpp src/worldgen/StructureManager.cpp src/game/WorldDataManager.cpp)
target_include_directories(test_spec_wire PRIVATE src src/generated)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(test_spec_wire PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME spec_wire COMMAND test_spec_wire)
set_tests_properties(spec_wire PROPERTIES TIMEOUT 30)

add_executable(test_smoke_80
    tests/TestClient.cpp
    tests/test_smoke_80.cpp
    src/core/NBT.cpp
    src/game/GameServer.cpp
    src/game/GameServer_tick.cpp
    src/game/GameServer_combat.cpp
    src/game/GameServer_items.cpp
    src/game/GameServer_world.cpp
    src/game/GameServer_session.cpp
    src/game/GameServer_core.cpp
    src/game/WorldGen.cpp
    src/game/Recipes.cpp
    src/game/MenuInteraction.cpp
    src/game/Containers.cpp
    src/game/Commands.cpp
    src/game/commands_chat.cpp
    src/game/commands_admin.cpp
    src/game/commands_world.cpp
    src/game/commands_player.cpp
    src/game/commands_data.cpp
    src/game/commands_misc.cpp
    src/game/commands_execute.cpp
    src/game/commands_scoreboard.cpp
    src/game/AiBrain.cpp
    src/game/Stats.cpp
    src/physics/LightEngine.cpp
    src/physics/Fluids.cpp
    src/physics/Redstone.cpp
    src/physics/BlockTickScheduler.cpp
    src/worldgen/DensityFunction.cpp
    src/worldgen/MultiNoise.cpp
    src/worldgen/Structures.cpp
    src/worldgen/StructurePlacer.cpp
    src/worldgen/ChunkGenerator.cpp
    src/worldgen/StructureManager.cpp
    src/game/WorldDataManager.cpp
    src/net/PacketBatcher.cpp
    src/game/BehaviorTree.cpp
    src/game/EntityData.cpp
    src/game/BossAI.cpp
    src/game/MenuLogic.cpp
    src/game/MobSpawner.cpp
    src/game/HungerManager.cpp
    src/game/CombatManager.cpp
    src/game/CombatShield.cpp
    src/game/InventoryController.cpp
    src/game/FunctionEvaluator.cpp)
target_include_directories(test_smoke_80 PRIVATE src src/generated tests)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(test_smoke_80 PRIVATE Threads::Threads ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME smoke80 COMMAND test_smoke_80 $<TARGET_FILE:cppfm>
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(smoke80 PROPERTIES TIMEOUT 700)

# plan43 B1+B2 live-server tests (W-01..W-07, W-12; see docs/VERIFICATION.md) — TestClient + cppfm
add_executable(test_plan43
    tests/TestClient.cpp
    tests/test_plan43.cpp
    src/core/NBT.cpp)
target_include_directories(test_plan43 PRIVATE src src/generated tests)
target_link_libraries(test_plan43 PRIVATE Threads::Threads ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME plan43 COMMAND test_plan43 $<TARGET_FILE:cppfm>
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(plan43 PROPERTIES TIMEOUT 300)

# plan46 §1 (O-13/W-14/W-16) flood/defense: unit (budgets/throttle/gate) +
# live-server (fork cppfm, raw sockets + TestClient) A1–A8 scenarios.
add_executable(test_flood_net
    tests/TestClient.cpp
    tests/test_flood_net.cpp
    src/core/NBT.cpp)
target_include_directories(test_flood_net PRIVATE src src/generated tests)
target_link_libraries(test_flood_net PRIVATE Threads::Threads ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME flood_net COMMAND test_flood_net $<TARGET_FILE:cppfm>
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(flood_net PROPERTIES TIMEOUT 300)

add_executable(test_fuzz tests/test_fuzz.cpp
    src/core/NBT.cpp)
target_include_directories(test_fuzz PRIVATE src src/generated)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(test_fuzz PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME fuzz COMMAND test_fuzz)
set_tests_properties(fuzz PROPERTIES TIMEOUT 30)

# wt42/test-wire: exhaustive vanilla-exact wire coverage (131 play toClient, Prismarine protocol.json live-fetch)
add_executable(test_wire_full tests/test_wire_full.cpp
    src/core/NBT.cpp src/game/Stats.cpp
    src/game/WorldGen.cpp src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp
    src/worldgen/Structures.cpp src/worldgen/StructurePlacer.cpp
    src/worldgen/ChunkGenerator.cpp src/worldgen/StructureManager.cpp src/game/WorldDataManager.cpp)
target_include_directories(test_wire_full PRIVATE src src/generated)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(test_wire_full PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME wire_full COMMAND test_wire_full)
set_tests_properties(wire_full PROPERTIES TIMEOUT 30)

# plan45 B6: remaining fromClient (W-05/W-08/W-09/W-10/W-11/W-13) + G-13 network side
add_executable(test_wire_b6 tests/test_wire_b6.cpp
    src/core/NBT.cpp
    src/game/Recipes.cpp src/game/Containers.cpp src/game/MenuInteraction.cpp)
target_include_directories(test_wire_b6 PRIVATE src src/generated)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(test_wire_b6 PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME wire_b6 COMMAND test_wire_b6)
set_tests_properties(wire_b6 PROPERTIES TIMEOUT 60)

# plan36 soak 2h nightly (300s dry + 7200s full) — B-06
add_test(NAME soak2h COMMAND python3 tests/soak_test.py --duration 7200 --binary $<TARGET_FILE:cppfm> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(soak2h PROPERTIES TIMEOUT 8000 LABELS nightly)

add_executable(test_gameplay_full tests/test_gameplay_full.cpp
    src/core/NBT.cpp
    src/physics/Redstone.cpp
    src/game/MenuInteraction.cpp src/game/Containers.cpp src/game/Recipes.cpp
    src/physics/BlockTickScheduler.cpp
    src/game/CombatShield.cpp
    src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp src/worldgen/Structures.cpp src/worldgen/StructurePlacer.cpp src/worldgen/ChunkGenerator.cpp src/worldgen/StructureManager.cpp src/game/WorldGen.cpp src/game/WorldDataManager.cpp)
target_include_directories(test_gameplay_full PRIVATE src src/generated)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(test_gameplay_full PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME gameplay_full COMMAND test_gameplay_full)
set_tests_properties(gameplay_full PROPERTIES TIMEOUT 60)

# plan45 G-11 — seed parity L1/L2 (independent Python hand-calc + determinism)
add_executable(test_seed_parity tests/test_seed_parity.cpp
    src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp src/worldgen/Structures.cpp src/worldgen/StructurePlacer.cpp src/worldgen/StructureManager.cpp)
target_include_directories(test_seed_parity PRIVATE src src/generated)
add_test(NAME seed_parity COMMAND test_seed_parity)
set_tests_properties(seed_parity PROPERTIES TIMEOUT 120)

# plan44 G-02/G-03 — hardness 1095 lock + mining calculator (generated worktree)
add_executable(test_block_hardness_full tests/test_block_hardness_full.cpp)
target_include_directories(test_block_hardness_full PRIVATE src tests)
add_test(NAME block_hardness_full COMMAND test_block_hardness_full)
set_tests_properties(block_hardness_full PROPERTIES TIMEOUT 60)

# plan44 G-12: redstone engine verification (World + RedstoneEngine public API) — restored after merge conflict
add_executable(test_redstone_engine_full tests/test_redstone_engine_full.cpp
    src/core/NBT.cpp
    src/physics/Redstone.cpp
    src/game/MenuInteraction.cpp src/game/Containers.cpp src/game/Recipes.cpp
    src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp src/worldgen/Structures.cpp src/worldgen/StructurePlacer.cpp src/worldgen/ChunkGenerator.cpp src/worldgen/StructureManager.cpp src/game/WorldGen.cpp src/game/WorldDataManager.cpp)
target_include_directories(test_redstone_engine_full PRIVATE src src/generated)
target_link_libraries(test_redstone_engine_full PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME redstone_engine_full COMMAND test_redstone_engine_full)
set_tests_properties(redstone_engine_full PROPERTIES TIMEOUT 60)

# plan44 G-05: mob stats 149 lock — restored after merge conflict
add_executable(test_mob_stats_full tests/test_mob_stats_full.cpp)
target_include_directories(test_mob_stats_full PRIVATE src src/generated)
add_test(NAME mob_stats_full COMMAND test_mob_stats_full)
set_tests_properties(mob_stats_full PROPERTIES TIMEOUT 30)

add_executable(test_mining_full tests/test_mining_full.cpp)
target_include_directories(test_mining_full PRIVATE src tests)
add_test(NAME mining_full COMMAND test_mining_full)
set_tests_properties(mining_full PROPERTIES TIMEOUT 60)

# plan39 C-03 weak_zero gate — grep 0 strict
add_test(NAME weak_zero COMMAND sh -c "test $(grep -rn '|| true' ${CMAKE_SOURCE_DIR}/tests --include='*.cpp' | wc -l) -eq 0")
set_tests_properties(weak_zero PROPERTIES TIMEOUT 10)

# plan41 C-09 bench strict gate — p50<5 p95<10 hit>80
add_test(NAME bench COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/bench_chunk_gen.py --view-distance 32 --chunks 100 --dry --strict)
set_tests_properties(bench PROPERTIES TIMEOUT 30 LABELS bench)

# plan41 C-12 bot verification — 3-clients + soak (python, no binary deps)
add_test(NAME multi_client COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/multi_client_test.py --binary $<TARGET_FILE:cppfm>)
set_tests_properties(multi_client PROPERTIES TIMEOUT 120 LABELS "multi;network" WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME bot_smoke COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/bot_smoke.py --duration 30 --binary $<TARGET_FILE:cppfm>)
set_tests_properties(bot_smoke PROPERTIES TIMEOUT 120 LABELS "bot;smoke" WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME soak_bot COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/soak_bot.py --duration 300 --binary $<TARGET_FILE:cppfm>)
set_tests_properties(soak_bot PROPERTIES TIMEOUT 400 LABELS "soak;nightly" WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})


# plan41 C-11 recipes mirror/offset strict (20+ cases) — inventory
add_executable(test_recipes_mirror
    tests/test_recipes_mirror.cpp
    src/core/NBT.cpp
    src/game/Recipes.cpp)
target_include_directories(test_recipes_mirror PRIVATE src src/generated)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(test_recipes_mirror PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME recipes_mirror COMMAND test_recipes_mirror WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(recipes_mirror PROPERTIES TIMEOUT 30 LABELS inventory)

# plan46 §2 recovery (O-07/O-08): offline world-integrity checker
add_executable(check_world
    tools/check_world.cpp
    src/core/NBT.cpp)
target_include_directories(check_world PRIVATE src src/generated)
find_package(ZLIB REQUIRED)
target_link_libraries(check_world PRIVATE ZLIB::ZLIB)

# plan46 §2 recovery (O-07/O-08): loadWithRecovery matrix + quarantine + session.lock
add_executable(test_recovery tests/test_recovery.cpp
    src/core/NBT.cpp
    src/game/WorldGen.cpp
    src/worldgen/DensityFunction.cpp src/worldgen/MultiNoise.cpp
    src/worldgen/Structures.cpp src/worldgen/StructurePlacer.cpp
    src/worldgen/ChunkGenerator.cpp src/worldgen/StructureManager.cpp
    src/game/WorldDataManager.cpp)
target_include_directories(test_recovery PRIVATE src src/generated)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(test_recovery PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME recovery COMMAND test_recovery WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(recovery PROPERTIES TIMEOUT 120 LABELS recovery)

# plan46 §2 (O-09): 5x concurrent RCON + 10x wrong-pass harness (in-process)
add_executable(test_rcon_multi tests/test_rcon_multi.cpp)
target_include_directories(test_rcon_multi PRIVATE src src/generated)
find_package(Threads REQUIRED)
target_link_libraries(test_rcon_multi PRIVATE Threads::Threads)
add_test(NAME rcon_multi COMMAND test_rcon_multi WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(rcon_multi PROPERTIES TIMEOUT 60 LABELS recovery)

# plan44 G-05 mob attributes 149 species (world; see docs/SPEC_GAMEPLAY.md) — header-only, reads docs/mob_stats_149.csv
