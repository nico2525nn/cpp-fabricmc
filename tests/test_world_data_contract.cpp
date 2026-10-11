#define CPPFM_PERSISTENCE_PUBLICATION_TEST_HOOKS
#include "game/World.hpp"
#include "game/ChunkCodec.hpp"
#include "game/Anvil.hpp"
#include "game/BlockEntities.hpp"
#include "game/BlockStateTraits.hpp"
#include "game/Persistence.hpp"
#include "core/NBT.hpp"
#include "../src/worldgen/PortalHandler.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int failures = 0;
int assertions = 0;

void check(bool condition, const char* description) {
    ++assertions;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", description);
}

std::uint64_t hashBytes(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t hashChunk(const cppfm::Chunk& chunk) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto add = [&](std::uint8_t byte) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    };
    for (std::size_t i = 0; i < cppfm::kSectionsPerChunk * 4096; ++i) {
        const auto block = chunk.blockAt(i);
        add(static_cast<std::uint8_t>(block));
        add(static_cast<std::uint8_t>(block >> 8));
    }
    for (const auto biome : chunk.biomes) {
        add(static_cast<std::uint8_t>(biome));
        add(static_cast<std::uint8_t>(biome >> 8));
    }
    for (const auto light : chunk.blockLightNib) add(light);
    const bool hasSky = chunk.hasSkyLightStorage();
    add(hasSky ? 1 : 0);
    add(chunk.skyLightReady() ? 1 : 0);
    if (hasSky) {
        for (std::size_t i = 0; i < cppfm::kSectionsPerChunk * 2048; ++i) {
            const auto low = chunk.skyLightAt(i * 2);
            const auto high = chunk.skyLightAt(i * 2 + 1);
            add(static_cast<std::uint8_t>(low | (high << 4)));
        }
    }
    return hash;
}

// These hashes lock this implementation's output for regression detection;
// they are not vanilla-derived world-generation parity oracles.
void testGeneratedChunkRegressionBaselines() {
    struct Case {
        const char* name;
        cppfm::LevelType type;
        std::int8_t dimension;
        std::uint64_t chunkHash;
        std::uint64_t bodyHash;
        std::uint64_t nbtHash;
    };
    constexpr std::uint64_t seed = 0x51A7C0DEULL;
    const std::array<Case, 3> cases{{
        {"overworld", cppfm::LevelType::Normal, 0,
         0xC5F64711F9E27A07ULL, 0x90F327FFA5F2D279ULL,
         0x4B858C315553CEC8ULL},
        {"nether", cppfm::LevelType::Nether, -1,
         0x144228EFD14CE393ULL, 0xCFFC8DE38720C488ULL,
         0x2A5CA68A6A29E07DULL},
        {"end", cppfm::LevelType::End, 1,
         0xB75288B4C3830685ULL, 0x5AF1F2322ADEE109ULL,
         0x2898DE155668123AULL},
    }};

    for (const auto& test : cases) {
        cppfm::World world("minecraft:plains", test.type, seed);
        world.setDimensionId(test.dimension);
        world.generateChunkIfMissing(0, 0);
        const auto chunk = world.tryGetCopy(0, 0);
        check(chunk.has_value(), "fixed-seed chunk is generated");
        if (!chunk) continue;
        const auto actualChunkHash = hashChunk(*chunk);
        if (actualChunkHash != test.chunkHash)
            std::fprintf(stderr,
                         "[baseline] %s chunk hash: expected=%016llx actual=%016llx\n",
                         test.name,
                         static_cast<unsigned long long>(test.chunkHash),
                         static_cast<unsigned long long>(actualChunkHash));
        check(actualChunkHash == test.chunkHash,
              "fixed-seed chunk content matches the regression baseline");

        cppfm::WriteBuffer body;
        cppfm::serializeLevelChunkBody(body, 0, 0, *chunk, 40,
                                       world.verticalGeometry());
        const auto actualBodyHash = hashBytes(body.data);
        if (actualBodyHash != test.bodyHash)
            std::fprintf(stderr,
                         "[baseline] %s dimension-projected wire body hash: expected=%016llx actual=%016llx\n",
                         test.name,
                         static_cast<unsigned long long>(test.bodyHash),
                         static_cast<unsigned long long>(actualBodyHash));
        check(actualBodyHash == test.bodyHash,
              "generated chunk wire body matches the dimension projection regression baseline");
        const auto root = cppfm::chunkToNBT(
            0, 0, *chunk, "minecraft:plains", nullptr,
            world.verticalGeometry());
        cppfm::WriteBuffer encodedNbt;
        cppfm::nbt::writeFileRoot(encodedNbt, root);
        const auto actualNbtHash = hashBytes(encodedNbt.data);
        if (actualNbtHash != test.nbtHash)
            std::fprintf(stderr,
                         "[baseline] %s Anvil NBT hash: expected=%016llx actual=%016llx\n",
                         test.name,
                         static_cast<unsigned long long>(test.nbtHash),
                         static_cast<unsigned long long>(actualNbtHash));
        check(actualNbtHash == test.nbtHash,
              "generated chunk NBT matches the fixed-seed Anvil regression baseline");
    }
}

