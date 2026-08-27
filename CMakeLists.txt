cmake_minimum_required(VERSION 3.20)
project(cppfabricmc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

add_executable(cppfm
    src/main.cpp
    src/core/NBT.cpp
    src/game/GameServer.cpp
    src/game/Recipes.cpp
    src/game/MenuInteraction.cpp
    src/game/Commands.cpp
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
    src/game/InventoryController.cpp
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
    src/game/WorldGen.cpp
    src/game/Recipes.cpp
    src/game/MenuInteraction.cpp
    src/game/Containers.cpp
    src/game/Commands.cpp
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
    src/game/MobSpawner.cpp)
    src/game/HungerManager.cpp
    src/game/CombatManager.cpp
    src/game/InventoryController.cpp)
target_include_directories(test_native PRIVATE src src/generated tests)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(test_native PRIVATE Threads::Threads ZLIB::ZLIB OpenSSL::Crypto)

# self-contained: spawns the server itself on a random port
add_test(NAME native COMMAND test_native $<TARGET_FILE:cppfm>
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(native PROPERTIES TIMEOUT 300)

add_executable(test_smoke_80
    tests/TestClient.cpp
    tests/test_smoke_80.cpp
    src/core/NBT.cpp
    src/game/GameServer.cpp
    src/game/WorldGen.cpp
    src/game/Recipes.cpp
    src/game/MenuInteraction.cpp
    src/game/Containers.cpp
    src/game/Commands.cpp
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
    src/game/MobSpawner.cpp)
    src/game/HungerManager.cpp
    src/game/CombatManager.cpp
    src/game/InventoryController.cpp)
target_include_directories(test_smoke_80 PRIVATE src src/generated tests)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
target_link_libraries(test_smoke_80 PRIVATE Threads::Threads ZLIB::ZLIB OpenSSL::Crypto)
add_test(NAME smoke80 COMMAND test_smoke_80 $<TARGET_FILE:cppfm>
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(smoke80 PROPERTIES TIMEOUT 400)