void testAnvilHeightmapTypes() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Flat, 123);
    world.generateChunkIfMissing(0, 0);
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    const auto grass = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:short_grass"));
    const auto leaves = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:oak_leaves"));
    const auto water = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:water"));
    const auto waterloggedChain = static_cast<std::uint16_t>(
        cppfm::gen::stateWithPropsList(
            "minecraft:chain", {{"axis", "y"}, {"waterlogged", "true"}}));
    const auto torchflower = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:torchflower"));
    const auto redstoneTorch = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:redstone_torch"));
    const auto redstoneWallTorch = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:redstone_wall_torch"));
    check(!cppfm::isOceanFloor(waterloggedChain) &&
              cppfm::isMotionBlocking(waterloggedChain),
          "waterlogged non-solid blocks count only for MOTION_BLOCKING, not OCEAN_FLOOR");
    check(!cppfm::blocksMotion(torchflower) &&
              !cppfm::isMotionBlocking(torchflower) &&
              !cppfm::isOceanFloor(torchflower) &&
              !cppfm::isMotionBlockingNoLeaves(torchflower),
          "torchflower is excluded from vanilla motion-blocking heightmaps");
    check(!cppfm::blocksMotion(redstoneTorch) &&
              !cppfm::isMotionBlocking(redstoneTorch) &&
              !cppfm::isOceanFloor(redstoneTorch) &&
              !cppfm::isMotionBlockingNoLeaves(redstoneTorch) &&
              !cppfm::blocksMotion(redstoneWallTorch) &&
              !cppfm::isMotionBlocking(redstoneWallTorch) &&
              !cppfm::isOceanFloor(redstoneWallTorch) &&
              !cppfm::isMotionBlockingNoLeaves(redstoneWallTorch),
          "redstone torches do not raise vanilla motion-blocking heightmaps");
    check(world.setBlock(4, 0, 4, stone) &&
              world.setBlock(4, 1, 4, grass) &&
              world.setBlock(5, 0, 4, stone) &&
              world.setBlock(5, 1, 4, leaves) &&
              world.setBlock(6, 0, 4, stone) &&
              world.setBlock(6, 1, 4, water) &&
              world.setBlock(7, 0, 4, stone) &&
              world.setBlock(7, 1, 4, redstoneTorch) &&
              world.setBlock(8, 0, 4, stone) &&
              world.setBlock(8, 1, 4, redstoneWallTorch),
          "heightmap fixture distinguishes foliage, leaves, fluids, and redstone torches");
    const auto chunk = world.tryGetCopy(0, 0);
    check(chunk.has_value(), "heightmap fixture chunk is available");
    if (!chunk) return;

    const auto root = cppfm::chunkToNBT(0, 0, *chunk, "minecraft:plains");
    const auto* heightmaps = root.get("Heightmaps");
    const auto* savedMotion = heightmaps
        ? heightmaps->get("MOTION_BLOCKING") : nullptr;
    const auto* savedSurface = heightmaps
        ? heightmaps->get("WORLD_SURFACE") : nullptr;
    const auto* savedWorldSurfaceWg = heightmaps
        ? heightmaps->get("WORLD_SURFACE_WG") : nullptr;
    const auto* savedOceanFloorWg = heightmaps
        ? heightmaps->get("OCEAN_FLOOR_WG") : nullptr;
    const auto* savedOceanFloor = heightmaps
        ? heightmaps->get("OCEAN_FLOOR") : nullptr;
    const auto* savedMotionNoLeaves = heightmaps
        ? heightmaps->get("MOTION_BLOCKING_NO_LEAVES") : nullptr;
    std::vector<std::int64_t> expectedSurface, expectedMotion;
    cppfm::packHeightmaps(expectedSurface, expectedMotion, *chunk);
    const auto allHeightmaps = cppfm::packAnvilHeightmaps(*chunk);
    const auto packedAt = [](const std::vector<std::int64_t>& packed,
                             int index) {
        constexpr int bits = 9;
        constexpr int perLong = 64 / bits;
        return static_cast<int>(
            (static_cast<std::uint64_t>(packed[index / perLong]) >>
             ((index % perLong) * bits)) & 0x1FFU);
    };
    const int column = 4 * 16 + 4;
    const bool distinct = packedAt(expectedSurface, column) == 66 &&
                          packedAt(expectedMotion, column) == 65 &&
                          expectedSurface != expectedMotion;
    check(distinct,
          "WORLD_SURFACE includes grass while MOTION_BLOCKING stops at the solid block");
    check(savedMotion && savedMotion->tag == cppfm::nbt::LongArray &&
              savedMotion->longArray == expectedMotion && savedSurface &&
              savedSurface->tag == cppfm::nbt::LongArray &&
              savedSurface->longArray == expectedSurface,
          "Anvil Heightmaps serialize the same distinct arrays as wire encoding");
    const auto atColumn = [&](const std::vector<std::int64_t>& packed,
                              int x) {
        return packedAt(packed, 4 * 16 + x);
    };
    const bool allTypesMatch = savedWorldSurfaceWg && savedOceanFloorWg &&
        savedOceanFloor && savedMotionNoLeaves &&
        savedWorldSurfaceWg->tag == cppfm::nbt::LongArray &&
        savedWorldSurfaceWg->longArray == allHeightmaps.worldSurfaceWg &&
        savedOceanFloorWg->tag == cppfm::nbt::LongArray &&
        savedOceanFloorWg->longArray == allHeightmaps.oceanFloorWg &&
        savedOceanFloor->tag == cppfm::nbt::LongArray &&
        savedOceanFloor->longArray == allHeightmaps.oceanFloor &&
        savedMotionNoLeaves->tag == cppfm::nbt::LongArray &&
        savedMotionNoLeaves->longArray == allHeightmaps.motionBlockingNoLeaves &&
        atColumn(allHeightmaps.worldSurface, 4) == 66 &&
        atColumn(allHeightmaps.motionBlocking, 4) == 65 &&
        atColumn(allHeightmaps.oceanFloor, 4) == 65 &&
        atColumn(allHeightmaps.worldSurface, 5) == 66 &&
        atColumn(allHeightmaps.motionBlocking, 5) == 66 &&
        atColumn(allHeightmaps.motionBlockingNoLeaves, 5) == 65 &&
        atColumn(allHeightmaps.worldSurface, 6) == 66 &&
        atColumn(allHeightmaps.motionBlocking, 6) == 66 &&
        atColumn(allHeightmaps.oceanFloor, 6) == 65 &&
        atColumn(allHeightmaps.motionBlocking, 7) == 65 &&
        atColumn(allHeightmaps.oceanFloor, 7) == 65 &&
        atColumn(allHeightmaps.motionBlocking, 8) == 65 &&
        atColumn(allHeightmaps.oceanFloor, 8) == 65;
    check(allTypesMatch,
          "all six 1.21.4 Anvil heightmaps are recomputed with their block/fluid predicates");
}

void testWorldgenHeightmapsStayAtWorldgenStage() {
    cppfm::World world("minecraft:plains", cppfm::LevelType::Normal,
                       0x51A7C0DEULL);
    check(world.generateChunkIfMissing(0, 0),
          "normal terrain chunk is generated for worldgen heightmap test");
    const auto before = world.tryGetCopy(0, 0);
    check(before && before->hasWorldgenHeightmaps(),
          "normal worldgen captures pre-feature surface and ocean-floor maps");
    if (!before) return;

    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    const auto initialMaps = cppfm::packAnvilHeightmaps(*before);
    const auto indexOf = [](const std::vector<std::int64_t>& packed,
                            std::size_t column) {
        const auto word = column / cppfm::kHeightmapValuesPerLong;
        const auto shift = (column % cppfm::kHeightmapValuesPerLong) *
                           cppfm::kHeightmapBitsPerEntry;
        return static_cast<int>(
            (static_cast<std::uint64_t>(packed[word]) >> shift) & 0x1FFU);
    };
    check(world.setBlock(0, cppfm::kMaxY - 1, 0, stone),
          "post-generation edit raises one final surface column");
    const auto after = world.tryGetCopy(0, 0);
    check(after.has_value(), "edited normal chunk remains available");
    if (!after) return;
    const auto finalMaps = cppfm::packAnvilHeightmaps(*after);
    const auto worldgenHeight = indexOf(initialMaps.worldSurfaceWg, 0);
    const auto finalHeight = indexOf(finalMaps.worldSurface, 0);
    check(after->worldSurfaceWg()[0] == worldgenHeight &&
              indexOf(finalMaps.worldSurfaceWg, 0) == worldgenHeight &&
              finalHeight == cppfm::kMaxY - cppfm::kMinY &&
              finalHeight != worldgenHeight,
          "WORLD_SURFACE_WG retains its captured stage while WORLD_SURFACE tracks edits");
}

void testWorldgenHeightmapsAreCapturedForEveryDimension() {
    const auto stone = static_cast<std::uint16_t>(
        cppfm::gen::blockNameToState().at("minecraft:stone"));
    const std::array<std::pair<cppfm::LevelType, const char*>, 3> dimensions{{
        {cppfm::LevelType::Flat, "flat"},
        {cppfm::LevelType::Nether, "nether"},
        {cppfm::LevelType::End, "end"}}};
    for (const auto& [level, name] : dimensions) {
        cppfm::World world("minecraft:plains", level, 0x51A7C0DEULL);
        check(world.generateChunkIfMissing(0, 0),
              "dimension terrain chunk generates for WG map capture");
        const auto before = world.tryGetCopy(0, 0);
        check(before && before->hasWorldgenHeightmaps(),
              name);
        if (!before || !before->hasWorldgenHeightmaps()) continue;

        const auto geometry = world.verticalGeometry();
        const auto initialMaps = cppfm::packAnvilHeightmaps(*before, geometry);
        const auto unpackHeight = [](const std::vector<std::int64_t>& packed,
                                     std::size_t index) {
            constexpr std::size_t valuesPerLong = 64 / 9;
            return static_cast<std::uint16_t>(
                (static_cast<std::uint64_t>(packed[index / valuesPerLong]) >>
                 ((index % valuesPerLong) * 9)) & 0x1FFU);
        };
        int localX = -1;
        int localZ = -1;
        std::uint16_t surface = 0;
        for (int z = 0; z < 16 && localX < 0; ++z)
            for (int x = 0; x < 16; ++x) {
                const auto column = static_cast<std::size_t>(z * 16 + x);
                const auto finalSurface =
                    unpackHeight(initialMaps.worldSurface, column);
                if (before->worldSurfaceWg()[column] < geometry.height &&
                    finalSurface < geometry.height) {
                    localX = x;
                    localZ = z;
                    surface = before->worldSurfaceWg()[column];
                    break;
                }
            }
        check(localX >= 0,
              "dimension has an in-range column above generated terrain");
        if (localX < 0) continue;
        const auto column = static_cast<std::size_t>(localZ * 16 + localX);
        const int y = geometry.minY + unpackHeight(initialMaps.worldSurface,
                                                   column);
        check(world.setBlock(localX, y, localZ, stone),
              "post-generation edit is accepted above dimension terrain");
        const auto after = world.tryGetCopy(0, 0);
        check(after && after->hasWorldgenHeightmaps(),
              "dimension retains its captured WG map after gameplay edit");
        if (!after) continue;
        const auto finalMaps = cppfm::packAnvilHeightmaps(*after, geometry);
        check(after->worldSurfaceWg()[column] == surface &&
                  finalMaps.worldSurfaceWg == initialMaps.worldSurfaceWg &&
                  finalMaps.worldSurface != initialMaps.worldSurface,
              "dimension *_WG stays stable while final surface heightmap updates");
    }
}

void testDimensionGeometryProjection() {
    using namespace cppfm;
    check(dimensionGeometry(0).minY == -64 &&
              dimensionGeometry(0).height == 384 &&
              dimensionGeometry(0).logicalHeight == 384 &&
              dimensionGeometry(0).sectionCount() == 24,
          "Overworld geometry describes its 24 physical sections");
    check(dimensionGeometry(-1).minY == 0 &&
              dimensionGeometry(-1).height == 256 &&
              dimensionGeometry(-1).logicalHeight == 128 &&
              !dimensionGeometry(-1).hasSkylight &&
              dimensionGeometry(-1).sectionCount() == 16,
          "Nether physical height is 256 and stays distinct from logical height 128");
    check(dimensionGeometry(1).minY == 0 &&
              dimensionGeometry(1).height == 256 &&
              dimensionGeometry(1).logicalHeight == 256 &&
              !dimensionGeometry(1).hasSkylight &&
              dimensionGeometry(1).sectionCount() == 16,
          "End geometry describes 16 sections with no skylight");

    World nether("minecraft:nether_wastes", LevelType::Nether, 0x59);
    const auto geometry = nether.verticalGeometry();
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    check(geometry.storageSection(0) == 4 &&
              geometry.storageSection(15) == 19 &&
              geometry.worldSection(4) == 0 &&
              geometry.worldSection(19) == 15,
          "Nether world sections map to the middle 16 slices of fixed backing storage");
    check(!nether.setBlock(0, -1, 0, stone) &&
              !nether.setBlock(0, 256, 0, stone) &&
              nether.loadedChunkCount() == 0,
          "out-of-dimension writes are rejected without generating chunks");
    check(nether.setBlock(0, 0, 0, stone) &&
              nether.setBlock(0, 255, 0, stone) &&
              nether.getBlock(0, 0, 0) == stone &&
              nether.getBlock(0, 255, 0) == stone &&
              nether.getBlock(0, -1, 0) == 0 &&
              nether.getBlock(0, 256, 0) == 0,
          "Nether lower and upper physical block bounds map to storage safely");
    check(nether.surfaceFeetYIfLoaded(0, 0) == 256,
          "Nether surface lookup uses the dimension's physical top");
    check(nether.seaLevel() == 32 &&
              geometry.logicalMaxY() == 128 &&
              geometry.containsY(nether.spawnPoint().y),
          "Nether generator metadata and spawn use the correct dimension height model");
    World end("minecraft:the_end", LevelType::End, 0x60);
    check(end.seaLevel() == 0 && end.spawnPoint().y == 65 &&
              end.verticalGeometry().logicalMaxY() == 256,
          "End metadata and default spawn are dimension-specific");

    const auto chunk = nether.tryGetCopy(0, 0);
    check(chunk.has_value(), "Nether geometry fixture is available");
    if (!chunk) return;

    const auto heights = packAnvilHeightmaps(*chunk, geometry);
    const int topColumnHeight = static_cast<int>(
        static_cast<std::uint64_t>(heights.worldSurface[0]) & 0x1FFU);
    check(topColumnHeight == 256,
          "Nether heightmap values are relative to min_y and include block Y=255");

    Chunk emptyChunk;
    WriteBuffer overworldSections, netherSections;
    serializeSectionData(overworldSections, &emptyChunk, 40,
                         kOverworldGeometry);
    serializeSectionData(netherSections, &emptyChunk, 40, geometry);
    check(overworldSections.data.size() == 24U * 8U &&
              netherSections.data.size() == 16U * 8U,
          "network section projection excludes the eight padded Nether backing sections");

    const auto root = chunkToNBT(0, 0, *chunk, "minecraft:nether_wastes",
                                 nullptr, geometry);
    const auto* yPos = root.get("yPos");
    const auto* sections = root.get("sections");
    check(yPos && yPos->tag == nbt::Int && yPos->i == 0 && sections &&
              sections->tag == nbt::List && sections->list.size() == 16 &&
              sections->list.front().get("Y")->b == 0 &&
              sections->list.back().get("Y")->b == 15,
          "Nether Anvil projection stores only section Ys 0 through 15");

    Chunk restored;
    std::string biome;
    bool decoded = false;
    {
        ChunkBuildWriter writer(restored);
        decoded = chunkFromNBT(root, writer, {}, biome,
                               [](const std::string&) { return 8; }, geometry);
    }
    check(decoded &&
              restored.blockAt(Chunk::index(4, 0, 0, 0)) == stone &&
              restored.blockAt(Chunk::index(19, 15, 0, 0)) == stone,
          "Nether Anvil round-trip restores both physical edge sections to their backing slices");

    // A pre-geometry cppfm chunk contains Overworld-shaped padding sections.
    // They are not active Nether storage, but an upgrade must not silently
    // destroy data from an older build if a user had written there.
    Chunk legacyChunk;
    const auto diamond = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:diamond_block"));
    {
        ChunkBuildWriter writer(legacyChunk);
        writer.blocks.set(Chunk::index(0, 0, 0, 0), diamond);
    }
    const auto legacyRoot = chunkToNBT(
        0, 0, legacyChunk, "minecraft:nether_wastes");
    WriteBuffer legacyBytes;
    nbt::writeFileRoot(legacyBytes, legacyRoot);
    World migratedNether("minecraft:nether_wastes", LevelType::Nether, 0x61);
    const auto migrationDirectory = std::filesystem::temp_directory_path() /
        ("cppfm-dimension-geometry-migration-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    Persistence migration(migratedNether, migrationDirectory.string(),
                          "minecraft:nether_wastes");
    Chunk migrationChunk;
    std::function<void()> publishCallback;
    bool legacyDecoded = false;
    {
        ChunkBuildWriter writer(migrationChunk);
        legacyDecoded = migration.decodeChunkBytes(
            legacyBytes.data, writer, 0, 0);
        publishCallback = writer.takePublishCallback();
    }
    if (legacyDecoded)
        migratedNether.installLoadedChunk(0, 0, std::move(migrationChunk),
                                          std::move(publishCallback));
    const auto migratedChunk = migratedNether.tryGetCopy(0, 0);
    nbt::Value migratedRoot;
    if (migratedChunk)
        migratedRoot = chunkToNBT(0, 0, *migratedChunk,
                                  "minecraft:nether_wastes", nullptr,
                                  migratedNether.verticalGeometry());
    migration.mergeOpaqueChunkRootNbt(0, 0, migratedRoot);
    const auto* migratedSections = migratedRoot.get("sections");
    bool retainedLegacyBlock = false;
    if (migratedSections && migratedSections->tag == nbt::List) {
        for (const auto& section : migratedSections->list) {
            const auto* sectionY = section.get("Y");
            const auto* blockStates = section.get("block_states");
            const auto* palette = blockStates
                ? blockStates->get("palette") : nullptr;
            if (sectionY && sectionY->b == -4 && palette &&
                palette->tag == nbt::List && !palette->list.empty()) {
                const auto* name = palette->list.front().get("Name");
                retainedLegacyBlock = name &&
                    name->str == "minecraft:diamond_block";
            }
        }
    }
    check(legacyDecoded && migratedChunk &&
              migratedNether.getBlock(0, -64, 0) == 0 &&
              retainedLegacyBlock,
          "out-of-dimension legacy blocks stay inactive but survive as opaque Anvil data");
    std::error_code cleanupError;
    std::filesystem::remove_all(migrationDirectory, cleanupError);
    check(!cleanupError,
          "dimension geometry fixture removes its exact unique temporary directory");

    WriteBuffer light;
    serializeLightPayload(light, *chunk, geometry);
    ReadBuffer lightIn(light.data);
    const auto readMask = [&](ReadBuffer& input) {
        const auto words = input.varint();
        std::vector<std::int64_t> mask;
        for (int i = 0; i < words; ++i) mask.push_back(input.i64());
        return mask;
    };
    const auto skyMask = readMask(lightIn);
    const auto blockMask = readMask(lightIn);
    const auto emptySkyMask = readMask(lightIn);
    const auto emptyBlockMask = readMask(lightIn);
    check(skyMask.empty() && blockMask.empty() && emptySkyMask.empty() &&
              emptyBlockMask.size() == 1 &&
              static_cast<std::uint64_t>(emptyBlockMask[0]) == 0x3FFFFU,
          "Nether light data uses unset sky masks and 18 padded block-light sections");
}

void testLightPayloadVerticalMaskProjection() {
    using namespace cppfm;
    const auto readMask = [](ReadBuffer& input) {
        const auto words = input.varint();
        std::vector<std::int64_t> mask;
        for (int i = 0; i < words; ++i) mask.push_back(input.i64());
        return mask;
    };
    const auto readArrays = [](ReadBuffer& input) {
        const auto count = input.varint();
        std::vector<std::vector<std::uint8_t>> arrays;
        for (int i = 0; i < count; ++i) {
            const auto length = input.varint();
            arrays.push_back(input.bytes(static_cast<std::size_t>(length)));
        }
        return arrays;
    };

    Chunk darkNether;
    const auto nether = kNetherGeometry;
    const auto setSectionLight = [&](int worldSection, std::size_t cell,
                                     std::uint8_t value) {
        const auto storage = nether.storageSection(worldSection);
        Chunk::setNibble(darkNether.blockLightNib,
                         Chunk::index(storage, 0, 0, 0) + cell, value);
    };
    setSectionLight(-1, 0, 1);  // lower light-padding section -> bit 0
    setSectionLight(0, 1, 2);   // first physical section -> bit 1
    setSectionLight(15, 2, 3); // last physical section -> bit 16
    setSectionLight(16, 3, 4); // upper light-padding section -> bit 17
    WriteBuffer netherPayload;
    serializeLightPayload(netherPayload, darkNether, nether);
    ReadBuffer netherInput(netherPayload.data);
    const auto netherSkyMask = readMask(netherInput);
    const auto netherBlockMask = readMask(netherInput);
    const auto netherEmptySkyMask = readMask(netherInput);
    const auto netherEmptyBlockMask = readMask(netherInput);
    const auto netherSkyArrays = readArrays(netherInput);
    const auto netherBlockArrays = readArrays(netherInput);
    const std::uint64_t litNetherBits = (1ULL << 0) | (1ULL << 1) |
                                        (1ULL << 16) | (1ULL << 17);
    const std::uint64_t allNetherBits = (1ULL << 18) - 1;
    const bool netherArraysMatchBits = netherBlockArrays.size() == 4 &&
        netherBlockArrays[0][0] == 0x01 &&
        netherBlockArrays[1][0] == 0x20 &&
        netherBlockArrays[2][1] == 0x03 &&
        netherBlockArrays[3][1] == 0x40;
    const bool netherProjectionMatches = netherSkyMask.empty() &&
              netherEmptySkyMask.empty() &&
              netherBlockMask.size() == 1 &&
              static_cast<std::uint64_t>(netherBlockMask[0]) == litNetherBits &&
              netherEmptyBlockMask.size() == 1 &&
              static_cast<std::uint64_t>(netherEmptyBlockMask[0]) ==
                  (allNetherBits & ~litNetherBits) &&
              netherSkyArrays.empty() && netherArraysMatchBits &&
              netherInput.remaining() == 0;
    if (!netherProjectionMatches) {
        std::fprintf(stderr,
                     "[light mask] sky=%zu emptySky=%zu block=%zu/%llx "
                     "emptyBlock=%zu/%llx arrays=%zu skyArrays=%zu remain=%zu\n",
                     netherSkyMask.size(), netherEmptySkyMask.size(),
                     netherBlockMask.size(),
                     static_cast<unsigned long long>(netherBlockMask.empty()
                         ? 0 : static_cast<std::uint64_t>(netherBlockMask[0])),
                     netherEmptyBlockMask.size(),
                     static_cast<unsigned long long>(netherEmptyBlockMask.empty()
                         ? 0 : static_cast<std::uint64_t>(netherEmptyBlockMask[0])),
                     netherBlockArrays.size(), netherSkyArrays.size(),
                     netherInput.remaining());
        for (const auto& array : netherBlockArrays)
            std::fprintf(stderr, "[light mask] first array byte=%02x\n",
                         array.empty() ? 0 : array[0]);
    }
    check(netherProjectionMatches,
          "Nether light masks align arrays to padded and physical section Y positions");

    Chunk emptyOverworld;
    WriteBuffer overworldPayload;
    serializeLightPayload(overworldPayload, emptyOverworld,
                          kOverworldGeometry);
    ReadBuffer overworldInput(overworldPayload.data);
    const auto overworldSkyMask = readMask(overworldInput);
    const auto overworldBlockMask = readMask(overworldInput);
    const auto overworldEmptySkyMask = readMask(overworldInput);
    const auto overworldEmptyBlockMask = readMask(overworldInput);
    const auto overworldSkyArrays = readArrays(overworldInput);
    const auto overworldBlockArrays = readArrays(overworldInput);
    const auto allOverworldBits = (1ULL << 26) - 1;
    const auto fullSkyBits = allOverworldBits & ~(1ULL << 0);
    const bool fullSkyArrays = overworldSkyArrays.size() == 25 &&
        std::all_of(overworldSkyArrays.begin(), overworldSkyArrays.end(),
                    [](const auto& array) {
                        return array.size() == 2048 &&
                            std::all_of(array.begin(), array.end(),
                                        [](std::uint8_t byte) {
                                            return byte == 0xFF;
                                        });
                    });
    check(overworldSkyMask.size() == 1 &&
              static_cast<std::uint64_t>(overworldSkyMask[0]) == fullSkyBits &&
              overworldBlockMask.empty() &&
              overworldEmptySkyMask.size() == 1 &&
              overworldEmptySkyMask[0] == 1 &&
              overworldEmptyBlockMask.size() == 1 &&
              static_cast<std::uint64_t>(overworldEmptyBlockMask[0]) ==
                  allOverworldBits &&
              fullSkyArrays && overworldBlockArrays.empty() &&
              overworldInput.remaining() == 0,
          "Overworld light uses full-sky data above terrain and the two boundary sections");
}

void testPortalSpawnUsesTargetDimensionGeometry() {
    using namespace cppfm;
    World unprepared("minecraft:nether_wastes", LevelType::Nether, 0x60);
    int unpreparedX = -1, unpreparedY = -1, unpreparedZ = -1;
    std::vector<World::BlockUpdate> unpreparedPlatform;
    const auto initiallyLoadedChunks = unprepared.loadedChunkCount();
    const bool searchedUnprepared = PortalHandler::findSafeSpawn(
        unprepared, unpreparedX, unpreparedY, unpreparedZ, 8, 200, 8);
    const bool stagedUnprepared = PortalHandler::prepareSafeFallback(
        unprepared, unpreparedX, unpreparedY, unpreparedZ,
        unpreparedPlatform, 8, 200, 8);
    check(!searchedUnprepared && !stagedUnprepared &&
              initiallyLoadedChunks == 0 &&
              unprepared.loadedChunkCount() == initiallyLoadedChunks &&
              !unprepared.hasChunk(0, 0),
          "portal destination searches decline unloaded chunks without synchronous generation");

    BlockPos unchangedTarget{1, 2, 3};
    const bool rejectedOverflow = !PortalHandler::tryTransformCoordinates(
        BlockPos{std::numeric_limits<std::int32_t>::max(), 64, 0}, -1, 0,
        unchangedTarget);
    check(rejectedOverflow && unchangedTarget.x == 1 &&
              unchangedTarget.y == 2 && unchangedTarget.z == 3,
          "Nether-to-Overworld portal scaling rejects coordinate overflow without changing output");
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    World nether("minecraft:nether_wastes", LevelType::Nether, 0x61);
    check(nether.setBlock(8, 199, 8, stone) &&
              nether.setBlock(8, 253, 8, stone) &&
              nether.setBlock(8, 0, 8, stone) &&
              nether.setBlock(8, 1, 8, 0) &&
              nether.setBlock(8, 2, 8, 0),
          "portal fixture places safe floors at ordinary, high, and low Nether Y");

    int x = 0, y = 0, z = 0;
    const bool highTarget = PortalHandler::findSafeSpawn(
        nether, x, y, z, 8, 200, 8);
    check(highTarget && x == 8 && y == 200 && z == 8,
          "portal search honors transformed Y above Nether logical height");

    const bool clampedUpperTarget = PortalHandler::findSafeSpawn(
        nether, x, y, z, 8, 300, 8);
    check(clampedUpperTarget && x == 8 && y == 254 && z == 8,
          "portal search clamps feet and head inside the physical dimension bounds");

    const bool clampedLowerTarget = PortalHandler::findSafeSpawn(
        nether, x, y, z, 8, -100, 8);
    check(clampedLowerTarget && x == 8 && y == 1 && z == 8,
          "portal search clamps low transformed positions to a safe in-range Y");

    World fallback("minecraft:nether_wastes", LevelType::Nether, 0x62);
    int fallbackX = -1, fallbackY = -1, fallbackZ = -1;
    std::vector<World::BlockUpdate> platformEvents;
    const PortalHandler::PlatformBlockObserver observePlatform =
        [&platformEvents](std::int8_t dimension,
                          const World::BlockUpdate& block) {
            if (dimension == -1) platformEvents.push_back(block);
        };
    World stagedFallback("minecraft:nether_wastes", LevelType::Nether,
                         0x62);
    std::int32_t stagedX = -1, stagedY = -1, stagedZ = -1;
    std::vector<World::BlockUpdate> stagedPlatform;
    const bool stagedChunksReady = PortalHandler::prepareDestinationChunks(
        stagedFallback, 8, 8);
    const bool staged = stagedChunksReady &&
        PortalHandler::prepareSafeFallback(
            stagedFallback, stagedX, stagedY, stagedZ, stagedPlatform,
            8, 200, 8);
    check(stagedChunksReady && staged && stagedX == 8 && stagedY == 200 && stagedZ == 8 &&
              stagedPlatform.size() == 9 &&
              stagedFallback.getBlock(8, 199, 8) == 0 &&
              stagedFallback.getBlock(8, 200, 8) == 0 &&
              stagedFallback.getBlock(8, 201, 8) == 0,
          "portal fallback preparation stages platform edits without mutating the destination");

    const bool fallbackChunksReady = PortalHandler::prepareDestinationChunks(
        fallback, 8, 8);
    const bool fallbackCreated = fallbackChunksReady &&
        PortalHandler::findOrCreateSafeFallback(
              observePlatform, fallback, -1, fallbackX, fallbackY, fallbackZ,
              8, 200, 8);
    check(fallbackChunksReady && fallbackCreated &&
              fallbackX == 8 && fallbackY == 200 && fallbackZ == 8 &&
              fallback.getBlock(8, 199, 8) == static_cast<std::uint16_t>(
                  gen::blockNameToState().at("minecraft:obsidian")) &&
              fallback.getBlock(8, 200, 8) == 0 &&
              fallback.getBlock(8, 201, 8) == 0 &&
              platformEvents.size() == 9,
          "portal fallback creates and reports all platform blocks below clear headroom");

    constexpr std::uint64_t edgeSeed = 0x64;
    World edgeReference("minecraft:nether_wastes", LevelType::Nether,
                        edgeSeed);
    World edgeFallback("minecraft:nether_wastes", LevelType::Nether,
                       edgeSeed);
    check(edgeReference.generateChunkIfMissing(0, 0) &&
              edgeReference.generateChunkIfMissing(1, 0) &&
              edgeFallback.generateChunkIfMissing(0, 0),
          "portal chunk-edge fixture loads only the reference neighbor chunk");
    std::int32_t edgeX = 15;
    std::int32_t edgeY = -1;
    std::int32_t edgeZ = -1;
    std::uint16_t neighborTerrain = 0;
    for (std::int32_t z = 0; z < 16 && edgeY < 0; ++z) {
        for (std::int32_t y = kNetherGeometry.minY + 1;
             y <= kNetherGeometry.maxY() - 2; ++y) {
            if (edgeFallback.getBlock(edgeX, y - 1, z) != 0 ||
                edgeFallback.getBlock(edgeX, y, z) != 0 ||
                edgeFallback.getBlock(edgeX, y + 1, z) != 0)
                continue;
            const auto neighbor = edgeReference.getBlock(edgeX + 1,
                                                         y - 1, z);
            if (neighbor == 0) continue;
            edgeY = y;
            edgeZ = z;
            neighborTerrain = neighbor;
            break;
        }
    }
    const auto edgeLoadedBeforeFallback = edgeFallback.loadedChunkCount();
    std::vector<World::BlockUpdate> edgeUnpreparedPlatform;
    std::int32_t edgeUnpreparedX = -1, edgeUnpreparedY = -1,
                 edgeUnpreparedZ = -1;
    const bool edgeRejectedUnloadedNeighbor = edgeY >= 0 &&
        !PortalHandler::prepareSafeFallback(
            edgeFallback, edgeUnpreparedX, edgeUnpreparedY,
            edgeUnpreparedZ, edgeUnpreparedPlatform,
            edgeX, edgeY, edgeZ);
    check(edgeRejectedUnloadedNeighbor &&
              edgeFallback.loadedChunkCount() == edgeLoadedBeforeFallback,
          "portal fallback declines an unprepared edge neighbor without generating or editing it");
    const bool edgeChunksPrepared = PortalHandler::prepareDestinationChunks(
        edgeFallback, edgeX, edgeZ);
    std::int32_t edgeOutX = -1;
    std::int32_t edgeOutY = -1;
    std::int32_t edgeOutZ = -1;
    const bool edgeFallbackPlaced = edgeChunksPrepared && edgeY >= 0 &&
        PortalHandler::findOrCreateSafeFallback(
            {}, edgeFallback, -1, edgeOutX, edgeOutY, edgeOutZ,
            edgeX, edgeY, edgeZ);
    check(edgeChunksPrepared && edgeFallbackPlaced && edgeOutX == edgeX && edgeOutY == edgeY &&
              edgeOutZ == edgeZ &&
              edgeFallback.getBlock(edgeX + 1, edgeY - 1, edgeZ) ==
                  neighborTerrain,
          "portal fallback succeeds after off-guard chunk preparation and preserves edge terrain");

    const auto caveAir = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:cave_air"));
    const auto voidAir = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:void_air"));
    const auto obsidian = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:obsidian"));
    World nonzeroAirFallback("minecraft:nether_wastes", LevelType::Nether,
                             0x66);
    const bool nonzeroAirFixture =
        nonzeroAirFallback.setBlock(8, 199, 8, caveAir) &&
        nonzeroAirFallback.setBlock(8, 200, 8, voidAir) &&
        nonzeroAirFallback.setBlock(8, 201, 8, caveAir);
    std::vector<World::BlockUpdate> nonzeroAirPlatform;
    std::int32_t nonzeroAirX = -1, nonzeroAirY = -1, nonzeroAirZ = -1;
    const bool nonzeroAirStaged = nonzeroAirFixture &&
        PortalHandler::prepareSafeFallback(
            nonzeroAirFallback, nonzeroAirX, nonzeroAirY, nonzeroAirZ,
            nonzeroAirPlatform, 8, 200, 8);
    const bool caveAirSupportReplaced = std::any_of(
        nonzeroAirPlatform.begin(), nonzeroAirPlatform.end(),
        [obsidian](const World::BlockUpdate& block) {
            return block.x == 8 && block.y == 199 && block.z == 8 &&
                   block.state == obsidian;
        });
    check(nonzeroAirStaged && nonzeroAirX == 8 && nonzeroAirY == 200 &&
              nonzeroAirZ == 8 && caveAirSupportReplaced &&
              nonzeroAirFallback.getBlock(8, 199, 8) == caveAir &&
              nonzeroAirFallback.getBlock(8, 200, 8) == voidAir &&
              nonzeroAirFallback.getBlock(8, 201, 8) == caveAir,
          "portal fallback treats cave_air and void_air as clear space and replaceable platform air");

    World blocked("minecraft:nether_wastes", LevelType::Nether, 0x63);
    const auto blockedStone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    std::array<World::BlockUpdate, 256> blockedColumn{};
    for (std::size_t i = 0; i < blockedColumn.size(); ++i)
        blockedColumn[i] = {8, static_cast<std::int32_t>(i), 8, blockedStone};
    check(blocked.setBlockBatch(blockedColumn),
          "portal blocked-column fixture fills the physical Nether height");
    fallbackX = -1; fallbackY = -1; fallbackZ = -1;
    check(!PortalHandler::findOrCreateSafeFallback(
              {}, blocked, -1, fallbackX, fallbackY, fallbackZ,
              8, 200, 8) &&
              fallbackX == -1 && fallbackY == -1 && fallbackZ == -1,
          "portal fallback declines a fully obstructed destination without unsafe teleport");

    for (const auto* fluidName : {"minecraft:water", "minecraft:lava"}) {
        World fluidFloor("minecraft:nether_wastes", LevelType::Nether,
                         fluidName[10] == 'w' ? 0x64 : 0x65);
        std::array<World::BlockUpdate, 256> column{};
        for (std::size_t i = 0; i < column.size(); ++i)
            column[i] = {8, static_cast<std::int32_t>(i), 8, stone};
        column[199].state = static_cast<std::uint16_t>(
            gen::blockNameToState().at(fluidName));
        column[200].state = 0;
        column[201].state = 0;
        check(fluidFloor.setBlockBatch(column),
              "portal fluid-floor fixture is initialized");
        int fluidX = -1, fluidY = -1, fluidZ = -1;
        const bool safeSearch = PortalHandler::findSafeSpawn(
            fluidFloor, fluidX, fluidY, fluidZ, 8, 200, 8);
        const auto fluidState = static_cast<std::uint16_t>(
            gen::blockNameToState().at(fluidName));
        const bool searchAvoidsFluid = !safeSearch ||
            (fluidFloor.getBlock(fluidX, fluidY - 1, fluidZ) != fluidState &&
             PortalHandler::hasSafeSupport(
                 fluidFloor.getBlock(fluidX, fluidY - 1, fluidZ)));
        int fallbackFluidX = -1, fallbackFluidY = -1, fallbackFluidZ = -1;
        const bool safeFallback = PortalHandler::findOrCreateSafeFallback(
            {}, fluidFloor, -1, fallbackFluidX, fallbackFluidY,
            fallbackFluidZ, 8, 200, 8);
        check(searchAvoidsFluid && !safeFallback && fallbackFluidX == -1 &&
                  fallbackFluidY == -1 && fallbackFluidZ == -1,
              "portal search avoids fluid support and fallback declines unsafe landing");
    }
}

void testDoorSupportClassification() {
    using namespace cppfm;
    const auto state = [](const char* name) {
        return static_cast<std::uint16_t>(gen::blockNameToState().at(name));
    };
    const auto* slab = gen::blockByName("minecraft:stone_slab");
    const auto topSlab = slab ? static_cast<std::uint16_t>(gen::stateWithProps(
        *slab, {{"type", "top"}})) : std::uint16_t{0};
    const auto bottomSlab = slab ? static_cast<std::uint16_t>(gen::stateWithProps(
        *slab, {{"type", "bottom"}})) : std::uint16_t{0};
    check(hasSturdyTopFace(state("minecraft:stone")) &&
              hasSturdyTopFace(state("minecraft:glass")) &&
              hasSturdyTopFace(state("minecraft:slime_block")) &&
              hasSturdyTopFace(state("minecraft:honey_block")) &&
              hasSturdyTopFace(state("minecraft:beacon")) &&
              hasSturdyTopFace(topSlab) &&
              !hasSturdyTopFace(state("minecraft:chest")) &&
              !hasSturdyTopFace(0) &&
              !hasSturdyTopFace(state("minecraft:cave_air")) &&
              !hasSturdyTopFace(state("minecraft:void_air")) &&
              !hasSturdyTopFace(state("minecraft:oak_fence")) &&
              !hasSturdyTopFace(state("minecraft:cobblestone_wall")) &&
              !hasSturdyTopFace(state("minecraft:fire")) &&
              !hasSturdyTopFace(state("minecraft:water")) &&
              !hasSturdyTopFace(state("minecraft:lava")) &&
              !hasSturdyTopFace(state("minecraft:dandelion")) &&
              !hasSturdyTopFace(bottomSlab),
          "door support distinguishes sturdy blocks from fluids, plants, and non-supporting slabs");
    check(hasSturdyTopFace(state("minecraft:magma_block")) &&
              !PortalHandler::hasSafeSupport(state("minecraft:magma_block")),
          "portal landing rejects hazardous magma support while retaining its sturdy-face classification");
}

void testChunkArrayWriterBounds() {
    using namespace cppfm;
    std::array<std::uint16_t, 2> storage{};
    ChunkArrayWriter<std::uint16_t, 2> writer(storage);
    writer.set(1, 7);
    check(writer.get(0) == 0 && writer.get(1) == 7,
          "chunk construction adapter reads and writes valid array indexes");

    bool getRejected = false;
    try {
        (void)writer.get(storage.size());
    } catch (const std::out_of_range&) {
        getRejected = true;
    }
    bool setRejected = false;
    try {
        writer.set(storage.size(), 9);
    } catch (const std::out_of_range&) {
        setRejected = true;
    }
    check(getRejected && setRejected && storage[1] == 7,
          "chunk construction adapter rejects out-of-range access without mutation");
}

void testChunkSaveAdmissionUsesAnvilSectorLimit() {
    using namespace cppfm;
    World world("minecraft:plains", LevelType::Flat, 0xA71AULL);
    check(world.generateChunkIfMissing(0, 0),
          "save-admission fixture creates its chunk");
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-chunk-save-admission-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    bool ordinaryChunkFits = false;
    bool highEntropyBlockEntityRejected = false;
    bool highEntropyItemCandidateRejected = false;
    {
        Persistence persistence(world, directory.string(), "minecraft:plains");
        ordinaryChunkFits = persistence.chunkFitsAnvilEntry(0, 0);
        persistence.setChunkExtras(
            [](std::int32_t cx, std::int32_t cz, nbt::Value& root) {
                if (cx != 0 || cz != 0) return;
                nbt::Value entities = nbt::Value::makeList(nbt::Compound);
                nbt::Value entity = nbt::Value::makeCompound();
                entity.set("id", nbt::Value::makeString("minecraft:chest"));
                entity.set("x", nbt::Value::makeInt(1));
                entity.set("y", nbt::Value::makeInt(64));
                entity.set("z", nbt::Value::makeInt(1));
                nbt::Value payload;
                payload.tag = nbt::ByteArray;
                payload.byteArray.resize(1'100'000);
                std::uint64_t state = 0x9E3779B97F4A7C15ULL;
                for (auto& byte : payload.byteArray) {
                    state ^= state >> 12;
                    state ^= state << 25;
                    state ^= state >> 27;
                    byte = static_cast<std::uint8_t>(
                        (state * 0x2545F4914F6CDD1DULL) >> 56);
                }
                entity.set("payload", std::move(payload));
                entities.list.push_back(std::move(entity));
                root.set("block_entities", std::move(entities));
            }, {});
        highEntropyBlockEntityRejected =
            !persistence.chunkFitsAnvilEntry(0, 0);

        BlockEntityStore blockEntities;
        const auto hopperKey = posKey(1, 64, 1);
        const auto hopper = blockEntities.createShared(
            hopperKey, BlockEntity::Kind::Hopper);
        persistence.setChunkExtras(
            [&blockEntities](std::int32_t cx, std::int32_t cz,
                             nbt::Value& root) {
                nbt::Value entities = nbt::Value::makeList(nbt::Compound);
                blockEntities.writeChunkNbt(cx, cz, entities);
                if (!entities.list.empty())
                    root.set("block_entities", std::move(entities));
            }, {});
        BlockEntity candidate = *hopper;
        auto stack = ItemStack::ofName("minecraft:stone");
        nbt::Value customData = nbt::Value::makeCompound();
        nbt::Value blob;
        blob.tag = nbt::ByteArray;
        blob.byteArray.resize(ItemStack::kMaxComponentPayloadBytes - 64);
        std::uint64_t randomState = 0x9E3779B97F4A7C15ULL;
        for (auto& byte : blob.byteArray) {
            randomState ^= randomState >> 12;
            randomState ^= randomState << 25;
            randomState ^= randomState >> 27;
            byte = static_cast<std::uint8_t>(
                (randomState * 0x2545F4914F6CDD1DULL) >> 56);
        }
        customData.set("blob", std::move(blob));
        WriteBuffer componentPayload;
        nbt::writeFileRoot(componentPayload, customData);
        stack.components.emplace_back(0, std::move(componentPayload.data));
        candidate.generic.slots[0] = std::move(stack);
        std::vector<BlockEntityStore::SnapshotOverride> overrides;
        overrides.push_back({hopperKey, std::move(candidate)});
        const auto writeProjectedBlockEntities =
            [&blockEntities, &overrides](std::int32_t cx, std::int32_t cz,
                                         nbt::Value& root) {
                nbt::Value entities = nbt::Value::makeList(nbt::Compound);
                blockEntities.writeChunkNbt(cx, cz, entities, overrides);
                if (!entities.list.empty())
                    root.set("block_entities", std::move(entities));
            };
        highEntropyItemCandidateRejected =
            !persistence.chunkFitsAnvilEntry(
                0, 0, writeProjectedBlockEntities);
    }
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(ordinaryChunkFits,
          "a normal chunk passes the exact Anvil compression admission check");
    check(highEntropyBlockEntityRejected,
          "high-entropy block-entity NBT beyond 255 sectors is rejected before commit");
    check(highEntropyItemCandidateRejected,
          "projected hopper item transfer rejects high-entropy item components before publishing them");
}

void testChunkPublicationRollsBackAllPersistenceSideData() {
    using namespace cppfm;
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-chunk-publication-rollback-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    World world("minecraft:plains", LevelType::Flat, 0xC0FFEEULL);
    Persistence persistence(world, directory.string(), "minecraft:plains");
    persistence.setBiomeCodec({{0, "minecraft:desert"}}, 0);
    BlockEntityStore blockEntities;
    persistence.setChunkExtras(
        {}, {},
        [](const nbt::Value& root) {
            BlockEntityStore::validateChunkNbt(root);
        },
        [&blockEntities](const nbt::Value& root) {
            return blockEntities.prepareChunkNbt(root);
        });

    Chunk saved;
    auto root = chunkToNBT(0, 0, saved, "minecraft:desert");
    root.set("example:opaque_root_marker", nbt::Value::makeString("kept"));
    auto entities = nbt::Value::makeList(nbt::Compound);
    auto chest = nbt::Value::makeCompound();
    chest.set("id", nbt::Value::makeString("minecraft:chest"));
    chest.set("x", nbt::Value::makeInt(1));
    chest.set("y", nbt::Value::makeInt(64));
    chest.set("z", nbt::Value::makeInt(1));
    entities.list.push_back(std::move(chest));
    root.set("block_entities", std::move(entities));
    WriteBuffer encoded;
    nbt::writeFileRoot(encoded, root);

    const auto decodeCandidate = [&](Chunk& chunk) {
        ChunkBuildWriter writer(chunk);
        const bool decoded = persistence.decodeChunkBytes(
            encoded.data, writer, 0, 0);
        return std::pair{decoded, writer.takePublishCallback()};
    };

    Chunk failedCandidate;
    auto [decoded, failedPublish] = decodeCandidate(failedCandidate);
    Persistence::setPublicationPreparedHookForTesting([] {
        throw std::runtime_error("injected failure after side-state staging");
    });
    bool publishFailed = false;
    try {
        world.installLoadedChunk(0, 0, std::move(failedCandidate),
                                 std::move(failedPublish));
    } catch (const ChunkPublishError&) {
        publishFailed = true;
    }
    Persistence::setPublicationPreparedHookForTesting({});

    Chunk projected;
    auto projectedRoot = chunkToNBT(0, 0, projected, "minecraft:plains");
    persistence.mergeOpaqueChunkRootNbt(0, 0, projectedRoot);
    check(decoded && publishFailed && !world.hasChunk(0, 0) &&
              blockEntities.empty() &&
              persistence.biomeKeyForTesting() == "minecraft:plains" &&
              !projectedRoot.get("example:opaque_root_marker"),
          "injected failure rolls back block entities, opaque NBT, and biome override with the discarded chunk");

    Chunk acceptedCandidate;
    auto [retryDecoded, acceptedPublish] = decodeCandidate(acceptedCandidate);
    world.installLoadedChunk(0, 0, std::move(acceptedCandidate),
                             std::move(acceptedPublish));
    const auto acceptedChunk = world.tryGetCopy(0, 0);
    bool opaqueMarkerKept = false;
    if (acceptedChunk) {
        auto acceptedRoot = chunkToNBT(0, 0, *acceptedChunk,
                                       "minecraft:plains");
        persistence.mergeOpaqueChunkRootNbt(0, 0, acceptedRoot);
        const auto* marker =
            acceptedRoot.get("example:opaque_root_marker");
        opaqueMarkerKept = marker && marker->tag == nbt::String &&
                           marker->str == "kept";
    }
    check(retryDecoded && acceptedChunk &&
              blockEntities.getAt(1, 64, 1) &&
              persistence.biomeKeyForTesting() == "minecraft:desert" &&
              opaqueMarkerKept,
          "a retry publishes the chunk and all staged side data together");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "chunk publication rollback fixture removes its exact unique temporary directory");
}

} // namespace

int main() {
    testGeneratedChunkRegressionBaselines();
    testAnvilHeightmapTypes();
    testWorldgenHeightmapsStayAtWorldgenStage();
    testWorldgenHeightmapsAreCapturedForEveryDimension();
    testDimensionGeometryProjection();
    testLightPayloadVerticalMaskProjection();
    testPortalSpawnUsesTargetDimensionGeometry();
    testDoorSupportClassification();
    testChunkArrayWriterBounds();
    testChunkSaveAdmissionUsesAnvilSectorLimit();
    testChunkPublicationRollsBackAllPersistenceSideData();
    std::printf("world_data_contract: %d assertions, %d failures\n",
                assertions, failures);
    return failures == 0 ? 0 : 1;
}
