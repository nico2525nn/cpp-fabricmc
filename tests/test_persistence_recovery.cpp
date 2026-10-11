#include "game/World.hpp"
#include "game/ChunkCodec.hpp"
#include "game/Anvil.hpp"
#include "game/BlockEntities.hpp"
#include "game/PendingStructureLoot.hpp"
#include "game/Persistence.hpp"
#include "game/PlayerDataRecovery.hpp"
#include "core/NBT.hpp"
#include "worldgen/ChunkGenerator.hpp"
#include "worldgen/StructureManager.hpp"

#include <chrono>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace cppfm {
struct WorldMutationContractTestAccess {
    static std::uint32_t mutationOrderWaiters(const World& world) {
        return world.mutationOrderWaiters_.load(std::memory_order_acquire);
    }
    static std::uint32_t dirtyAdmissionWaiters(
        const Persistence& persistence) {
        return persistence.dirtyAdmissionWaitersForTest_.load(
            std::memory_order_acquire);
    }
    static void setBeforeDirtyCommit(
        Persistence& persistence, std::function<void()> callback) {
        persistence.beforeDirtyCommitForTest_ = std::move(callback);
    }
    static void setBeforeFlushCallLeave(
        Persistence& persistence, std::function<void()> callback) {
        persistence.beforeFlushCallLeaveForTest_ = std::move(callback);
    }
    static void setBeforeStopAdmissionCheck(
        Persistence& persistence, std::function<void()> callback) {
        persistence.beforeStopAdmissionCheckForTest_ = std::move(callback);
    }
    static bool stopInProgress(const Persistence& persistence) {
        return persistence.stopInProgress_.load(std::memory_order_acquire);
    }
};
} // namespace cppfm

namespace {

int failures = 0;
int assertions = 0;

class SideEffectPublishingGenerator final
    : public cppfm::worldgen::ChunkGenerator {
public:
    explicit SideEffectPublishingGenerator(std::atomic<bool>& published)
        : published_(published) {}

    void fillChunk(cppfm::ChunkBuildWriter& writer, int, int) const override {
        writer.addPublishCallback([this] {
            published_.store(true, std::memory_order_release);
        });
    }

private:
    std::atomic<bool>& published_;
};

void check(bool condition, const char* description) {
    ++assertions;
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", description);
}

void writeFixtureBytes(const std::filesystem::path& path,
                       const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!bytes.empty())
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("cannot write recovery fixture bytes");
}

std::vector<std::uint8_t> readFixtureBytes(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read recovery fixture bytes");
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void installExternalRegionStub(const std::filesystem::path& path,
                               std::int32_t localX,
                               std::int32_t localZ) {
    if (localX < 0 || localX >= 32 || localZ < 0 || localZ >= 32)
        throw std::out_of_range("external stub fixture slot is out of range");
    const auto size = std::filesystem::file_size(path);
    if (size < 8192 || size % 4096 != 0)
        throw std::runtime_error("external stub fixture region is not sector aligned");
    const auto sector = size / 4096;
    if (sector >= 0x1000000ULL)
        throw std::runtime_error("external stub fixture exceeds Anvil address space");

    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) throw std::runtime_error("cannot open external stub fixture region");
    std::array<char, 4096> record{};
    record[3] = 1; // one-byte external record length
    record[4] = static_cast<char>(0x80U | 3U); // external, raw codec
    file.seekp(static_cast<std::streamoff>(sector * 4096));
    file.write(record.data(), static_cast<std::streamsize>(record.size()));

    const auto slot = static_cast<std::size_t>(localX) +
                      static_cast<std::size_t>(localZ) * 32;
    const std::array<std::uint8_t, 4> location{
        static_cast<std::uint8_t>(sector >> 16),
        static_cast<std::uint8_t>(sector >> 8),
        static_cast<std::uint8_t>(sector), 1};
    file.seekp(static_cast<std::streamoff>(slot * 4));
    file.write(reinterpret_cast<const char*>(location.data()),
               static_cast<std::streamsize>(location.size()));
    file.flush();
    if (!file) throw std::runtime_error("cannot install external region stub");
}

bool stopPersistenceAndWait(cppfm::Persistence& persistence) {
    for (;;) {
        const auto result = persistence.stopWithStatus();
        if (result == cppfm::Persistence::StopStatus::StoppedCleanly)
            return true;
        if (result == cppfm::Persistence::StopStatus::UnsavedData)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void testAsyncChunkReadUsesStagedRecovery() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-async-chunk-recovery-" + std::to_string(unique));
    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");

    Chunk source;
    ChunkBuildWriter sourceWriter(source);
    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    sourceWriter.blocks.set(Chunk::index(10, 4, 4, 4), stone);
    const auto root = chunkToNBT(0, 0, source, "minecraft:plains");
    WriteBuffer encoded;
    nbt::writeFileRoot(encoded, root);
    Chunk newerSource;
    ChunkBuildWriter newerWriter(newerSource);
    const auto dirt = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:dirt"));
    newerWriter.blocks.set(Chunk::index(10, 4, 4, 4), dirt);
    const auto newerRoot = chunkToNBT(0, 0, newerSource,
                                      "minecraft:plains");
    WriteBuffer newerEncoded;
    nbt::writeFileRoot(newerEncoded, newerRoot);
    const auto siblingRoot = chunkToNBT(1, 0, source,
                                        "minecraft:plains");
    WriteBuffer siblingEncoded;
    nbt::writeFileRoot(siblingEncoded, siblingRoot);
    const auto stagedSiblingRoot = chunkToNBT(2, 0, source,
                                              "minecraft:plains");
    WriteBuffer stagedSiblingEncoded;
    nbt::writeFileRoot(stagedSiblingEncoded, stagedSiblingRoot);
    const auto primaryPath = directory / "region" / "r.0.0.mca";
    std::error_code setupError;
    std::filesystem::create_directories(primaryPath.parent_path(), setupError);
    check(!setupError, "async recovery fixture creates its scoped region directory");
    if (setupError) return;

    RegionFile primary(primaryPath.string());
    primary.store(0, 0, {0xFF}); // invalid NBT, but a structurally valid region entry
    RegionFile staged(primaryPath.string() + ".new");
    staged.store(0, 0, encoded.data);
    const auto recovered = persistence.loadChunkBytesRecovering(0, 0);
    check(recovered.status == Persistence::ChunkBytesRead::Status::Loaded &&
              recovered.bytes == encoded.data &&
              !std::filesystem::exists(primaryPath.string() + ".new") &&
              std::filesystem::exists(primaryPath.string() + ".corrupt"),
          "async chunk reader restores a valid staged region instead of treating primary corruption as missing");

    const auto conflictDir = directory / "valid-conflict-async";
    const auto conflictPath = conflictDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(conflictPath.parent_path(), setupError);
    check(!setupError,
          "valid-conflict async fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile conflictPrimary(conflictPath.string());
        conflictPrimary.store(0, 0, encoded.data);
        conflictPrimary.store(1, 0, siblingEncoded.data);
        RegionFile conflictStage(conflictPath.string() + ".new");
        conflictStage.store(0, 0, newerEncoded.data);
        Persistence conflictPersistence(world, conflictDir.string(),
                                        "minecraft:plains");
        const auto conflictRead =
            conflictPersistence.loadChunkBytesRecovering(0, 0);
        check(conflictRead.status == Persistence::ChunkBytesRead::Status::Loaded &&
                  conflictRead.bytes == newerEncoded.data &&
                  !std::filesystem::exists(conflictPath.string() + ".new"),
              "async recovery prefers a fully written staged snapshot over a readable stale primary");
        conflictPersistence.storeChunkBytes(8, 8, encoded.data);
        RegionFile conflictSaved(conflictPath.string());
        check(conflictSaved.load(0, 0) == newerEncoded.data &&
                  conflictSaved.load(1, 0) == siblingEncoded.data &&
                  conflictSaved.load(8, 8) == encoded.data,
              "a save after staged recovery retains both the newer chunk and primary-only siblings");
    }

    const auto missing = persistence.loadChunkBytesRecovering(8, 8);
    check(missing.status == Persistence::ChunkBytesRead::Status::Missing,
          "async chunk reader distinguishes a genuinely absent chunk from read failure");

    const auto damagedAsyncDir = directory / "corrupt-target-valid-sibling-async";
    const auto damagedAsyncPath =
        damagedAsyncDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(damagedAsyncPath.parent_path(),
                                        setupError);
    check(!setupError,
          "corrupt-target async fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile damagedPrimary(damagedAsyncPath.string());
        damagedPrimary.store(0, 0, {0xFF});
        damagedPrimary.store(1, 0, siblingEncoded.data);
        RegionFile damagedStage(damagedAsyncPath.string() + ".new");
        damagedStage.store(0, 0, newerEncoded.data);
        Persistence damagedPersistence(world, damagedAsyncDir.string(),
                                       "minecraft:plains");
        const auto recoveredTarget =
            damagedPersistence.loadChunkBytesRecovering(0, 0);
        RegionFile recoveredRegion(damagedAsyncPath.string());
        check(recoveredTarget.status ==
                      Persistence::ChunkBytesRead::Status::Loaded &&
                  recoveredTarget.bytes == newerEncoded.data &&
                  recoveredRegion.load(1, 0) == siblingEncoded.data &&
                  std::filesystem::exists(
                      damagedAsyncPath.string() + ".corrupt") &&
                  !std::filesystem::exists(
                      damagedAsyncPath.string() + ".new"),
              "async recovery preserves valid primary siblings before quarantining a corrupt requested chunk");
    }

    const auto structurallyCorruptDir =
        directory / "structural-primary-corruption-async";
    const auto structurallyCorruptPath =
        structurallyCorruptDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(
        structurallyCorruptPath.parent_path(), setupError);
    check(!setupError,
          "structural-corruption fixture creates its scoped region directory");
    if (!setupError) {
        {
            std::ofstream truncated(structurallyCorruptPath,
                                    std::ios::binary | std::ios::trunc);
            const std::array<char, 3> bytes{'b', 'a', 'd'};
            truncated.write(bytes.data(),
                            static_cast<std::streamsize>(bytes.size()));
            check(static_cast<bool>(truncated),
                  "structural-corruption fixture writes a truncated primary header");
        }
        const auto structuralStagePath = std::filesystem::path(
            structurallyCorruptPath.string() + ".new");
        RegionFile structuralStage(structuralStagePath.string());
        structuralStage.store(0, 0, newerEncoded.data);
        structuralStage.store(1, 0, siblingEncoded.data);
        Persistence structuralPersistence(
            world, structurallyCorruptDir.string(), "minecraft:plains");
        const auto recovered =
            structuralPersistence.loadChunkBytesRecovering(0, 0);
        RegionFile repaired(structurallyCorruptPath.string());
        std::error_code backupSizeError;
        const auto backupSize = std::filesystem::file_size(
            structurallyCorruptPath.string() + ".corrupt", backupSizeError);
        check(recovered.status ==
                      Persistence::ChunkBytesRead::Status::Loaded &&
                  recovered.bytes == newerEncoded.data &&
                  repaired.load(0, 0) == newerEncoded.data &&
                  repaired.load(1, 0) == siblingEncoded.data &&
                  !backupSizeError && backupSize == 3 &&
                  !std::filesystem::exists(structuralStagePath),
              "confirmed structural primary corruption is quarantined and repaired from a valid staged region while preserving its siblings");
    }

    const auto corruptMissingStageAsyncDir =
        directory / "corrupt-target-missing-stage-async";
    const auto corruptMissingStageAsyncPath =
        corruptMissingStageAsyncDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(
        corruptMissingStageAsyncPath.parent_path(), setupError);
    check(!setupError,
          "missing-target async recovery fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile damagedPrimary(corruptMissingStageAsyncPath.string());
        damagedPrimary.store(0, 0, {0xFF});
        damagedPrimary.store(1, 0, siblingEncoded.data);
        RegionFile partialStage(corruptMissingStageAsyncPath.string() + ".new");
        partialStage.store(2, 0, stagedSiblingEncoded.data);
        Persistence partialPersistence(world,
                                       corruptMissingStageAsyncDir.string(),
                                       "minecraft:plains");
        const auto missingTarget =
            partialPersistence.loadChunkBytesRecovering(0, 0);
        RegionFile recoveredRegion(corruptMissingStageAsyncPath.string());
        check(missingTarget.status ==
                      Persistence::ChunkBytesRead::Status::Missing &&
                  recoveredRegion.load(1, 0) == siblingEncoded.data &&
                  recoveredRegion.load(2, 0) == stagedSiblingEncoded.data &&
                  std::filesystem::exists(
                      corruptMissingStageAsyncPath.string() + ".corrupt") &&
                  !std::filesystem::exists(
                      corruptMissingStageAsyncPath.string() + ".new"),
              "async recovery discards only the known-corrupt requested slot while salvaging both sibling snapshots");
        partialPersistence.storeChunkBytes(0, 0, encoded.data);
        RegionFile afterGeneratedSave(corruptMissingStageAsyncPath.string());
        check(afterGeneratedSave.load(0, 0) == encoded.data &&
                  afterGeneratedSave.load(1, 0) == siblingEncoded.data &&
                  afterGeneratedSave.load(2, 0) == stagedSiblingEncoded.data,
              "async generated-chunk save retains every salvaged sibling");
    }

    const auto ioFailedAsyncDir = directory / "primary-io-failure-async";
    const auto ioFailedAsyncPath =
        ioFailedAsyncDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(ioFailedAsyncPath, setupError);
    check(!setupError,
          "async primary-I/O fixture creates an unreadable region-path directory");
    if (!setupError) {
        RegionFile partialStage(ioFailedAsyncPath.string() + ".new");
        partialStage.store(0, 0, encoded.data);
        const auto originalStagePath =
            std::filesystem::path(ioFailedAsyncPath.string() + ".new");
        const auto preserved = Persistence(
            world, ioFailedAsyncDir.string(), "minecraft:plains")
                                   .loadChunkBytesRecovering(0, 0);
        check(preserved.status ==
                      Persistence::ChunkBytesRead::Status::Loaded &&
                  preserved.bytes == encoded.data &&
                  std::filesystem::is_directory(ioFailedAsyncPath) &&
                  std::filesystem::exists(originalStagePath) &&
                  RegionFile(originalStagePath.string()).load(0, 0) ==
                      encoded.data &&
                  !std::filesystem::exists(
                      ioFailedAsyncPath.string() + ".corrupt"),
              "async recovery serves a valid staged chunk without promoting or quarantining over a primary I/O failure");
    }

    const auto unsupportedAsyncDir = directory / "primary-unsupported-async";
    const auto unsupportedAsyncPath =
        unsupportedAsyncDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(unsupportedAsyncPath.parent_path(),
                                        setupError);
    check(!setupError,
          "async unsupported-format fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile unsupportedPrimary(unsupportedAsyncPath.string());
        unsupportedPrimary.store(0, 0, encoded.data);
        {
            std::fstream file(unsupportedAsyncPath,
                              std::ios::binary | std::ios::in |
                                  std::ios::out);
            std::array<unsigned char, 4> location{};
            file.read(reinterpret_cast<char*>(location.data()),
                      static_cast<std::streamsize>(location.size()));
            const std::uint32_t sector =
                (static_cast<std::uint32_t>(location[0]) << 16) |
                (static_cast<std::uint32_t>(location[1]) << 8) |
                static_cast<std::uint32_t>(location[2]);
            file.seekp(static_cast<std::streamoff>(sector) * 4096 + 4);
            const char unsupportedCompression = 127;
            file.write(&unsupportedCompression, 1);
            file.flush();
            check(static_cast<bool>(file) && sector >= 2,
                  "unsupported-format fixture marks an unknown compression type");
        }
        RegionFile validStage(unsupportedAsyncPath.string() + ".new");
        validStage.store(0, 0, newerEncoded.data);
        const auto read = Persistence(
            world, unsupportedAsyncDir.string(), "minecraft:plains")
                             .loadChunkBytesRecovering(0, 0);
        bool primaryStillUnsupported = false;
        try {
            (void)RegionFile(unsupportedAsyncPath.string()).load(0, 0);
        } catch (const RegionLoadError& error) {
            primaryStillUnsupported =
                error.kind() == RegionLoadFailureKind::Unsupported;
        }
        check(read.status == Persistence::ChunkBytesRead::Status::Loaded &&
                  read.bytes == newerEncoded.data &&
                  primaryStillUnsupported &&
                  RegionFile(unsupportedAsyncPath.string() + ".new")
                          .load(0, 0) == newerEncoded.data &&
                  !std::filesystem::exists(
                      unsupportedAsyncPath.string() + ".corrupt"),
              "async recovery may read a valid staged chunk but does not promote or quarantine over an unsupported primary");
    }

    const auto siblingDir = directory / "sibling-only-async";
    const auto siblingPath = siblingDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(siblingPath.parent_path(), setupError);
    check(!setupError,
          "sibling-only async fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile siblingStage(siblingPath.string() + ".new");
        siblingStage.store(0, 0, encoded.data);
        Persistence siblingPersistence(world, siblingDir.string(),
                                       "minecraft:plains");
        const auto absentSiblingSlot =
            siblingPersistence.loadChunkBytesRecovering(8, 8);
        check(absentSiblingSlot.status ==
                      Persistence::ChunkBytesRead::Status::Missing &&
                  std::filesystem::exists(siblingPath) &&
                  !std::filesystem::exists(siblingPath.string() + ".new"),
              "async missing-slot recovery promotes a staged region containing sibling chunks");
        siblingPersistence.storeChunkBytes(8, 8, encoded.data);
        RegionFile siblingSaved(siblingPath.string());
        check(siblingSaved.load(0, 0) == encoded.data &&
                  siblingSaved.load(8, 8) == encoded.data,
              "saving a newly generated async chunk preserves staged sibling data");
    }

    const auto directStoreDir = directory / "sibling-only-direct-store";
    const auto directStorePath =
        directStoreDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(directStorePath.parent_path(),
                                        setupError);
    check(!setupError,
          "direct-store sibling fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile directStorePrimary(directStorePath.string());
        directStorePrimary.store(0, 0, encoded.data);
        directStorePrimary.store(1, 0, siblingEncoded.data);
        RegionFile directStoreStage(directStorePath.string() + ".new");
        directStoreStage.store(0, 0, newerEncoded.data);
        Persistence directStorePersistence(world, directStoreDir.string(),
                                           "minecraft:plains");
        directStorePersistence.storeChunkBytes(8, 8, encoded.data);
        RegionFile directStoreSaved(directStorePath.string());
        check(directStoreSaved.load(0, 0) == newerEncoded.data &&
                  directStoreSaved.load(1, 0) == siblingEncoded.data &&
                  directStoreSaved.load(8, 8) == encoded.data &&
                  !std::filesystem::exists(
                      directStorePath.string() + ".new"),
              "direct save preserves a newer staged chunk when a stale primary still exists");
    }

    const auto splitSiblingDir = directory / "split-sibling-async";
    const auto splitSiblingPath =
        splitSiblingDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(splitSiblingPath.parent_path(),
                                        setupError);
    check(!setupError,
          "split-sibling async fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile splitPrimary(splitSiblingPath.string());
        splitPrimary.store(0, 0, encoded.data);
        RegionFile splitStage(splitSiblingPath.string() + ".new");
        splitStage.store(1, 0, siblingEncoded.data);
        Persistence splitPersistence(world, splitSiblingDir.string(),
                                     "minecraft:plains");
        const auto absent = splitPersistence.loadChunkBytesRecovering(2, 0);
        check(absent.status == Persistence::ChunkBytesRead::Status::Missing &&
                  !std::filesystem::exists(splitSiblingPath.string() + ".new"),
              "async recovery merges a missing slot from both sibling-only region snapshots");
        splitPersistence.storeChunkBytes(2, 0, encoded.data);
        RegionFile merged(splitSiblingPath.string());
        check(merged.load(0, 0) == encoded.data &&
                  merged.load(1, 0) == siblingEncoded.data &&
                  merged.load(2, 0) == encoded.data,
              "subsequent async save preserves chunks from both recovered snapshots");
    }

    const auto invalidSiblingDir = directory / "invalid-nbt-sibling";
    const auto invalidSiblingPath =
        invalidSiblingDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(invalidSiblingPath.parent_path(),
                                        setupError);
    check(!setupError,
          "invalid-sibling fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile usablePrimary(invalidSiblingPath.string());
        usablePrimary.store(0, 0, encoded.data);
        usablePrimary.store(1, 0, siblingEncoded.data);
        RegionFile invalidStage(invalidSiblingPath.string() + ".new");
        invalidStage.store(0, 0, newerEncoded.data);
        invalidStage.store(1, 0, {0xFF});
        invalidStage.store(2, 0, stagedSiblingEncoded.data);
        Persistence invalidSiblingPersistence(world,
                                              invalidSiblingDir.string(),
                                              "minecraft:plains");
        const auto recoveredStage =
            invalidSiblingPersistence.loadChunkBytesRecovering(0, 0);
        RegionFile recoveredBeforeSave(invalidSiblingPath.string());
        check(recoveredStage.status ==
                      Persistence::ChunkBytesRead::Status::Loaded &&
                  recoveredStage.bytes == newerEncoded.data &&
                  recoveredBeforeSave.load(0, 0) == newerEncoded.data &&
                  recoveredBeforeSave.load(1, 0) == siblingEncoded.data &&
                  recoveredBeforeSave.load(2, 0) ==
                      stagedSiblingEncoded.data &&
                  recoveredBeforeSave.load(3, 0).empty() &&
                  std::filesystem::exists(invalidSiblingPath.string() +
                                          ".new.corrupt") &&
                  !std::filesystem::exists(invalidSiblingPath.string() +
                                           ".new"),
              "recovery salvages valid staged and primary chunks around a malformed sibling");
        invalidSiblingPersistence.storeChunkBytes(2, 0, encoded.data);
        RegionFile afterSafeSave(invalidSiblingPath.string());
        check(afterSafeSave.load(0, 0) == newerEncoded.data &&
              afterSafeSave.load(1, 0) == siblingEncoded.data &&
              afterSafeSave.load(2, 0) == encoded.data &&
              std::filesystem::exists(invalidSiblingPath.string() +
                                          ".new.corrupt"),
              "later save preserves salvaged staged state and valid primary siblings");
    }

    const auto truncatedStageDir = directory / "truncated-stage-header";
    const auto truncatedStagePath =
        truncatedStageDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(
        truncatedStagePath.parent_path(), setupError);
    check(!setupError,
          "truncated-stage fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile healthyPrimary(truncatedStagePath.string());
        healthyPrimary.store(0, 0, encoded.data);
        healthyPrimary.store(1, 0, siblingEncoded.data);
        {
            std::ofstream truncated(truncatedStagePath.string() + ".new",
                                    std::ios::binary | std::ios::trunc);
            truncated.write("partial", 7);
        }
        Persistence truncatedStagePersistence(
            world, truncatedStageDir.string(), "minecraft:plains");
        const auto recovered =
            truncatedStagePersistence.loadChunkBytesRecovering(0, 0);
        RegionFile recoveredRegion(truncatedStagePath.string());
        check(recovered.status ==
                      Persistence::ChunkBytesRead::Status::Loaded &&
                  recovered.bytes == encoded.data &&
                  recoveredRegion.load(1, 0) == siblingEncoded.data &&
                  std::filesystem::exists(
                      truncatedStagePath.string() + ".new.corrupt") &&
                  !std::filesystem::exists(
                      truncatedStagePath.string() + ".new"),
              "a truncated staged header is quarantined and rebuilt from a healthy primary without losing siblings");
        truncatedStagePersistence.storeChunkBytes(8, 8, newerEncoded.data);
        RegionFile afterTruncatedStageSave(truncatedStagePath.string());
        check(afterTruncatedStageSave.load(0, 0) == encoded.data &&
                  afterTruncatedStageSave.load(1, 0) == siblingEncoded.data &&
                  afterTruncatedStageSave.load(8, 8) == newerEncoded.data,
                  "writes continue after truncated staged-region recovery");
    }

    const auto truncatedPrimaryDir = directory / "truncated-primary-header";
    const auto truncatedPrimaryPath =
        truncatedPrimaryDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(
        truncatedPrimaryPath.parent_path(), setupError);
    check(!setupError,
          "truncated-primary fixture creates its scoped region directory");
    if (!setupError) {
        {
            std::ofstream truncated(truncatedPrimaryPath,
                                    std::ios::binary | std::ios::trunc);
            truncated.write("partial", 7);
        }
        RegionFile invalidStage(truncatedPrimaryPath.string() + ".new");
        invalidStage.store(0, 0, newerEncoded.data);
        invalidStage.store(1, 0, {0xFF});
        invalidStage.store(2, 0, stagedSiblingEncoded.data);

        Persistence truncatedPrimaryPersistence(
            world, truncatedPrimaryDir.string(), "minecraft:plains");
        const auto recovered =
            truncatedPrimaryPersistence.loadChunkBytesRecovering(0, 0);
        RegionFile repairedPrimary(truncatedPrimaryPath.string());
        check(recovered.status ==
                      Persistence::ChunkBytesRead::Status::Loaded &&
                  recovered.bytes == newerEncoded.data &&
                  repairedPrimary.load(0, 0) == newerEncoded.data &&
                  repairedPrimary.load(2, 0) ==
                      stagedSiblingEncoded.data &&
                  repairedPrimary.load(1, 0).empty() &&
                  !std::filesystem::exists(
                      truncatedPrimaryPath.string() + ".new") &&
                  std::filesystem::exists(
                      truncatedPrimaryPath.string() + ".corrupt") &&
                  std::filesystem::exists(
                      truncatedPrimaryPath.string() + ".new.corrupt"),
              "valid staged siblings recover around a malformed slot when the primary region header is truncated");
        truncatedPrimaryPersistence.storeChunkBytes(
            3, 0, encoded.data);
        RegionFile afterTruncatedPrimarySave(truncatedPrimaryPath.string());
        check(afterTruncatedPrimarySave.load(0, 0) == newerEncoded.data &&
                  afterTruncatedPrimarySave.load(2, 0) ==
                      stagedSiblingEncoded.data &&
                  afterTruncatedPrimarySave.load(3, 0) == encoded.data,
              "writes continue after truncated-primary staged recovery");
    }

    auto invalidExtrasRoot = chunkToNBT(0, 0, source,
                                        "minecraft:plains");
    auto invalidEntities = nbt::Value::makeList(nbt::Compound);
    auto invalidEntity = nbt::Value::makeCompound();
    invalidEntity.set("id", nbt::Value::makeString("minecraft:chest"));
    invalidEntity.set("x", nbt::Value::makeInt(32));
    invalidEntity.set("y", nbt::Value::makeInt(100));
    invalidEntity.set("z", nbt::Value::makeInt(0));
    invalidEntities.list.push_back(std::move(invalidEntity));
    invalidExtrasRoot.set("block_entities", std::move(invalidEntities));
    WriteBuffer invalidExtrasEncoded;
    nbt::writeFileRoot(invalidExtrasEncoded, invalidExtrasRoot);

    auto invalidExtrasTagRoot = chunkToNBT(0, 0, source,
                                           "minecraft:plains");
    invalidExtrasTagRoot.set("block_entities", nbt::Value::makeInt(1));
    WriteBuffer invalidExtrasTagEncoded;
    nbt::writeFileRoot(invalidExtrasTagEncoded, invalidExtrasTagRoot);

    auto invalidLegacyExtrasTagRoot = chunkToNBT(0, 0, source,
                                                 "minecraft:plains");
    invalidLegacyExtrasTagRoot.set("TileEntities",
                                   nbt::Value::makeInt(1));
    WriteBuffer invalidLegacyExtrasTagEncoded;
    nbt::writeFileRoot(invalidLegacyExtrasTagEncoded,
                       invalidLegacyExtrasTagRoot);

    auto invalidExtrasListRoot = chunkToNBT(0, 0, source,
                                            "minecraft:plains");
    auto invalidExtrasList = nbt::Value::makeList(nbt::Int);
    invalidExtrasList.list.push_back(nbt::Value::makeInt(1));
    invalidExtrasListRoot.set("block_entities", std::move(invalidExtrasList));
    WriteBuffer invalidExtrasListEncoded;
    nbt::writeFileRoot(invalidExtrasListEncoded, invalidExtrasListRoot);

    const auto makeChestRoot = [&]() {
        auto root = chunkToNBT(0, 0, source, "minecraft:plains");
        auto entities = nbt::Value::makeList(nbt::Compound);
        auto chest = nbt::Value::makeCompound();
        chest.set("id", nbt::Value::makeString("minecraft:chest"));
        chest.set("x", nbt::Value::makeInt(1));
        chest.set("y", nbt::Value::makeInt(100));
        chest.set("z", nbt::Value::makeInt(1));
        entities.list.push_back(std::move(chest));
        root.set("block_entities", std::move(entities));
        return root;
    };
    auto duplicateBlockEntityRoot = makeChestRoot();
    duplicateBlockEntityRoot.get("block_entities")->list.push_back(
        duplicateBlockEntityRoot.get("block_entities")->list.front());
    WriteBuffer duplicateBlockEntityEncoded;
    nbt::writeFileRoot(duplicateBlockEntityEncoded,
                       duplicateBlockEntityRoot);

    auto duplicateSlotRoot = makeChestRoot();
    auto& duplicateSlotChest =
        duplicateSlotRoot.get("block_entities")->list.front();
    auto duplicateSlotItems = nbt::Value::makeList(nbt::Compound);
    for (const auto* itemName : {"minecraft:diamond", "minecraft:emerald"}) {
        auto item = nbt::Value::makeCompound();
        item.set("id", nbt::Value::makeString(itemName));
        item.set("Count", nbt::Value::makeByte(1));
        item.set("Slot", nbt::Value::makeByte(0));
        duplicateSlotItems.list.push_back(std::move(item));
    }
    duplicateSlotChest.set("Items", std::move(duplicateSlotItems));
    WriteBuffer duplicateSlotEncoded;
    nbt::writeFileRoot(duplicateSlotEncoded, duplicateSlotRoot);

    auto duplicateOpaqueSlotRoot = makeChestRoot();
    auto& duplicateOpaqueChest =
        duplicateOpaqueSlotRoot.get("block_entities")->list.front();
    auto duplicateOpaqueItems = nbt::Value::makeList(nbt::Compound);
    auto opaqueDiamond = nbt::Value::makeCompound();
    opaqueDiamond.set("id", nbt::Value::makeString("minecraft:diamond"));
    opaqueDiamond.set("Count", nbt::Value::makeByte(1));
    opaqueDiamond.set("Slot", nbt::Value::makeByte(0));
    auto opaqueComponents = nbt::Value::makeCompound();
    opaqueComponents.set("example:custom_data",
                         nbt::Value::makeString("preserve opaque stack"));
    opaqueDiamond.set("components", std::move(opaqueComponents));
    duplicateOpaqueItems.list.push_back(std::move(opaqueDiamond));
    auto duplicateOpaqueEmerald = nbt::Value::makeCompound();
    duplicateOpaqueEmerald.set("id",
                               nbt::Value::makeString("minecraft:emerald"));
    duplicateOpaqueEmerald.set("Count", nbt::Value::makeByte(1));
    duplicateOpaqueEmerald.set("Slot", nbt::Value::makeByte(0));
    duplicateOpaqueItems.list.push_back(std::move(duplicateOpaqueEmerald));
    duplicateOpaqueChest.set("Items", std::move(duplicateOpaqueItems));
    WriteBuffer duplicateOpaqueSlotEncoded;
    nbt::writeFileRoot(duplicateOpaqueSlotEncoded,
                       duplicateOpaqueSlotRoot);

    auto malformedItemsTagRoot = makeChestRoot();
    malformedItemsTagRoot.get("block_entities")->list.front().set(
        "Items", nbt::Value::makeInt(1));
    WriteBuffer malformedItemsTagEncoded;
    nbt::writeFileRoot(malformedItemsTagEncoded, malformedItemsTagRoot);

    auto malformedItemsElementRoot = makeChestRoot();
    auto malformedItemsElement = nbt::Value::makeList(nbt::Int);
    malformedItemsElement.list.push_back(nbt::Value::makeInt(1));
    malformedItemsElementRoot.get("block_entities")->list.front().set(
        "Items", std::move(malformedItemsElement));
    WriteBuffer malformedItemsElementEncoded;
    nbt::writeFileRoot(malformedItemsElementEncoded,
                       malformedItemsElementRoot);

    BlockEntityStore primaryBlockEntities;
    auto primaryChest = primaryBlockEntities.createShared(
        posKey(1, 100, 1), BlockEntity::Kind::Chest);
    primaryChest->chest.slots[0] = ItemStack::ofName("minecraft:diamond", 5);
    auto validPrimaryRoot = chunkToNBT(0, 0, source, "minecraft:plains");
    auto validPrimaryEntities = nbt::Value::makeList(nbt::Compound);
    primaryBlockEntities.writeChunkNbt(0, 0, validPrimaryEntities);
    validPrimaryRoot.set("block_entities", std::move(validPrimaryEntities));
    WriteBuffer validPrimaryEncoded;
    nbt::writeFileRoot(validPrimaryEncoded, validPrimaryRoot);

    const auto rejectInvalidExtrasStage = [&](
        const std::filesystem::path& dimensionDir,
        const std::vector<std::uint8_t>& invalidBytes) {
        const auto dimensionRegion = dimensionDir / "region" / "r.0.0.mca";
        setupError.clear();
        std::filesystem::create_directories(dimensionRegion.parent_path(),
                                            setupError);
        check(!setupError,
              "invalid block-entity extras fixture creates a dimension region directory");
        if (setupError) return;

        RegionFile extrasPrimary(dimensionRegion.string());
        extrasPrimary.store(0, 0, validPrimaryEncoded.data);
        RegionFile extrasStage(dimensionRegion.string() + ".new");
        extrasStage.store(0, 0, invalidBytes);
        Persistence extrasPersistence(world, dimensionDir.string(),
                                      "minecraft:plains");
        extrasPersistence.setChunkExtras(
            {}, {}, [](const nbt::Value& root) {
                BlockEntityStore::validateChunkNbt(root);
            });
        const auto safeExtrasPrimary =
            extrasPersistence.loadChunkBytesRecovering(0, 0);
        check(safeExtrasPrimary.status ==
                      Persistence::ChunkBytesRead::Status::Loaded &&
                  safeExtrasPrimary.bytes == validPrimaryEncoded.data &&
                  !std::filesystem::exists(dimensionRegion.string() + ".new") &&
                  std::filesystem::exists(dimensionRegion.string() +
                                          ".new.corrupt"),
              "malformed block-entity extras are quarantined without losing the usable primary inventory");
        extrasPersistence.storeChunkBytes(2, 0, encoded.data);
        RegionFile afterInvalidExtras(dimensionRegion.string());
        check(afterInvalidExtras.load(0, 0) == validPrimaryEncoded.data &&
                  afterInvalidExtras.load(2, 0) == encoded.data &&
                  std::filesystem::exists(dimensionRegion.string() +
                                          ".new.corrupt"),
              "direct save preserves salvaged inventory after malformed extras are quarantined");
    };

    for (const auto* dimensionName : {"DIM-1", "DIM1"}) {
        rejectInvalidExtrasStage(directory / "invalid-coordinate" /
                                     dimensionName,
                                 invalidExtrasEncoded.data);
        rejectInvalidExtrasStage(directory / "invalid-tag" / dimensionName,
                                 invalidExtrasTagEncoded.data);
        rejectInvalidExtrasStage(directory / "invalid-legacy-tag" /
                                     dimensionName,
                                 invalidLegacyExtrasTagEncoded.data);
        rejectInvalidExtrasStage(directory / "invalid-list-element" /
                                     dimensionName,
                                 invalidExtrasListEncoded.data);
        rejectInvalidExtrasStage(directory / "duplicate-block-entity" /
                                     dimensionName,
                                 duplicateBlockEntityEncoded.data);
        rejectInvalidExtrasStage(directory / "duplicate-item-slot" /
                                     dimensionName,
                                 duplicateSlotEncoded.data);
        rejectInvalidExtrasStage(directory / "duplicate-opaque-item-slot" /
                                     dimensionName,
                                 duplicateOpaqueSlotEncoded.data);
        rejectInvalidExtrasStage(directory / "malformed-items-tag" /
                                     dimensionName,
                                 malformedItemsTagEncoded.data);
        rejectInvalidExtrasStage(directory / "malformed-items-element" /
                                     dimensionName,
                                 malformedItemsElementEncoded.data);
    }

    const auto failedDir = directory / "failed";
    std::filesystem::create_directories(failedDir / "region", setupError);
    check(!setupError, "failed-read fixture creates its scoped region directory");
    const auto failedPath = failedDir / "region" / "r.0.0.mca";
    RegionFile corrupt(failedPath.string());
    corrupt.store(0, 0, {0xFF});
    Persistence failedPersistence(world, failedDir.string(), "minecraft:plains");
    const auto failed = failedPersistence.loadChunkBytesRecovering(0, 0);
    check(failed.status == Persistence::ChunkBytesRead::Status::Failed &&
              std::filesystem::exists(failedPath),
          "async chunk reader reports unrecoverable data and preserves it instead of regenerating over it");

    const auto syncDir = directory / "sync-recovery";
    const auto syncRegionPath = syncDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(syncRegionPath.parent_path(), setupError);
    RegionFile syncPrimary(syncRegionPath.string());
    syncPrimary.store(0, 0, {0xFF});
    RegionFile syncStaged(syncRegionPath.string() + ".new");
    syncStaged.store(0, 0, encoded.data);
    World syncWorld("minecraft:plains", LevelType::Flat, 0);
    Persistence syncPersistence(syncWorld, syncDir.string(),
                               "minecraft:plains");
    syncPersistence.start();
    const bool syncRecovered = syncWorld.generateChunkIfMissing(0, 0);
    check(syncRecovered && syncWorld.getBlock(4, 100, 4) == stone &&
              !std::filesystem::exists(syncRegionPath.string() + ".new"),
          "synchronous world loading promotes and publishes a valid staged chunk");
    stopPersistenceAndWait(syncPersistence);

    const auto damagedSiblingDir = directory / "corrupt-target-valid-sibling";
    const auto damagedSiblingPath =
        damagedSiblingDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(damagedSiblingPath.parent_path(),
                                        setupError);
    check(!setupError,
          "corrupt-target recovery fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile damagedPrimary(damagedSiblingPath.string());
        damagedPrimary.store(0, 0, {0xFF});
        damagedPrimary.store(1, 0, siblingEncoded.data);
        RegionFile damagedStage(damagedSiblingPath.string() + ".new");
        damagedStage.store(0, 0, newerEncoded.data);
        World damagedWorld("minecraft:plains", LevelType::Flat, 0);
        Persistence damagedPersistence(damagedWorld,
                                       damagedSiblingDir.string(),
                                       "minecraft:plains");
        damagedPersistence.start();
        const bool recoveredTarget =
            damagedWorld.generateChunkIfMissing(0, 0);
        stopPersistenceAndWait(damagedPersistence);
        RegionFile recoveredRegion(damagedSiblingPath.string());
        check(recoveredTarget && damagedWorld.getBlock(4, 100, 4) == dirt &&
                  recoveredRegion.load(1, 0) == siblingEncoded.data &&
                  std::filesystem::exists(
                      damagedSiblingPath.string() + ".corrupt") &&
                  !std::filesystem::exists(
                      damagedSiblingPath.string() + ".new"),
              "sync recovery preserves valid primary siblings before quarantining a corrupt requested chunk");
    }

    const auto syncMissingTargetDir =
        directory / "corrupt-target-missing-stage-sync";
    const auto syncMissingTargetPath =
        syncMissingTargetDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(syncMissingTargetPath.parent_path(),
                                        setupError);
    check(!setupError,
          "missing-target sync recovery fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile damagedPrimary(syncMissingTargetPath.string());
        damagedPrimary.store(0, 0, {0xFF});
        damagedPrimary.store(1, 0, siblingEncoded.data);
        RegionFile partialStage(syncMissingTargetPath.string() + ".new");
        partialStage.store(2, 0, stagedSiblingEncoded.data);
        World missingTargetWorld("minecraft:plains", LevelType::Flat, 0);
        Persistence missingTargetPersistence(missingTargetWorld,
                                             syncMissingTargetDir.string(),
                                             "minecraft:plains");
        missingTargetPersistence.start();
        const bool targetRegenerated =
            missingTargetWorld.generateChunkIfMissing(0, 0);
        const bool generatedChunkPublished =
            missingTargetWorld.hasChunk(0, 0);
        stopPersistenceAndWait(missingTargetPersistence);
        RegionFile recoveredRegion(syncMissingTargetPath.string());
        check(targetRegenerated && generatedChunkPublished &&
                  recoveredRegion.load(1, 0) == siblingEncoded.data &&
                  recoveredRegion.load(2, 0) == stagedSiblingEncoded.data &&
                  std::filesystem::exists(
                      syncMissingTargetPath.string() + ".corrupt") &&
                  !std::filesystem::exists(
                      syncMissingTargetPath.string() + ".new"),
              "sync loading regenerates a corrupt target after preserving primary and staged siblings");
        missingTargetPersistence.storeChunkBytes(0, 0, encoded.data);
        RegionFile afterGeneratedSave(syncMissingTargetPath.string());
        check(afterGeneratedSave.load(0, 0) == encoded.data &&
                  afterGeneratedSave.load(1, 0) == siblingEncoded.data &&
                  afterGeneratedSave.load(2, 0) == stagedSiblingEncoded.data,
              "sync generated-chunk save retains every salvaged sibling");
    }

    const auto ioFailedSyncDir = directory / "primary-io-failure-sync";
    const auto ioFailedSyncPath = ioFailedSyncDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(ioFailedSyncPath, setupError);
    check(!setupError,
          "sync primary-I/O fixture creates an unreadable region-path directory");
    if (!setupError) {
        RegionFile partialStage(ioFailedSyncPath.string() + ".new");
        partialStage.store(2, 0, stagedSiblingEncoded.data);
        World ioFailedWorld("minecraft:plains", LevelType::Flat, 0);
        Persistence ioFailedPersistence(ioFailedWorld,
                                        ioFailedSyncDir.string(),
                                        "minecraft:plains");
        Chunk unpublished;
        ChunkBuildWriter unpublishedWriter(unpublished);
        const auto status = ioFailedPersistence.loadChunkStatus(
            0, 0, unpublishedWriter);
        check(status == ChunkLoadStatus::Failed &&
                  std::filesystem::is_directory(ioFailedSyncPath) &&
                  std::filesystem::exists(ioFailedSyncPath.string() + ".new") &&
                  !std::filesystem::exists(
                      ioFailedSyncPath.string() + ".corrupt"),
              "sync recovery fails closed and preserves the primary after a region I/O error");
    }

    const auto syncConflictDir = directory / "valid-conflict-sync";
    const auto syncConflictPath =
        syncConflictDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(syncConflictPath.parent_path(),
                                        setupError);
    check(!setupError,
          "valid-conflict sync fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile syncConflictPrimary(syncConflictPath.string());
        syncConflictPrimary.store(0, 0, encoded.data);
        syncConflictPrimary.store(1, 0, siblingEncoded.data);
        RegionFile syncConflictStage(syncConflictPath.string() + ".new");
        syncConflictStage.store(0, 0, newerEncoded.data);
        World syncConflictWorld("minecraft:plains", LevelType::Flat, 0);
        Persistence syncConflictPersistence(syncConflictWorld,
                                            syncConflictDir.string(),
                                            "minecraft:plains");
        syncConflictPersistence.start();
        const bool loadedNewer =
            syncConflictWorld.generateChunkIfMissing(0, 0);
        check(loadedNewer && syncConflictWorld.getBlock(4, 100, 4) == dirt &&
                  !std::filesystem::exists(syncConflictPath.string() + ".new"),
              "sync world loading publishes the newer valid staged chunk over a stale primary");
        stopPersistenceAndWait(syncConflictPersistence);
        RegionFile syncConflictSaved(syncConflictPath.string());
        check(syncConflictSaved.load(0, 0) == newerEncoded.data &&
                  syncConflictSaved.load(1, 0) == siblingEncoded.data,
              "sync recovery retains primary-only sibling chunks when staged target wins");
    }

    const auto syncMalformedSiblingDir =
        directory / "malformed-staged-sibling-sync";
    const auto syncMalformedSiblingPath =
        syncMalformedSiblingDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(
        syncMalformedSiblingPath.parent_path(), setupError);
    check(!setupError,
          "malformed staged-sibling sync fixture creates its region directory");
    if (!setupError) {
        RegionFile primary(syncMalformedSiblingPath.string());
        primary.store(0, 0, encoded.data);
        primary.store(1, 0, siblingEncoded.data);
        RegionFile staged(syncMalformedSiblingPath.string() + ".new");
        staged.store(0, 0, newerEncoded.data);
        staged.store(1, 0, {0xFF});
        staged.store(2, 0, stagedSiblingEncoded.data);

        World malformedSiblingWorld("minecraft:plains", LevelType::Flat, 0);
        Persistence malformedSiblingPersistence(
            malformedSiblingWorld, syncMalformedSiblingDir.string(),
            "minecraft:plains");
        malformedSiblingPersistence.start();
        const bool loadedNewer =
            malformedSiblingWorld.generateChunkIfMissing(0, 0);
        stopPersistenceAndWait(malformedSiblingPersistence);

        RegionFile recovered(syncMalformedSiblingPath.string());
        check(loadedNewer &&
                  malformedSiblingWorld.getBlock(4, 100, 4) == dirt &&
                  recovered.load(0, 0) == newerEncoded.data &&
                  recovered.load(1, 0) == siblingEncoded.data &&
                  recovered.load(2, 0) == stagedSiblingEncoded.data &&
                  recovered.load(3, 0).empty() &&
                  std::filesystem::exists(
                      syncMalformedSiblingPath.string() + ".new.corrupt") &&
                  !std::filesystem::exists(
                      syncMalformedSiblingPath.string() + ".new"),
              "sync loading salvages valid staged and primary chunks around malformed staged NBT");
    }

    const auto syncSiblingDir = directory / "sibling-only-sync";
    const auto syncSiblingPath =
        syncSiblingDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(syncSiblingPath.parent_path(),
                                        setupError);
    check(!setupError,
          "sibling-only sync fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile syncSiblingStage(syncSiblingPath.string() + ".new");
        syncSiblingStage.store(0, 0, encoded.data);
        World syncSiblingWorld("minecraft:plains", LevelType::Flat, 0);
        Persistence syncSiblingPersistence(syncSiblingWorld,
                                           syncSiblingDir.string(),
                                           "minecraft:plains");
        Chunk missingChunk;
        ChunkBuildWriter missingWriter(missingChunk);
        const auto missingSlotStatus =
            syncSiblingPersistence.loadChunkStatus(8, 8, missingWriter);
        check(missingSlotStatus == ChunkLoadStatus::Missing &&
                  std::filesystem::exists(syncSiblingPath) &&
                  !std::filesystem::exists(syncSiblingPath.string() + ".new"),
              "sync missing-slot recovery promotes a staged region containing sibling chunks");
        syncSiblingPersistence.storeChunkBytes(8, 8, encoded.data);
        RegionFile syncSiblingSaved(syncSiblingPath.string());
        check(syncSiblingSaved.load(0, 0) == encoded.data &&
                  syncSiblingSaved.load(8, 8) == encoded.data,
              "sync save of a newly generated chunk preserves staged sibling data");
    }

    const auto syncSplitDir = directory / "split-sibling-sync";
    const auto syncSplitPath = syncSplitDir / "region" / "r.0.0.mca";
    std::filesystem::create_directories(syncSplitPath.parent_path(),
                                        setupError);
    check(!setupError,
          "split-sibling sync fixture creates its scoped region directory");
    if (!setupError) {
        RegionFile syncSplitPrimary(syncSplitPath.string());
        syncSplitPrimary.store(0, 0, encoded.data);
        RegionFile syncSplitStage(syncSplitPath.string() + ".new");
        syncSplitStage.store(1, 0, siblingEncoded.data);
        World syncSplitWorld("minecraft:plains", LevelType::Flat, 0);
        Persistence syncSplitPersistence(syncSplitWorld,
                                         syncSplitDir.string(),
                                         "minecraft:plains");
        Chunk missingChunk;
        ChunkBuildWriter missingWriter(missingChunk);
        const auto missingSlot =
            syncSplitPersistence.loadChunkStatus(2, 0, missingWriter);
        check(missingSlot == ChunkLoadStatus::Missing &&
                  !std::filesystem::exists(syncSplitPath.string() + ".new"),
              "sync loader merges primary-only siblings before reporting a missing chunk");
        syncSplitPersistence.storeChunkBytes(2, 0, encoded.data);
        RegionFile syncMerged(syncSplitPath.string());
        check(syncMerged.load(0, 0) == encoded.data &&
                  syncMerged.load(1, 0) == siblingEncoded.data &&
                  syncMerged.load(2, 0) == encoded.data,
              "sync save preserves all sibling chunks after region reconciliation");
    }

    World syncFailedWorld("minecraft:plains", LevelType::Flat, 0);
    Persistence syncFailedPersistence(syncFailedWorld, failedDir.string(),
                                      "minecraft:plains");
    syncFailedPersistence.start();
    check(!syncFailedWorld.generateChunkIfMissing(0, 0) &&
              !syncFailedWorld.hasChunk(0, 0) &&
              std::filesystem::exists(failedPath),
          "synchronous world loading refuses to generate over unrecoverable persisted bytes");
    stopPersistenceAndWait(syncFailedPersistence);

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "async chunk-recovery fixture removes its exact temporary directory");
}

void testStagedExternalSiblingRecoverySaveAndReopen() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto worldDirectory = std::filesystem::temp_directory_path() /
        ("cppfm-staged-external-recovery-" + std::to_string(unique));
    const auto regionPath = worldDirectory / "region" / "r.0.0.mca";
    std::error_code setupError;
    std::filesystem::create_directories(regionPath.parent_path(), setupError);
    check(!setupError,
          "staged-external fixture creates its scoped region directory");
    if (setupError) return;

    const auto& states = gen::blockNameToState();
    const auto stone = static_cast<std::uint16_t>(
        states.at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        states.at("minecraft:dirt"));
    const auto encodeChunk = [](std::int32_t chunkX, std::int32_t chunkZ,
                                std::uint16_t blockState) {
        Chunk chunk;
        ChunkBuildWriter writer(chunk);
        writer.blocks.set(Chunk::index(10, 4, 4, 4), blockState);
        const auto root = chunkToNBT(chunkX, chunkZ, chunk,
                                     "minecraft:plains");
        WriteBuffer bytes;
        nbt::writeFileRoot(bytes, root);
        return bytes.data;
    };
    const auto oldTarget = encodeChunk(0, 0, stone);
    const auto stagedTarget = encodeChunk(0, 0, dirt);
    const auto externalSibling = encodeChunk(1, 0, stone);
    const auto laterSave = encodeChunk(8, 8, dirt);
    const auto stagedPath = std::filesystem::path(
        regionPath.string() + ".new");
    const auto sidecarPath = regionPath.parent_path() / "c.1.0.mcc";

    try {
        RegionFile primary(regionPath.string());
        primary.store(0, 0, oldTarget);
        installExternalRegionStub(regionPath, 1, 0);
        writeFixtureBytes(sidecarPath, externalSibling);
        check(RegionFile(regionPath.string()).load(1, 0) == externalSibling,
              "fixture canonical MCA and .mcc decode the untouched external sibling");

        std::filesystem::copy_file(
            regionPath, stagedPath,
            std::filesystem::copy_options::overwrite_existing);
        RegionFile staged(stagedPath.string());
        staged.store(0, 0, stagedTarget);
        staged.store(2, 0, {0xff}); // force slot-wise staged recovery
        bool ordinaryStagedReadRejected = false;
        try {
            (void)RegionFile(stagedPath.string()).load(1, 0);
        } catch (const RegionLoadError& error) {
            ordinaryStagedReadRejected =
                error.kind() == RegionLoadFailureKind::Unsupported;
        }
        check(ordinaryStagedReadRejected,
              "ordinary staged load remains fail-closed for external chunks");

        {
            World world("minecraft:plains", LevelType::Flat, 0);
            Persistence persistence(world, worldDirectory.string(),
                                    "minecraft:plains");
            const auto recovered =
                persistence.loadChunkBytesRecovering(0, 0);
            check(recovered.status ==
                          Persistence::ChunkBytesRead::Status::Loaded &&
                      recovered.bytes == stagedTarget &&
                      !std::filesystem::exists(stagedPath) &&
                      std::filesystem::exists(stagedPath.string() +
                                              ".corrupt"),
                  "recovery salvages staged data and binds its external sibling to the matching canonical stub and .mcc");

            RegionFile repaired(regionPath.string());
            check(repaired.load(0, 0) == stagedTarget &&
                      repaired.load(1, 0) == externalSibling &&
                      repaired.load(2, 0).empty(),
                  "recovered region inlines the external sibling and drops only the malformed staged slot");

            check(persistence.storeChunkBytes(8, 8, laterSave),
                  "subsequent save succeeds after external-sibling recovery");
        }

        RegionFile reopened(regionPath.string());
        check(reopened.load(0, 0) == stagedTarget &&
                  reopened.load(1, 0) == externalSibling &&
                  reopened.load(8, 8) == laterSave &&
                  reopened.load(2, 0).empty(),
              "reopen after the subsequent save retains recovered target and sibling bytes");
    } catch (const std::exception& error) {
        check(false, (std::string("staged-external fixture operation failed: ") +
                      error.what()).c_str());
    }

    const auto corruptSidecarWorld = worldDirectory / "corrupt-sidecar";
    const auto corruptSidecarRegion = corruptSidecarWorld / "region" /
                                     "r.0.0.mca";
    const auto corruptSidecarStage = std::filesystem::path(
        corruptSidecarRegion.string() + ".new");
    const auto corruptSidecarPath = corruptSidecarRegion.parent_path() /
                                    "c.1.0.mcc";
    std::error_code corruptSetupError;
    std::filesystem::create_directories(
        corruptSidecarRegion.parent_path(), corruptSetupError);
    check(!corruptSetupError,
          "corrupt-sidecar fixture creates a separate scoped region directory");
    if (!corruptSetupError) {
        try {
            RegionFile primary(corruptSidecarRegion.string());
            primary.store(0, 0, oldTarget);
            installExternalRegionStub(corruptSidecarRegion, 1, 0);
            writeFixtureBytes(corruptSidecarPath, externalSibling);
            std::filesystem::copy_file(
                corruptSidecarRegion, corruptSidecarStage,
                std::filesystem::copy_options::overwrite_existing);
            RegionFile staged(corruptSidecarStage.string());
            staged.store(0, 0, stagedTarget);
            staged.store(2, 0, {0xff});
            // An empty sidecar is structurally corrupt, but it must not stop
            // recovery from retaining independently valid staged siblings.
            writeFixtureBytes(corruptSidecarPath, {});

            World world("minecraft:plains", LevelType::Flat, 0);
            Persistence persistence(world, corruptSidecarWorld.string(),
                                    "minecraft:plains");
            const auto recovered =
                persistence.loadChunkBytesRecovering(0, 0);
            RegionFile repaired(corruptSidecarRegion.string());
            check(recovered.status ==
                          Persistence::ChunkBytesRead::Status::Loaded &&
                      recovered.bytes == stagedTarget &&
                      repaired.load(0, 0) == stagedTarget &&
                      repaired.load(1, 0).empty() &&
                      !std::filesystem::exists(corruptSidecarStage),
                  "a corrupt external sidecar is isolated while valid staged sibling chunks are salvaged");
        } catch (const std::exception& error) {
            check(false, (std::string(
                "corrupt-sidecar recovery fixture failed: ") +
                error.what()).c_str());
        }
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(worldDirectory, cleanupError);
    check(!cleanupError,
          "staged-external fixture removes only its scoped temporary world");
}

void testValidStagedExternalSidecarPromotion() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("cppfm-valid-staged-external-" + std::to_string(unique));
    std::error_code setupError;
    std::filesystem::create_directories(root, setupError);
    check(!setupError,
          "valid staged-external fixture creates its scoped temporary directory");
    if (setupError) return;

    const auto& states = gen::blockNameToState();
    const auto stone = static_cast<std::uint16_t>(
        states.at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        states.at("minecraft:dirt"));
    const auto gold = static_cast<std::uint16_t>(
        states.at("minecraft:gold_block"));
    const auto encodeChunk = [](std::int32_t chunkX, std::int32_t chunkZ,
                                std::uint16_t blockState) {
        Chunk chunk;
        ChunkBuildWriter writer(chunk);
        writer.blocks.set(Chunk::index(10, 4, 4, 4), blockState);
        const auto rootNbt = chunkToNBT(chunkX, chunkZ, chunk,
                                       "minecraft:plains");
        WriteBuffer bytes;
        nbt::writeFileRoot(bytes, rootNbt);
        return bytes.data;
    };
    const auto oldTarget = encodeChunk(0, 0, stone);
    const auto stagedTarget = encodeChunk(0, 0, dirt);
    const auto stagedSibling = encodeChunk(1, 0, gold);
    const auto laterSave = encodeChunk(8, 8, gold);

    const auto runLoadCase = [&](const char* label, bool staleCanonical,
                                 bool preexistingCleanupMarker) {
        const auto caseDirectory = root / label;
        const auto regionPath = caseDirectory / "region" / "r.0.0.mca";
        const auto stagedPath = std::filesystem::path(
            regionPath.string() + ".new");
        const auto canonicalSidecar =
            regionPath.parent_path() / "c.0.0.mcc";
        const auto stagedSidecar =
            regionPath.parent_path() / "c.0.0.mcc.new";
        std::error_code caseError;
        std::filesystem::create_directories(regionPath.parent_path(),
                                            caseError);
        check(!caseError,
              "valid staged-external load case creates its region directory");
        if (caseError) return;

        try {
            RegionFile primary(regionPath.string());
            primary.store(0, 0, oldTarget);
            if (staleCanonical)
                writeFixtureBytes(canonicalSidecar, oldTarget);
            std::filesystem::copy_file(
                regionPath, stagedPath,
                std::filesystem::copy_options::overwrite_existing);
            installExternalRegionStub(stagedPath, 0, 0);
            writeFixtureBytes(stagedSidecar, stagedTarget);
            if (preexistingCleanupMarker) {
                // Simulate restart after the MCA rewrite was synced, but
                // before the now-unreferenced sidecar was removed.
                RegionFile(stagedPath.string()).store(0, 0, stagedTarget);
                writeFixtureBytes(stagedPath.string() + ".sidecars-pending", {});
            }

            World world("minecraft:plains", LevelType::Flat, 0);
            Persistence persistence(world, caseDirectory.string(),
                                    "minecraft:plains");
            const auto recovered =
                persistence.loadChunkBytesRecovering(0, 0);
            check(recovered.status ==
                          Persistence::ChunkBytesRead::Status::Loaded &&
                      recovered.bytes == stagedTarget,
                  "valid staged external payload wins with a missing or stale canonical sidecar");

            RegionFile reopened(regionPath.string());
            check(reopened.load(0, 0) == stagedTarget &&
                      !std::filesystem::exists(stagedPath) &&
                      !std::filesystem::exists(stagedSidecar) &&
                      !std::filesystem::exists(
                          stagedPath.string() + ".sidecars-pending"),
                  "promoted chunk is inline and the staged sidecar transaction is fully consumed");
            if (staleCanonical) {
                check(std::filesystem::exists(canonicalSidecar) &&
                          readFixtureBytes(canonicalSidecar) == oldTarget,
                      "stale canonical sidecar is neither rebound nor overwritten");
            } else {
                check(!std::filesystem::exists(canonicalSidecar),
                      "staged promotion does not require or invent a canonical sidecar");
            }
        } catch (const std::exception& exception) {
            check(false, (std::string("valid staged-external load case failed: ") +
                          exception.what()).c_str());
        }
    };

    runLoadCase("missing-canonical", false, false);
    runLoadCase("stale-canonical", true, false);
    // Simulates a restart after the durable marker was installed but before
    // sidecar cleanup completed.
    runLoadCase("resume-cleanup", true, true);

    const auto saveDirectory = root / "save-path";
    const auto saveRegionPath = saveDirectory / "region" / "r.0.0.mca";
    const auto saveStagedPath = std::filesystem::path(
        saveRegionPath.string() + ".new");
    const auto saveSidecar0 = saveRegionPath.parent_path() / "c.0.0.mcc.new";
    const auto saveSidecar1 = saveRegionPath.parent_path() / "c.1.0.mcc.new";
    std::error_code saveSetupError;
    std::filesystem::create_directories(saveRegionPath.parent_path(),
                                        saveSetupError);
    check(!saveSetupError,
          "valid staged-external save case creates its scoped region directory");
    if (!saveSetupError) {
        try {
            RegionFile primary(saveRegionPath.string());
            primary.store(0, 0, oldTarget);
            primary.store(1, 0, stagedSibling);
            std::filesystem::copy_file(
                saveRegionPath, saveStagedPath,
                std::filesystem::copy_options::overwrite_existing);
            installExternalRegionStub(saveStagedPath, 0, 0);
            installExternalRegionStub(saveStagedPath, 1, 0);
            writeFixtureBytes(saveSidecar0, stagedTarget);
            writeFixtureBytes(saveSidecar1, stagedSibling);
            writeFixtureBytes(
                saveRegionPath.parent_path() / "c.0.0.mcc", oldTarget);

            World world("minecraft:plains", LevelType::Flat, 0);
            Persistence persistence(world, saveDirectory.string(),
                                    "minecraft:plains");
            check(persistence.storeChunkBytes(8, 8, laterSave),
                  "save prepares a fully valid staged external generation before writing");

            RegionFile reopened(saveRegionPath.string());
            check(reopened.load(0, 0) == stagedTarget &&
                      reopened.load(1, 0) == stagedSibling &&
                      reopened.load(8, 8) == laterSave &&
                      !std::filesystem::exists(saveSidecar0) &&
                      !std::filesystem::exists(saveSidecar1) &&
                      !std::filesystem::exists(
                          saveStagedPath.string() + ".sidecars-pending"),
                  "save promotion preserves staged chunks inline and consumes staged sidecars");
            check(readFixtureBytes(saveRegionPath.parent_path() /
                                       "c.0.0.mcc") == oldTarget,
                  "save promotion leaves an unrelated stale canonical sidecar untouched");
        } catch (const std::exception& exception) {
            check(false, (std::string("valid staged-external save case failed: ") +
                          exception.what()).c_str());
        }
    }

    const auto failureDirectory = root / "cleanup-failure";
    const auto failureRegionPath =
        failureDirectory / "region" / "r.0.0.mca";
    const auto failureStagedPath = std::filesystem::path(
        failureRegionPath.string() + ".new");
    const auto failureSidecar =
        failureRegionPath.parent_path() / "c.0.0.mcc.new";
    std::error_code failureSetupError;
    std::filesystem::create_directories(
        failureRegionPath.parent_path(), failureSetupError);
    check(!failureSetupError,
          "staged-sidecar failure case creates its scoped region directory");
    if (!failureSetupError) {
        try {
            RegionFile primary(failureRegionPath.string());
            primary.store(0, 0, oldTarget);
            std::filesystem::copy_file(
                failureRegionPath, failureStagedPath,
                std::filesystem::copy_options::overwrite_existing);
            installExternalRegionStub(failureStagedPath, 0, 0);
            std::filesystem::create_directory(failureSidecar);

            World world("minecraft:plains", LevelType::Flat, 0);
            Persistence persistence(world, failureDirectory.string(),
                                    "minecraft:plains");
            const auto failed =
                persistence.loadChunkBytesRecovering(0, 0);
            check(failed.status ==
                          Persistence::ChunkBytesRead::Status::Loaded &&
                      failed.bytes == oldTarget &&
                      std::filesystem::exists(failureStagedPath) &&
                      std::filesystem::exists(
                          failureStagedPath.string() + ".sidecars-pending") &&
                      RegionFile(failureRegionPath.string()).load(0, 0) ==
                          oldTarget,
                  "invalid staged sidecar fails closed and retains the valid primary plus restart evidence");

            std::filesystem::remove(failureSidecar);
            writeFixtureBytes(failureSidecar, stagedTarget);
            const auto retried =
                persistence.loadChunkBytesRecovering(0, 0);
            check(retried.status ==
                          Persistence::ChunkBytesRead::Status::Loaded &&
                      retried.bytes == stagedTarget &&
                      RegionFile(failureRegionPath.string()).load(0, 0) ==
                          stagedTarget &&
                      !std::filesystem::exists(failureSidecar) &&
                      !std::filesystem::exists(
                          failureStagedPath.string() + ".sidecars-pending"),
                  "retry finishes the inline-and-cleanup transaction after sidecar repair");
        } catch (const std::exception& exception) {
            check(false, (std::string("staged-sidecar failure case failed: ") +
                          exception.what()).c_str());
        }
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    check(!cleanupError,
          "valid staged-external fixture removes only its scoped temporary directory");
}

void testPostRenameDirectorySyncFailureIsExplicit() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-region-sync-uncertain-" + std::to_string(unique));
    const auto regionDirectory = directory / "region";
    std::filesystem::create_directories(regionDirectory);
    const auto primary = regionDirectory / "r.0.0.mca";
    const auto staged = std::filesystem::path(primary.string() + ".new");

    Chunk source;
    const auto sourceRoot = chunkToNBT(0, 0, source, "minecraft:plains");
    WriteBuffer encoded;
    nbt::writeFileRoot(encoded, sourceRoot);
    RegionFile(staged.string()).store(0, 0, encoded.data);

    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");
    storage_file_ops::failNextDirectorySyncForTesting();
    const auto recovered = persistence.loadChunkBytesRecovering(0, 0);
    check(recovered.status == Persistence::ChunkBytesRead::Status::Loaded &&
              recovered.bytes == encoded.data && recovered.durabilityUncertain,
          "post-rename directory-sync failure is surfaced separately from valid loaded chunk bytes");
    check(std::filesystem::exists(primary) &&
              RegionFile(primary.string()).load(0, 0) == encoded.data,
          "directory-sync uncertainty does not misclassify the promoted chunk as corrupt or absent");

    (void)persistence.stopWithStatus();
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "post-rename sync uncertainty fixture removes its exact temporary directory");
}

void testRegionStorePreservesAnUnreadableExistingHeader() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-region-store-damaged-header-" + std::to_string(unique));
    const auto path = directory / "r.0.0.mca";
    std::error_code setupError;
    std::filesystem::create_directories(directory, setupError);
    check(!setupError, "damaged region fixture creates its exact directory");
    if (setupError) return;

    const std::vector<std::uint8_t> original(4096, 0xa5);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(original.data()),
                     static_cast<std::streamsize>(original.size()));
        output.close();
        check(static_cast<bool>(output),
              "damaged region fixture writes a partial existing header");
    }

    bool rejected = false;
    try {
        RegionFile(path.string()).store(1, 0, {1, 2, 3});
    } catch (const std::exception&) {
        rejected = true;
    }
    std::vector<std::uint8_t> after;
    {
        std::ifstream input(path, std::ios::binary);
        after.assign(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
    }
    check(rejected && after == original,
          "RegionFile refuses to replace an unreadable existing header with an empty one");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "damaged region fixture removes only its scoped temporary directory");
}

void testStagedRecoveryPreservesSourcesOnNonCorruptionReadFailures() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-region-recovery-read-failure-" + std::to_string(unique));
    std::error_code setupError;
    std::filesystem::create_directories(directory, setupError);
    check(!setupError,
          "read-failure recovery fixture creates its exact temporary directory");
    if (setupError) return;

    const auto readBytes = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::vector<std::uint8_t>(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    };
    for (const auto [kind, expectedStatus] : {
             std::pair{RegionLoadFailureKind::Io,
                       StagedRegionRecovery::Status::Failed},
             std::pair{RegionLoadFailureKind::Unsupported,
                       StagedRegionRecovery::Status::Unsupported}}) {
        const auto caseDirectory = directory /
            (kind == RegionLoadFailureKind::Io ? "io" : "unsupported");
        std::filesystem::create_directories(caseDirectory, setupError);
        check(!setupError,
              "read-failure recovery fixture creates a case directory");
        if (setupError) break;
        const auto primaryPath = caseDirectory / "r.0.0.mca";
        const auto stagedPath = std::filesystem::path(
            primaryPath.string() + ".new");
        RegionFile primary(primaryPath.string());
        primary.store(0, 0, {0x11, 0x22});
        RegionFile staged(stagedPath.string());
        staged.store(0, 0, {0x33, 0x44});
        const auto originalPrimary = readBytes(primaryPath);
        const auto originalStaged = readBytes(stagedPath);
        bool injected = false;
        const auto result = StagedRegionRecovery::recoverWithLoader(
            primaryPath, stagedPath, 0, 0,
            [](const std::vector<std::uint8_t>&, std::int32_t,
               std::int32_t) {
                return StagedRegionRecovery::ChunkState::Valid;
            },
            [&](RegionFile& region, std::int32_t localX,
                std::int32_t localZ) {
                if (!injected && localX == 0 && localZ == 0) {
                    injected = true;
                    throw RegionLoadError(kind,
                        "injected non-corruption region read failure");
                }
                return region.load(localX, localZ);
            });
        std::size_t entries = 0;
        for (const auto& entry : std::filesystem::directory_iterator(
                 caseDirectory)) {
            (void)entry;
            ++entries;
        }
        check(injected && result.status == expectedStatus &&
                  !result.error.empty() &&
                  readBytes(primaryPath) == originalPrimary &&
                  readBytes(stagedPath) == originalStaged &&
                  !std::filesystem::exists(primaryPath.string() + ".corrupt") &&
                  !std::filesystem::exists(stagedPath.string() + ".corrupt") &&
                  entries == 2,
              kind == RegionLoadFailureKind::Io
                  ? "transient region I/O failure aborts recovery without changing either source"
                  : "unsupported region compression aborts recovery without changing either source");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "read-failure recovery fixture removes its exact temporary directory");
}

void testStagedRecoveryDoesNotFollowDanglingTemporarySymlinks() {
#if !defined(_WIN32)
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-region-recovery-dangling-link-" + std::to_string(unique));
    const auto primaryPath = directory / "r.0.0.mca";
    const auto stagedPath = std::filesystem::path(
        primaryPath.string() + ".new");
    const auto outsideDirectory = directory / "outside";
    const auto outsideTarget = outsideDirectory / "must-not-be-created";
    std::error_code setupError;
    std::filesystem::create_directories(outsideDirectory, setupError);
    check(!setupError,
          "dangling-link recovery fixture creates its exact temporary directory");
    if (setupError) return;

    const std::vector<std::uint8_t> damagedPrimary{0xFF};
    const std::vector<std::uint8_t> validStaged{0x42, 0x24};
    RegionFile primary(primaryPath.string());
    primary.store(0, 0, damagedPrimary);
    RegionFile staged(stagedPath.string());
    staged.store(0, 0, validStaged);
    const auto recoveryLink = std::filesystem::path(
        stagedPath.string() + ".recovering");
    const auto primaryLink = std::filesystem::path(
        primaryPath.string() + ".recovering");
    const auto corruptLink = std::filesystem::path(
        primaryPath.string() + ".corrupt");
    std::filesystem::create_symlink(outsideTarget, recoveryLink, setupError);
    const bool recoveryLinkCreated = !setupError;
    if (setupError) setupError.clear();
    std::filesystem::create_symlink(outsideTarget, primaryLink, setupError);
    const bool primaryLinkCreated = !setupError;
    if (setupError) setupError.clear();
    std::filesystem::create_symlink(outsideTarget, corruptLink, setupError);
    const bool corruptLinkCreated = !setupError;
    if (!recoveryLinkCreated || !primaryLinkCreated ||
        !corruptLinkCreated) {
        check(false,
              "dangling-link recovery fixture creates adversarial temp and quarantine symlinks");
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
        check(!cleanupError,
              "dangling-link fixture removes its exact temporary directory");
        return;
    }

    const auto recovered = StagedRegionRecovery::recover(
        primaryPath, stagedPath, 0, 0,
        [&](const std::vector<std::uint8_t>& bytes, std::int32_t,
            std::int32_t) {
            return bytes == validStaged
                ? StagedRegionRecovery::ChunkState::Valid
                : StagedRegionRecovery::ChunkState::Invalid;
        });
    const auto recoveryLinkStatus = std::filesystem::symlink_status(
        recoveryLink, setupError);
    const bool recoveryLinkRetained = !setupError &&
        std::filesystem::is_symlink(recoveryLinkStatus);
    setupError.clear();
    const auto primaryLinkStatus = std::filesystem::symlink_status(
        primaryLink, setupError);
    const bool primaryLinkRetained = !setupError &&
        std::filesystem::is_symlink(primaryLinkStatus);
    setupError.clear();
    const auto corruptLinkStatus = std::filesystem::symlink_status(
        corruptLink, setupError);
    const bool corruptLinkRetained = !setupError &&
        std::filesystem::is_symlink(corruptLinkStatus);
    bool repairedContentsMatch = false;
    try {
        repairedContentsMatch = RegionFile(primaryPath.string()).load(0, 0) ==
                                    validStaged &&
                                RegionFile(stagedPath.string()).load(0, 0) ==
                                    validStaged;
    } catch (const std::exception&) {
    }
    bool corruptBackupHasOriginal = false;
    try {
        corruptBackupHasOriginal =
            RegionFile(corruptLink.string() + ".1").load(0, 0) ==
            damagedPrimary;
    } catch (const std::exception&) {
    }
    check(recovered.status == StagedRegionRecovery::Status::Recovered &&
              recoveryLinkRetained && primaryLinkRetained &&
              corruptLinkRetained && corruptBackupHasOriginal &&
              !std::filesystem::exists(outsideTarget) &&
              repairedContentsMatch &&
              !std::filesystem::exists(recoveryLink.string() + ".1") &&
              !std::filesystem::exists(primaryLink.string() + ".1"),
          "staged and repaired-primary recovery skips dangling symlinks and never creates their outside targets");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "dangling-link recovery fixture removes its exact temporary directory");
#endif
}

void testPlayerQuarantineSkipsDanglingCorruptSymlink() {
#if !defined(_WIN32)
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-player-quarantine-dangling-link-" +
         std::to_string(unique));
    const auto playerPath = directory / "playerdata" / "player.dat";
    const auto outsideDirectory = directory / "outside";
    const auto outsideTarget = outsideDirectory / "must-not-be-created";
    std::error_code setupError;
    std::filesystem::create_directories(playerPath.parent_path(), setupError);
    std::filesystem::create_directories(outsideDirectory, setupError);
    check(!setupError,
          "player quarantine fixture creates its scoped directories");
    if (setupError) return;

    {
        std::ofstream corrupt(playerPath, std::ios::binary | std::ios::trunc);
        corrupt.put('\x7f');
        check(static_cast<bool>(corrupt),
              "player quarantine fixture writes malformed player bytes");
    }
    const auto corruptLink =
        std::filesystem::path(playerPath.string() + ".corrupt");
    std::filesystem::create_symlink(outsideTarget, corruptLink, setupError);
    check(!setupError,
          "player quarantine fixture creates a dangling corruption symlink");
    if (setupError) {
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
        check(!cleanupError,
              "player quarantine fixture cleans up after setup failure");
        return;
    }

    std::string quarantined;
    const bool loaded = loadPlayerDataIsolated(
        playerPath.string(), [](const std::string&) { return false; },
        &quarantined);
    std::error_code statusError;
    const auto linkStatus = std::filesystem::symlink_status(
        corruptLink, statusError);
    const bool linkRetained = !statusError &&
        std::filesystem::is_symlink(linkStatus);
    check(!loaded && linkRetained && !std::filesystem::exists(outsideTarget) &&
              !std::filesystem::exists(playerPath) &&
              quarantined == playerPath.string() + ".corrupt.1" &&
              std::filesystem::exists(quarantined),
          "player quarantine copies to an exclusive numbered sibling without following a dangling .corrupt symlink");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "player quarantine fixture removes its exact temporary directory");
#endif
}

void testRegionSaveRejectsDanglingStageSymlink() {
#if !defined(_WIN32)
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-region-stage-dangling-link-" + std::to_string(unique));
    const auto regionDirectory = directory / "region";
    const auto regionPath = regionDirectory / "r.0.0.mca";
    const auto outsideDirectory = directory / "outside";
    const auto outsideTarget = outsideDirectory / "must-not-be-created";
    std::error_code setupError;
    std::filesystem::create_directories(regionDirectory, setupError);
    std::filesystem::create_directories(outsideDirectory, setupError);
    check(!setupError,
          "region-stage symlink fixture creates its scoped directories");
    if (setupError) return;

    const auto stageLink =
        std::filesystem::path(regionPath.string() + ".new");
    std::filesystem::create_symlink(outsideTarget, stageLink, setupError);
    check(!setupError,
          "region-stage symlink fixture creates a dangling .new link");
    if (setupError) {
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
        check(!cleanupError,
              "region-stage fixture cleans up after setup failure");
        return;
    }

    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");
    bool rejected = false;
    try {
        persistence.storeChunkBytes(0, 0, {0x01, 0x02, 0x03});
    } catch (const std::exception&) {
        rejected = true;
    }
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(stageLink, statusError);
    check(rejected && !statusError && std::filesystem::is_symlink(status) &&
              !std::filesystem::exists(outsideTarget) &&
              !std::filesystem::exists(regionPath),
          "region save refuses a dangling staged destination without following it or changing the primary");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "region-stage symlink fixture removes its exact temporary directory");
#endif
}

void testWorldDataSaveSkipsDanglingTemporarySymlinks() {
#if !defined(_WIN32)
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-world-data-dangling-temp-link-" +
         std::to_string(unique));
    const auto outsideDirectory = directory / "outside";
    const auto temporaryTarget = outsideDirectory / "tmp-target";
    const auto backupTarget = outsideDirectory / "backup-target";
    std::error_code setupError;
    std::filesystem::create_directories(outsideDirectory, setupError);
    check(!setupError,
          "world-data symlink fixture creates its exact temporary directory");
    if (setupError) return;

    const auto tmpLink = directory / "level.dat.new.tmp";
    std::filesystem::create_symlink(temporaryTarget, tmpLink, setupError);
    check(!setupError,
          "world-data fixture creates a dangling predictable temp symlink");
    if (setupError) {
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
        check(!cleanupError,
              "world-data fixture cleans up after temp-link setup failure");
        return;
    }

    WorldDataManager manager(directory.string());
    auto levelRoot = nbt::Value::makeCompound();
    levelRoot.set("Data", nbt::Value::makeCompound());
    const bool firstSave = manager.saveLevelData(levelRoot);
    const auto backupLink = directory / "level.dat_old.new";
    std::filesystem::create_symlink(backupTarget, backupLink, setupError);
    const bool backupLinkCreated = !setupError;
    const bool secondSave = backupLinkCreated &&
                            manager.saveLevelData(levelRoot);
    std::error_code tempStatusError;
    const auto tempStatus = std::filesystem::symlink_status(
        tmpLink, tempStatusError);
    std::error_code backupStatusError;
    const auto backupStatus = std::filesystem::symlink_status(
        backupLink, backupStatusError);
    check(firstSave && secondSave && !tempStatusError &&
              std::filesystem::is_symlink(tempStatus) &&
              !backupStatusError && std::filesystem::is_symlink(backupStatus) &&
              !std::filesystem::exists(temporaryTarget) &&
              !std::filesystem::exists(backupTarget) &&
              std::filesystem::exists(directory / "level.dat") &&
              std::filesystem::exists(directory / "level.dat_old"),
          "level.dat and level.dat_old saves use owned exclusive temp siblings rather than following dangling symlinks");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "world-data symlink fixture removes its exact temporary directory");
#endif
}

void testSyncAndAsyncStagedRecoveryParity() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-sync-async-recovery-parity-" + std::to_string(unique));
    const auto asyncDirectory = directory / "async";
    const auto syncDirectory = directory / "sync";

    const auto& blockStates = gen::blockNameToState();
    const auto stone = static_cast<std::uint16_t>(
        blockStates.at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        blockStates.at("minecraft:dirt"));
    const auto gold = static_cast<std::uint16_t>(
        blockStates.at("minecraft:gold_block"));
    const auto iron = static_cast<std::uint16_t>(
        blockStates.at("minecraft:iron_block"));
    const auto encodeChunk = [](std::int32_t cx, std::int32_t cz,
                               std::uint16_t blockState) {
        Chunk chunk;
        ChunkBuildWriter writer(chunk);
        writer.blocks.set(Chunk::index(10, 4, 4, 4), blockState);
        const auto root = chunkToNBT(cx, cz, chunk, "minecraft:plains");
        WriteBuffer bytes;
        nbt::writeFileRoot(bytes, root);
        return bytes.data;
    };
    const auto oldTarget = encodeChunk(0, 0, stone);
    const auto selectedTarget = encodeChunk(0, 0, dirt);
    const auto primarySibling = encodeChunk(1, 0, gold);
    const auto stagedSibling = encodeChunk(2, 0, iron);

    std::error_code setupError;
    std::filesystem::create_directories(asyncDirectory / "region",
                                        setupError);
    check(!setupError,
          "async parity fixture creates its scoped region directory");
    if (setupError) {
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
        check(!cleanupError,
              "sync/async recovery parity fixture cleans up after setup failure");
        return;
    }
    setupError.clear();
    std::filesystem::create_directories(syncDirectory / "region",
                                        setupError);
    check(!setupError,
          "sync parity fixture creates its scoped region directory");
    if (setupError) {
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
        check(!cleanupError,
              "sync/async recovery parity fixture cleans up after setup failure");
        return;
    }

    const auto installFixture = [&](const std::filesystem::path& worldDir) {
        const auto primaryPath = worldDir / "region" / "r.0.0.mca";
        RegionFile primary(primaryPath.string());
        primary.store(0, 0, oldTarget);
        primary.store(1, 0, primarySibling);
        RegionFile staged(primaryPath.string() + ".new");
        staged.store(0, 0, selectedTarget);
        staged.store(2, 0, stagedSibling);
    };
    installFixture(asyncDirectory);
    installFixture(syncDirectory);

    Persistence::ChunkBytesRead asyncRead;
    Chunk syncChunk;
    ChunkLoadStatus syncStatus = ChunkLoadStatus::Failed;
    const auto targetIndex = Chunk::index(10, 4, 4, 4);
    bool syncDecodedSelectedTarget = false;
    {
        World asyncWorld("minecraft:plains", LevelType::Flat, 0);
        Persistence asyncPersistence(asyncWorld, asyncDirectory.string(),
                                     "minecraft:plains");
        asyncRead = asyncPersistence.loadChunkBytesRecovering(0, 0);

        World syncWorld("minecraft:plains", LevelType::Flat, 0);
        Persistence syncPersistence(syncWorld, syncDirectory.string(),
                                    "minecraft:plains");
        ChunkBuildWriter syncWriter(syncChunk);
        syncStatus = syncPersistence.loadChunkStatus(0, 0, syncWriter);
        syncDecodedSelectedTarget = syncWriter.blocks.get(targetIndex) == dirt;
    }

    check(asyncRead.status == Persistence::ChunkBytesRead::Status::Loaded &&
              asyncRead.bytes == selectedTarget &&
              syncStatus == ChunkLoadStatus::Loaded &&
              syncDecodedSelectedTarget,
          "sync decode and async raw-byte loading select the same staged chunk");

    const auto asyncPrimaryPath = asyncDirectory / "region" / "r.0.0.mca";
    const auto syncPrimaryPath = syncDirectory / "region" / "r.0.0.mca";
    RegionFile asyncPromoted(asyncPrimaryPath.string());
    RegionFile syncPromoted(syncPrimaryPath.string());
    const auto promotedSnapshotMatches = [&](RegionFile& region) {
        return region.load(0, 0) == selectedTarget &&
               region.load(1, 0) == primarySibling &&
               region.load(2, 0) == stagedSibling;
    };
    check(promotedSnapshotMatches(asyncPromoted) &&
              promotedSnapshotMatches(syncPromoted) &&
              !std::filesystem::exists(asyncPrimaryPath.string() + ".new") &&
              !std::filesystem::exists(syncPrimaryPath.string() + ".new"),
          "sync and async recovery promote equivalent merged regions and preserve both sibling sets");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "sync/async recovery parity fixture removes its exact temporary directory");
}

void testDirtyGenerationReplacementAndRetry() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-dirty-generation-retry-" + std::to_string(unique));
    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");

    std::mutex barrierMutex;
    std::condition_variable barrierCv;
    bool firstFlushEntered = false;
    bool releaseFirstFlush = false;
    bool saveCompleted = false;
    persistence.setChunkSaveBarrier(
        [&](std::int32_t, std::int32_t) {
            std::unique_lock lock(barrierMutex);
            if (firstFlushEntered) return;
            firstFlushEntered = true;
            barrierCv.notify_all();
            barrierCv.wait(lock, [&] { return releaseFirstFlush; });
        });
    persistence.setChunkSaveComplete(
        [&](std::int32_t, std::int32_t) {
            std::lock_guard lock(barrierMutex);
            saveCompleted = true;
            barrierCv.notify_all();
        });

    persistence.start();
    persistence.markDirty(0, 0);
    bool entered = false;
    {
        std::unique_lock lock(barrierMutex);
        entered = barrierCv.wait_for(lock, std::chrono::seconds(5), [&] {
            return firstFlushEntered;
        });
    }
    check(entered,
          "dirty-generation retry fixture pauses the first asynchronous flush");

    std::uint64_t firstVersion = 0;
    std::uint64_t currentVersion = 0;
    if (entered) {
        firstVersion = persistence.dirtyVersion(0, 0);
        persistence.markDirty(0, 0);
        currentVersion = persistence.dirtyVersion(0, 0);
        check(firstVersion != 0 && currentVersion != 0 &&
                  currentVersion != firstVersion,
              "a repeated dirty mark atomically replaces the chunk generation with a new nonzero version");
        check(!persistence.markCleanIfVersion(0, 0, firstVersion) &&
                  persistence.isDirty(0, 0) &&
                  persistence.dirtyVersion(0, 0) == currentVersion,
              "completion of an older snapshot cannot clear the newer dirty generation");
    }

    {
        std::lock_guard lock(barrierMutex);
        releaseFirstFlush = true;
    }
    barrierCv.notify_all();

    bool completed = false;
    if (entered) {
        std::unique_lock lock(barrierMutex);
        completed = barrierCv.wait_for(lock, std::chrono::seconds(20), [&] {
            return saveCompleted;
        });
    }
    check(completed,
          "the asynchronous retry persists and completes the current generation");
    if (completed) {
        check(!persistence.isDirty(0, 0) &&
                  persistence.dirtyVersion(0, 0) == 0,
              "successful retry removes the dirty record and its generation together");
    }

    stopPersistenceAndWait(persistence);
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "dirty-generation retry fixture removes its exact temporary directory");
}

void testPendingStructureLootDefersChunkPersistence() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-pending-structure-save-admission-" +
         std::to_string(unique));
    World world("minecraft:plains", LevelType::Flat, 0);
    const bool generated = world.generateChunkIfMissing(0, 0);
    check(generated,
          "pending-structure-save fixture generates its source chunk");
    auto* structures = world.structureManager();
    check(structures != nullptr,
          "pending-structure-save fixture has a StructureManager");
    if (!generated || !structures) return;

    structures->requeuePendingLoot(
        worldgen::StructureManager::PendingLoot{
            {8, 64, 8}, "minecraft:chests/simple_dungeon", 0},
        0);
    Persistence persistence(world, directory.string(), "minecraft:plains");
    const auto manualSaver = std::this_thread::get_id();
    std::mutex admissionMutex;
    std::condition_variable admissionCv;
    unsigned int denialCount = 0;
    persistence.setChunkSaveAdmission(
        [&, manualSaver](std::int32_t cx, std::int32_t cz) {
            const bool admitted = std::this_thread::get_id() == manualSaver &&
                !structures->hasPendingLootInChunk(cx, cz);
            if (!admitted) {
                std::lock_guard lock(admissionMutex);
                ++denialCount;
                admissionCv.notify_all();
            }
            return admitted;
        });
    persistence.start();
    persistence.markDirty(0, 0);
    const bool directFlushDenied = !persistence.flushChunk(0, 0) &&
        persistence.isDirty(0, 0) &&
        !std::filesystem::exists(directory / "region" / "r.0.0.mca");
    bool bothPathsDenied = false;
    {
        std::unique_lock lock(admissionMutex);
        bothPathsDenied = admissionCv.wait_for(
            lock, std::chrono::seconds(5), [&] { return denialCount >= 2; });
    }
    const auto regionPath = directory / "region" / "r.0.0.mca";
    check(directFlushDenied && bothPathsDenied &&
              persistence.isDirty(0, 0) &&
              !std::filesystem::exists(regionPath),
          "flushOnce and flushChunk both defer a chunk with pending structure loot and retain its dirty generation");

    std::vector<worldgen::StructureManager::PendingLoot> drained;
    structures->drainPendingLoot(drained);
    const bool saved = persistence.flushChunk(0, 0);
    bool savedBytesReadable = false;
    try {
        savedBytesReadable =
            !RegionFile(regionPath.string()).load(0, 0).empty();
    } catch (const std::exception&) {
    }
    check(drained.size() == 1 && saved && savedBytesReadable &&
              !persistence.isDirty(0, 0),
          "flushChunk admits and persists the retained chunk after StructureManager drains its pending loot");
    stopPersistenceAndWait(persistence);

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "pending-structure-save fixture removes its exact temporary directory");
}

void testPendingLootSurvivesChunkUnloadAndIsReloadedAtShutdown() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-pending-loot-unloaded-chunk-" + std::to_string(unique));
    const auto regionPath = directory / "region" / "r.0.0.mca";
    World world("minecraft:plains", LevelType::Flat, 0);
    auto* structures = world.structureManager();
    const auto chestState = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:chest"));
    const bool generated = world.generateChunkIfMissing(0, 0);
    const bool chestPlaced = generated && world.setBlock(2, 64, 3, chestState);
    Persistence persistence(world, directory.string(), "minecraft:plains");
    const auto manualSaver = std::this_thread::get_id();
    persistence.setChunkSaveAdmission(
        [manualSaver](std::int32_t, std::int32_t) {
            return std::this_thread::get_id() == manualSaver;
        });
    persistence.start();
    persistence.markDirty(0, 0);
    const bool originalSaved = chestPlaced && persistence.flushChunk(0, 0);
    const bool unloaded = originalSaved && world.eraseChunk(0, 0);
    check(structures && originalSaved && unloaded,
          "pending-loot unload fixture saves and unloads its chest chunk");
    if (!structures || !unloaded) {
        (void)stopPersistenceAndWait(persistence);
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
        return;
    }

    structures->requeuePendingLoot(
        worldgen::StructureManager::PendingLoot{
            {2, 64, 3}, "minecraft:chests/simple_dungeon", 0},
        0);
    BlockEntityStore blockEntities;
    persistence.setChunkExtras(
        [&blockEntities](std::int32_t cx, std::int32_t cz,
                         nbt::Value& root) {
            auto entities = nbt::Value::makeList(nbt::Compound);
            blockEntities.writeChunkNbt(cx, cz, entities);
            if (!entities.list.empty())
                root.set("block_entities", std::move(entities));
        }, {});
    check(blockEntities.bindMutationGate(world.mutationGateIdentity()),
          "pending-loot unload fixture binds Block Entity mutations to World");
    blockEntities.setDirtyCallback(
        [&](std::int32_t cx, std::int32_t cz) {
            persistence.markDirty(cx, cz);
        });

    const auto deferred = drainPendingStructureLoot(
        world, blockEntities, *structures, 1);
    const bool stayedQueued = deferred.deferred == 1 &&
        !world.hasChunk(0, 0) && structures->hasPendingLootInChunk(0, 0);
    const bool loadedForShutdown =
        loadChunksForPendingStructureLoot(world, *structures);
    const auto transferred = drainPendingStructureLoot(
        world, blockEntities, *structures,
        std::numeric_limits<std::int64_t>::max());
    const bool savedLoot = persistence.flushChunk(0, 0);

    bool lootInRegion = false;
    try {
        const auto bytes = RegionFile(regionPath.string()).load(0, 0);
        if (!bytes.empty()) {
            ReadBuffer input(bytes);
            const auto chunk = nbt::Parser(input).readFileRoot();
            const auto* entities = chunk.get("block_entities");
            if (entities && entities->tag == nbt::List) {
                for (const auto& entity : entities->list) {
                    const auto* lootTable = entity.get("LootTable");
                    if (lootTable && lootTable->tag == nbt::String &&
                        lootTable->str ==
                            "minecraft:chests/simple_dungeon") {
                        lootInRegion = true;
                        break;
                    }
                }
            }
        }
    } catch (const std::exception&) {
    }
    const bool stoppedCleanly = stopPersistenceAndWait(persistence);
    check(stayedQueued && loadedForShutdown &&
              world.getBlock(2, 64, 3) == chestState &&
              transferred.persisted == 1 && transferred.deferred == 0 &&
              !structures->hasPendingLootInChunk(0, 0) && savedLoot &&
              lootInRegion && stoppedCleanly,
          "an unloaded chest is not mistaken for air and shutdown reloads it before persisting pending loot");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "pending-loot unload fixture removes its exact temporary directory");
}

void testPersistenceStopReportsUnflushedChunks() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-persistence-stop-status-" + std::to_string(unique));
    World world("minecraft:plains", LevelType::Flat, 0);
    check(world.generateChunkIfMissing(0, 0),
          "persistence stop-status fixture generates a chunk");
    Persistence persistence(world, directory.string(), "minecraft:plains");
    persistence.setChunkSaveAdmission(
        [](std::int32_t, std::int32_t) { return false; });
    persistence.start();
    persistence.markDirty(0, 0);
    const bool stopReportedFailure = !stopPersistenceAndWait(persistence);
    check(stopReportedFailure && persistence.isDirty(0, 0) &&
              !std::filesystem::exists(directory / "region" / "r.0.0.mca"),
          "Persistence::stop reports failure when retry exhaustion leaves a dirty chunk unsaved");
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "persistence stop-status fixture removes its exact temporary directory");
}

void testPersistenceStopWaitsForAcceptedDirtyRegistration() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-persistence-stop-dirty-race-" + std::to_string(unique));
    const auto regionPath = directory / "region" / "r.0.0.mca";
    World world("minecraft:plains", LevelType::Flat, 0);
    const bool generated = world.generateChunkIfMissing(0, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");

    std::mutex mutex;
    std::condition_variable changed;
    bool dirtyCommitEntered = false;
    bool releaseDirtyCommit = false;
    bool stopResult = false;
    std::atomic<bool> stopReturned{false};
    WorldMutationContractTestAccess::setBeforeDirtyCommit(
        persistence, [&] {
            std::unique_lock lock(mutex);
            dirtyCommitEntered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return releaseDirtyCommit; });
        });
    persistence.start();

    std::thread marker([&] { persistence.markDirty(0, 0); });
    bool markerPausedInsideAdmission = false;
    {
        std::unique_lock lock(mutex);
        markerPausedInsideAdmission = changed.wait_for(
            lock, std::chrono::seconds(5), [&] { return dirtyCommitEntered; });
    }
    std::thread stopper([&] {
        stopResult = persistence.stop();
        stopReturned.store(true, std::memory_order_release);
        changed.notify_all();
    });

    const auto waiterDeadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(5);
    while (markerPausedInsideAdmission &&
           std::chrono::steady_clock::now() < waiterDeadline &&
           WorldMutationContractTestAccess::dirtyAdmissionWaiters(
               persistence) == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool stopWaitedForRegistration = markerPausedInsideAdmission &&
        WorldMutationContractTestAccess::dirtyAdmissionWaiters(persistence) > 0 &&
        !stopReturned.load(std::memory_order_acquire);
    {
        std::lock_guard lock(mutex);
        releaseDirtyCommit = true;
    }
    changed.notify_all();
    marker.join();
    stopper.join();

    bool savedBytesReadable = false;
    try {
        savedBytesReadable =
            !RegionFile(regionPath.string()).load(0, 0).empty();
    } catch (const std::exception&) {
    }
    check(generated && stopWaitedForRegistration &&
              stopReturned.load(std::memory_order_acquire) && stopResult &&
              savedBytesReadable && !persistence.isDirty(0, 0),
          "stop waits for a previously admitted markDirty and flushes that generation before reporting success");
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "dirty-registration shutdown race fixture removes its exact temporary directory");
}

void testPersistenceStopFromSaveCallbackCannotDeadlockWorkerJoin() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-stop-from-save-callback-" + std::to_string(unique));
    const auto regionPath = directory / "region" / "r.0.0.mca";
    World world("minecraft:plains", LevelType::Flat, 0);
    const bool generated = world.generateChunkIfMissing(0, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");

    std::mutex mutex;
    std::condition_variable changed;
    bool barrierEntered = false;
    bool releaseBarrier = false;
    bool nestedLifecycleCallsFinished = false;
    bool stopAdmissionChecked = false;
    std::atomic<bool> nestedStopReturnedFalse{false};
    std::atomic<bool> nestedStopAttempted{false};
    std::atomic<bool> nestedStartReturned{false};
    std::atomic<bool> helperStopReturnedFalse{false};
    std::atomic<bool> nestedFlushReturnedFalse{false};
    WorldMutationContractTestAccess::setBeforeStopAdmissionCheck(
        persistence, [&] {
            std::lock_guard lock(mutex);
            stopAdmissionChecked = true;
            changed.notify_all();
        });
    persistence.setChunkSaveBarrier(
        [&](std::int32_t, std::int32_t) {
            std::unique_lock lock(mutex);
            barrierEntered = true;
            changed.notify_all();
            changed.wait(lock, [&] { return releaseBarrier; });
            lock.unlock();
            if (!nestedStopAttempted.exchange(true,
                                               std::memory_order_acq_rel)) {
                nestedStopReturnedFalse.store(!persistence.stop(),
                                              std::memory_order_release);
                nestedFlushReturnedFalse.store(
                    !persistence.flushChunk(0, 0),
                    std::memory_order_release);
                std::thread helper([&] {
                    helperStopReturnedFalse.store(
                        !persistence.stop(), std::memory_order_release);
                });
                helper.join();
                persistence.start();
                nestedStartReturned.store(true, std::memory_order_release);
                std::lock_guard finishedLock(mutex);
                nestedLifecycleCallsFinished = true;
                changed.notify_all();
            }
        });
    persistence.start();
    persistence.markDirty(0, 0);

    bool reachedBarrier = false;
    {
        std::unique_lock lock(mutex);
        reachedBarrier = changed.wait_for(
            lock, std::chrono::seconds(5), [&] { return barrierEntered; });
    }

    std::atomic<bool> outerStopReturned{false};
    std::atomic<bool> outerStopSucceeded{false};
    std::thread stopper;
    if (reachedBarrier) {
        stopper = std::thread([&] {
            outerStopSucceeded.store(persistence.stop(),
                                     std::memory_order_release);
            outerStopReturned.store(true, std::memory_order_release);
            changed.notify_all();
        });
    }
    bool outerStopWasDeferredDuringCallback = false;
    if (reachedBarrier) {
        std::unique_lock lock(mutex);
        const bool stopReachedAdmissionCheck = changed.wait_for(
            lock, std::chrono::seconds(5), [&] {
                return stopAdmissionChecked;
            });
        outerStopWasDeferredDuringCallback = changed.wait_for(
            lock, std::chrono::seconds(5), [&] {
                return outerStopReturned.load(std::memory_order_acquire);
            }) && stopReachedAdmissionCheck &&
            !outerStopSucceeded.load(std::memory_order_acquire);
    }
    {
        std::lock_guard lock(mutex);
        releaseBarrier = true;
    }
    changed.notify_all();
    if (stopper.joinable()) stopper.join();
    bool lifecycleCallsFinished = false;
    {
        std::unique_lock lock(mutex);
        lifecycleCallsFinished = changed.wait_for(
            lock, std::chrono::seconds(5), [&] {
                return nestedLifecycleCallsFinished;
            });
    }
    const bool outerRetrySucceeded = stopPersistenceAndWait(persistence);
    persistence.setChunkSaveBarrier({});
    WorldMutationContractTestAccess::setBeforeStopAdmissionCheck(
        persistence, {});

    bool savedChunkReadable = false;
    try {
        savedChunkReadable =
            !RegionFile(regionPath.string()).load(0, 0).empty();
    } catch (const std::exception&) {
    }
    check(generated && outerStopWasDeferredDuringCallback &&
              nestedStopAttempted.load(std::memory_order_acquire) &&
              nestedStopReturnedFalse.load(std::memory_order_acquire) &&
              nestedFlushReturnedFalse.load(std::memory_order_acquire) &&
              helperStopReturnedFalse.load(std::memory_order_acquire) &&
              nestedStartReturned.load(std::memory_order_acquire) &&
              lifecycleCallsFinished &&
              outerStopReturned.load(std::memory_order_acquire) &&
              !outerStopSucceeded.load(std::memory_order_acquire) &&
              outerRetrySucceeded &&
              savedChunkReadable,
          "save callbacks and concurrent callers defer lifecycle transitions until the callback is quiescent, then shutdown saves dirty chunks");
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "save-callback stop fixture removes its exact temporary directory");
}

void testPersistenceStopWaitsForSynchronousFlushCall() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-stop-waits-for-sync-flush-" + std::to_string(unique));
    const auto regionPath = directory / "region" / "r.0.0.mca";
    World world("minecraft:plains", LevelType::Flat, 0);
    const bool generated = world.generateChunkIfMissing(0, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");

    std::mutex mutex;
    std::condition_variable changed;
    std::thread::id saverId;
    bool flushCallReleaseReached = false;
    bool releaseFlushCall = false;
    persistence.setChunkSaveAdmission(
        [&](std::int32_t, std::int32_t) {
            std::lock_guard lock(mutex);
            return std::this_thread::get_id() == saverId;
        });
    WorldMutationContractTestAccess::setBeforeFlushCallLeave(
        persistence, [&] {
            std::unique_lock lock(mutex);
            flushCallReleaseReached = true;
            changed.notify_all();
            changed.wait(lock, [&] { return releaseFlushCall; });
        });
    persistence.start();
    persistence.markDirty(0, 0);

    std::atomic<bool> flushReturned{false};
    bool flushSucceeded = false;
    std::thread saver([&] {
        {
            std::lock_guard lock(mutex);
            saverId = std::this_thread::get_id();
        }
        flushSucceeded = persistence.flushChunk(0, 0);
        flushReturned.store(true, std::memory_order_release);
        changed.notify_all();
    });
    bool heldAfterSaveCallbacks = false;
    {
        std::unique_lock lock(mutex);
        heldAfterSaveCallbacks = changed.wait_for(
            lock, std::chrono::seconds(5),
            [&] { return flushCallReleaseReached; });
    }

    std::atomic<bool> stopReturned{false};
    std::atomic<bool> stopSucceeded{false};
    std::thread stopper;
    if (heldAfterSaveCallbacks) {
        stopper = std::thread([&] {
            stopSucceeded.store(stopPersistenceAndWait(persistence),
                                std::memory_order_release);
            stopReturned.store(true, std::memory_order_release);
            changed.notify_all();
        });
    }
    const auto stopDeadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(5);
    while (heldAfterSaveCallbacks &&
           std::chrono::steady_clock::now() < stopDeadline &&
           (!WorldMutationContractTestAccess::stopInProgress(persistence) ||
            stopReturned.load(std::memory_order_acquire)))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool stopWaitedForFlushLease = heldAfterSaveCallbacks &&
        WorldMutationContractTestAccess::stopInProgress(persistence) &&
        !stopReturned.load(std::memory_order_acquire) &&
        !flushReturned.load(std::memory_order_acquire);
    {
        std::lock_guard lock(mutex);
        releaseFlushCall = true;
    }
    changed.notify_all();
    if (saver.joinable()) saver.join();
    if (stopper.joinable()) stopper.join();
    persistence.setChunkSaveAdmission({});
    const bool cleanupStopSucceeded = stopPersistenceAndWait(persistence);
    WorldMutationContractTestAccess::setBeforeFlushCallLeave(
        persistence, {});

    bool savedChunkReadable = false;
    try {
        savedChunkReadable =
            !RegionFile(regionPath.string()).load(0, 0).empty();
    } catch (const std::exception&) {
    }
    check(generated && heldAfterSaveCallbacks && stopWaitedForFlushLease &&
              flushReturned.load(std::memory_order_acquire) && flushSucceeded &&
              stopReturned.load(std::memory_order_acquire) &&
              stopSucceeded.load(std::memory_order_acquire) &&
              cleanupStopSucceeded &&
              savedChunkReadable,
          "shutdown closes synchronous flush admission and waits for an admitted flush call after its callbacks finish");
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "synchronous flush shutdown fixture removes its exact temporary directory");
}

void testPersistenceStopWaitsForPublicRawChunkStoreCall() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-stop-waits-for-raw-chunk-store-" + std::to_string(unique));
    const auto regionPath = directory / "region" / "r.0.0.mca";
    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");

    std::mutex mutex;
    std::condition_variable changed;
    bool storeLeaseHeld = false;
    bool releaseStoreLease = false;
    WorldMutationContractTestAccess::setBeforeFlushCallLeave(
        persistence, [&] {
            std::unique_lock lock(mutex);
            storeLeaseHeld = true;
            changed.notify_all();
            changed.wait(lock, [&] { return releaseStoreLease; });
        });

    // The raw-store API is test-only and accepts the RegionFile payload as-is;
    // this fixture only needs a stable payload to prove the write call remains
    // admitted until its flush-call lease is released.
    const std::vector<std::uint8_t> bytes{0x01, 0x02, 0x03};
    std::atomic<bool> storeReturned{false};
    std::atomic<bool> storeSucceeded{false};
    std::thread store([&] {
        storeSucceeded.store(persistence.storeChunkBytes(0, 0, bytes),
                             std::memory_order_release);
        storeReturned.store(true, std::memory_order_release);
        changed.notify_all();
    });
    bool reachedStoreLease = false;
    {
        std::unique_lock lock(mutex);
        reachedStoreLease = changed.wait_for(
            lock, std::chrono::seconds(5), [&] { return storeLeaseHeld; });
    }

    std::atomic<bool> stopReturned{false};
    std::atomic<bool> stopSucceeded{false};
    std::thread stopper;
    if (reachedStoreLease) {
        stopper = std::thread([&] {
            stopSucceeded.store(stopPersistenceAndWait(persistence),
                                std::memory_order_release);
            stopReturned.store(true, std::memory_order_release);
        });
    }
    const auto stopDeadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(5);
    while (reachedStoreLease &&
           std::chrono::steady_clock::now() < stopDeadline &&
           (!WorldMutationContractTestAccess::stopInProgress(persistence) ||
            stopReturned.load(std::memory_order_acquire)))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool stopWaitedForStoreLease = reachedStoreLease &&
        WorldMutationContractTestAccess::stopInProgress(persistence) &&
        !stopReturned.load(std::memory_order_acquire) &&
        !storeReturned.load(std::memory_order_acquire);
    {
        std::lock_guard lock(mutex);
        releaseStoreLease = true;
    }
    changed.notify_all();
    if (store.joinable()) store.join();
    if (stopper.joinable()) stopper.join();
    const bool cleanupStopSucceeded = stopPersistenceAndWait(persistence);
    WorldMutationContractTestAccess::setBeforeFlushCallLeave(
        persistence, {});

    bool storedBytesReadable = false;
    try {
        storedBytesReadable =
            RegionFile(regionPath.string()).load(0, 0) == bytes;
    } catch (const std::exception&) {
    }
    check(reachedStoreLease && stopWaitedForStoreLease &&
              storeReturned.load(std::memory_order_acquire) &&
              storeSucceeded.load(std::memory_order_acquire) &&
              stopReturned.load(std::memory_order_acquire) &&
              stopSucceeded.load(std::memory_order_acquire) &&
              cleanupStopSucceeded && storedBytesReadable,
          "shutdown waits for an admitted public raw chunk store before finalizing persistence");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "raw chunk store shutdown fixture removes its exact temporary directory");
}

void testLevelDataOwnershipIsExplicitRatherThanPathInferred() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-level-data-storage-role-" + std::to_string(unique));
    const auto rootNamedLikeDimension = directory / "DIM1";
    const auto dimensionDirectory = rootNamedLikeDimension / "DIM-1";
    World rootWorld("minecraft:plains", LevelType::Flat, 0);
    Persistence worldRoot(rootWorld, rootNamedLikeDimension.string(),
                          "minecraft:plains");
    worldRoot.saveLevelData();
    const bool namedRootSaved =
        std::filesystem::exists(rootNamedLikeDimension / "level.dat");

    World dimensionWorld("minecraft:plains", LevelType::Nether, 0);
    Persistence dimension(dimensionWorld, dimensionDirectory.string(), "",
                          Persistence::StorageRole::Dimension);
    dimension.saveLevelData();
    const bool dimensionSkippedLevelData =
        !std::filesystem::exists(dimensionDirectory / "level.dat");

    check(namedRootSaved && dimensionSkippedLevelData,
          "level.dat ownership follows explicit storage role, not DIM-like directory names");
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "level-data storage-role fixture removes its exact temporary directory");
}

void testSynchronousSaveBarrierFailurePreservesDirtyState() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-sync-save-barrier-failure-" + std::to_string(unique));
    World world("minecraft:plains", LevelType::Flat, 0);
    const bool generated = world.generateChunkIfMissing(0, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");
    persistence.setChunkSaveBarrier(
        [](std::int32_t, std::int32_t) {
            throw std::runtime_error("injected save barrier failure");
        });
    persistence.start();
    persistence.markDirty(0, 0);
    const bool failedAsResult = generated &&
        !persistence.flushChunk(0, 0) && persistence.isDirty(0, 0) &&
        !std::filesystem::exists(directory / "region" / "r.0.0.mca");
    persistence.setChunkSaveBarrier({});
    const bool retrySucceeded = persistence.flushChunk(0, 0);
    const bool stoppedCleanly = stopPersistenceAndWait(persistence);
    check(failedAsResult && retrySucceeded && stoppedCleanly &&
              !persistence.isDirty(0, 0),
          "a throwing synchronous save barrier returns failure, retains dirtiness, and permits a successful retry");
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "save-barrier failure fixture removes its exact temporary directory");
}

void testSaveAdmissionIsRecheckedAfterChunkGeneration() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-generated-side-effect-save-admission-" +
         std::to_string(unique));
    const auto regionPath = directory / "region" / "r.0.0.mca";
    std::atomic<bool> generatedSideEffectPending{false};
    World world("minecraft:plains", LevelType::Flat, 0);
    world.setGenerator(
        std::make_unique<SideEffectPublishingGenerator>(
            generatedSideEffectPending));
    Persistence persistence(world, directory.string(), "minecraft:plains");
    std::mutex admissionMutex;
    std::condition_variable admissionCv;
    bool postGenerationAdmissionDenied = false;
    persistence.setChunkSaveAdmission(
        [&](std::int32_t, std::int32_t) {
            const bool pending = generatedSideEffectPending.load(
                std::memory_order_acquire);
            if (pending) {
                std::lock_guard lock(admissionMutex);
                postGenerationAdmissionDenied = true;
                admissionCv.notify_all();
            }
            return !pending;
        });
    persistence.start();
    persistence.markDirty(0, 0);
    bool recheckObserved = false;
    {
        std::unique_lock lock(admissionMutex);
        recheckObserved = admissionCv.wait_for(
            lock, std::chrono::seconds(5), [&] {
                return postGenerationAdmissionDenied;
            });
    }
    stopPersistenceAndWait(persistence);
    const bool generatedSaveWasDeferred = recheckObserved &&
        generatedSideEffectPending.load(std::memory_order_acquire) &&
        persistence.isDirty(0, 0) && !std::filesystem::exists(regionPath);
    generatedSideEffectPending.store(false, std::memory_order_release);
    const bool retrySaved = persistence.flushChunk(0, 0);
    bool retryBytesReadable = false;
    try {
        retryBytesReadable =
            !RegionFile(regionPath.string()).load(0, 0).empty();
    } catch (const std::exception&) {
    }
    check(generatedSaveWasDeferred,
          "flushOnce rechecks admission after fallback generation publishes deferred chunk side effects");
    check(retrySaved && retryBytesReadable && !persistence.isDirty(0, 0),
          "a later admitted retry persists the generated chunk and clears its dirty generation");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "generated-side-effect admission fixture removes its exact temporary directory");
}

void testStructureLootDrainIsAtomicWithChunkSnapshot() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-structure-loot-drain-save-" + std::to_string(unique));
    const auto regionPath = directory / "region" / "r.0.0.mca";
    World world("minecraft:plains", LevelType::Flat, 0);
    auto* structures = world.structureManager();
    const auto chestState = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:chest"));
    const bool chunkGenerated = world.generateChunkIfMissing(0, 0);
    const bool chestPlaced = chunkGenerated &&
        world.setBlock(2, 64, 3, chestState);
    check(structures != nullptr && chestPlaced,
          "loot-drain race fixture creates a resident generated chest");
    if (!structures || !chestPlaced) return;
    structures->requeuePendingLoot(
        worldgen::StructureManager::PendingLoot{
            {2, 64, 3}, "minecraft:chests/simple_dungeon", 0},
        0);

    BlockEntityStore blockEntities;
    Persistence persistence(world, directory.string(), "minecraft:plains");
    check(blockEntities.bindMutationGate(world.mutationGateIdentity()),
          "loot-drain race fixture binds Block Entity publication to its World gate");
    persistence.setChunkExtras(
        [&](std::int32_t cx, std::int32_t cz, nbt::Value& root) {
            nbt::Value entities = nbt::Value::makeList(nbt::Compound);
            blockEntities.writeChunkNbt(cx, cz, entities);
            if (!entities.list.empty())
                root.set("block_entities", std::move(entities));
        }, {});
    persistence.setChunkSaveAdmission(
        [&](std::int32_t cx, std::int32_t cz) {
            return !structures->hasPendingLootInChunk(cx, cz);
        });

    std::mutex callbackMutex;
    std::condition_variable callbackCv;
    bool dirtyCallbackEntered = false;
    bool releaseDirtyCallback = false;
    bool saveBarrierEntered = false;
    bool releaseSaveBarrier = false;
    bool chunkSaveCompletedBeforeRelease = false;
    std::atomic<bool> pauseNextDirtyCallback{false};
    blockEntities.setDirtyCallback(
        [&](std::int32_t cx, std::int32_t cz) {
            persistence.markDirty(cx, cz);
            if (!pauseNextDirtyCallback.exchange(false,
                                                  std::memory_order_acq_rel))
                return;
            std::unique_lock lock(callbackMutex);
            dirtyCallbackEntered = true;
            callbackCv.notify_all();
            callbackCv.wait(lock, [&] { return releaseDirtyCallback; });
        });
    std::thread::id explicitSaverId;
    persistence.setChunkSaveAdmission(
        [&](std::int32_t cx, std::int32_t cz) {
            std::lock_guard lock(callbackMutex);
            return std::this_thread::get_id() == explicitSaverId &&
                   !structures->hasPendingLootInChunk(cx, cz);
        });
    persistence.setChunkSaveBarrier(
        [&](std::int32_t, std::int32_t) {
            std::unique_lock lock(callbackMutex);
            saveBarrierEntered = true;
            callbackCv.notify_all();
            callbackCv.wait(lock, [&] { return releaseSaveBarrier; });
        });
    persistence.setChunkSaveComplete(
        [&](std::int32_t, std::int32_t) {
            std::lock_guard lock(callbackMutex);
            chunkSaveCompletedBeforeRelease = !releaseDirtyCallback;
            callbackCv.notify_all();
        });
    persistence.start();
    persistence.markDirty(0, 0);
    pauseNextDirtyCallback.store(true, std::memory_order_release);

    PendingStructureLootDrainResult drainResult;
    std::thread drainer([&] {
        drainResult = drainPendingStructureLoot(
            world, blockEntities, *structures, 0);
    });
    bool reachedBlockEntityPublication = false;
    {
        std::unique_lock lock(callbackMutex);
        reachedBlockEntityPublication = callbackCv.wait_for(
            lock, std::chrono::seconds(5), [&] {
                return dirtyCallbackEntered;
            });
    }

    std::atomic<bool> saverReturned{false};
    std::thread saver;
    if (reachedBlockEntityPublication) {
        saver = std::thread([&] {
            {
                std::lock_guard lock(callbackMutex);
                explicitSaverId = std::this_thread::get_id();
            }
            callbackCv.notify_all();
            (void)persistence.flushChunk(0, 0);
            saverReturned.store(true, std::memory_order_release);
            callbackCv.notify_all();
        });
    }
    bool barrierReached = false;
    bool saveCompletedWhileBlockEntityWasStaged = false;
    if (reachedBlockEntityPublication) {
        std::unique_lock lock(callbackMutex);
        barrierReached = callbackCv.wait_for(
            lock, std::chrono::seconds(5), [&] {
                return saveBarrierEntered;
            });
        releaseSaveBarrier = true;
    }
    callbackCv.notify_all();
    bool saverBlockedOnWorldGate = false;
    const auto waiterDeadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(5);
    while (barrierReached && std::chrono::steady_clock::now() < waiterDeadline) {
        if (WorldMutationContractTestAccess::mutationOrderWaiters(world) != 0) {
            saverBlockedOnWorldGate = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    {
        std::lock_guard lock(callbackMutex);
        saveCompletedWhileBlockEntityWasStaged =
            chunkSaveCompletedBeforeRelease ||
            saverReturned.load(std::memory_order_acquire);
    }
    const bool snapshotWaitedForBlockEntityCommit =
        reachedBlockEntityPublication &&
        barrierReached && saverBlockedOnWorldGate &&
        !saveCompletedWhileBlockEntityWasStaged &&
        !saverReturned.load(std::memory_order_acquire);
    {
        std::lock_guard lock(callbackMutex);
        releaseDirtyCallback = true;
    }
    callbackCv.notify_all();
    drainer.join();
    if (saver.joinable()) saver.join();
    stopPersistenceAndWait(persistence);

    bool persistedLootReadable = false;
    try {
        const auto bytes = RegionFile(regionPath.string()).load(0, 0);
        if (!bytes.empty()) {
            ReadBuffer input(bytes);
            const auto chunk = nbt::Parser(input).readFileRoot();
            const auto* entities = chunk.get("block_entities");
            if (entities && entities->tag == nbt::List) {
                for (const auto& entity : entities->list) {
                    const auto* x = entity.get("x");
                    const auto* y = entity.get("y");
                    const auto* z = entity.get("z");
                    const auto* lootTable = entity.get("LootTable");
                    if (x && y && z && lootTable &&
                        x->tag == nbt::Int && y->tag == nbt::Int &&
                        z->tag == nbt::Int &&
                        x->i == 2 && y->i == 64 && z->i == 3 &&
                        lootTable->tag == nbt::String &&
                        lootTable->str ==
                            "minecraft:chests/simple_dungeon") {
                        persistedLootReadable = true;
                        break;
                    }
                }
            }
        }
    } catch (const std::exception&) {
    }
    check(snapshotWaitedForBlockEntityCommit,
          "a chunk snapshot cannot pass admission while drained loot is between queue removal and Block Entity commit");
    check(drainResult.persisted == 1 && drainResult.deferred == 0 &&
              !structures->hasPendingLootInChunk(0, 0) &&
              persistedLootReadable && !persistence.isDirty(0, 0),
          "the admitted retry persists the drained loot table in chunk NBT before clearing dirtiness");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "loot-drain race fixture removes its exact temporary directory");
}

void testSalvagesMalformedPrimaryAndStagedSiblings() {
    using namespace cppfm;
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("cppfm-mixed-region-recovery-" + std::to_string(unique));
    const auto regionPath = directory / "region" / "r.0.0.mca";
    std::error_code setupError;
    std::filesystem::create_directories(regionPath.parent_path(), setupError);
    check(!setupError,
          "mixed-corruption recovery fixture creates its scoped region directory");
    if (setupError) {
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
        check(!cleanupError,
              "mixed-corruption recovery fixture cleans up after setup failure");
        return;
    }

    const auto stone = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:stone"));
    const auto dirt = static_cast<std::uint16_t>(
        gen::blockNameToState().at("minecraft:dirt"));
    const auto encodeChunk = [](std::int32_t chunkX, std::uint16_t state) {
        Chunk chunk;
        ChunkBuildWriter writer(chunk);
        writer.blocks.set(Chunk::index(10, 4, 4, 4), state);
        WriteBuffer encoded;
        nbt::writeFileRoot(
            encoded, chunkToNBT(chunkX, 0, chunk, "minecraft:plains"));
        return encoded.data;
    };
    const auto staleTarget = encodeChunk(0, stone);
    const auto newerTarget = encodeChunk(0, dirt);
    const auto primarySibling = encodeChunk(2, stone);
    const auto stagedSibling = encodeChunk(4, dirt);
    const std::vector<std::uint8_t> malformed{0xFF};
    const std::vector<std::uint8_t> preexistingBackupData{
        0xFA, 0xCE, 0xB0, 0x0C};
    const std::vector<std::uint8_t> preexistingNumberedBackupData{
        0xDE, 0xAD, 0xBE, 0xEF};

    RegionFile primary(regionPath.string());
    primary.store(0, 0, staleTarget);
    primary.store(1, 0, malformed);
    primary.store(2, 0, primarySibling);
    RegionFile staged(regionPath.string() + ".new");
    staged.store(0, 0, newerTarget);
    staged.store(3, 0, malformed);
    staged.store(4, 0, stagedSibling);
    const auto reservedBackupPath = regionPath.string() + ".corrupt";
    RegionFile reservedBackup(reservedBackupPath);
    reservedBackup.store(31, 31, preexistingBackupData);
    const auto reservedNumberedBackupPath =
        regionPath.string() + ".corrupt.1";
    RegionFile reservedNumberedBackup(reservedNumberedBackupPath);
    reservedNumberedBackup.store(30, 30, preexistingNumberedBackupData);

    World world("minecraft:plains", LevelType::Flat, 0);
    Persistence persistence(world, directory.string(), "minecraft:plains");
    const auto recovered = persistence.loadChunkBytesRecovering(0, 0);
    check(recovered.status == Persistence::ChunkBytesRead::Status::Loaded &&
              recovered.bytes == newerTarget,
          "recovery selects valid newer staged target over the primary copy");

    RegionFile repaired(regionPath.string());
    check(repaired.load(0, 0) == newerTarget &&
              repaired.load(1, 0).empty() &&
              repaired.load(2, 0) == primarySibling &&
              repaired.load(3, 0).empty() &&
              repaired.load(4, 0) == stagedSibling,
          "recovered region retains valid staged and primary data and omits only malformed slots");

    const auto primaryBackupPath =
        std::filesystem::path(regionPath.string() + ".corrupt.2");
    const auto stagedBackupPath =
        std::filesystem::path(regionPath.string() + ".new.corrupt");
    check(std::filesystem::exists(reservedBackupPath) &&
              std::filesystem::exists(reservedNumberedBackupPath) &&
              std::filesystem::exists(primaryBackupPath) &&
              std::filesystem::exists(stagedBackupPath),
          "quarantine skips existing base and numbered backups before allocating the next suffix");
    if (std::filesystem::exists(reservedBackupPath)) {
        RegionFile preservedBackup(reservedBackupPath);
        check(preservedBackup.load(31, 31) == preexistingBackupData,
              "quarantine never overwrites the contents of a pre-existing corruption backup");
    }
    if (std::filesystem::exists(reservedNumberedBackupPath)) {
        RegionFile preservedNumberedBackup(reservedNumberedBackupPath);
        check(preservedNumberedBackup.load(30, 30) ==
                  preexistingNumberedBackupData,
              "quarantine never overwrites the contents of a pre-existing numbered backup");
    }
    if (std::filesystem::exists(primaryBackupPath) &&
        std::filesystem::exists(stagedBackupPath)) {
        RegionFile primaryBackup(primaryBackupPath.string());
        RegionFile stagedBackup(stagedBackupPath.string());
        check(primaryBackup.load(0, 0) == staleTarget &&
                  primaryBackup.load(1, 0) == malformed &&
                  primaryBackup.load(2, 0) == primarySibling &&
                  stagedBackup.load(0, 0) == newerTarget &&
                  stagedBackup.load(3, 0) == malformed &&
                  stagedBackup.load(4, 0) == stagedSibling,
              "corruption backups retain the original primary and staged region contents");
    }
    check(!std::filesystem::exists(regionPath.string() + ".new"),
          "recovery consumes the sanitized staged region file");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    check(!cleanupError,
          "mixed-corruption recovery fixture removes its exact temporary directory");
}

} // namespace

int main() {
    testRegionStorePreservesAnUnreadableExistingHeader();
    testStagedRecoveryPreservesSourcesOnNonCorruptionReadFailures();
    testStagedRecoveryDoesNotFollowDanglingTemporarySymlinks();
    testPlayerQuarantineSkipsDanglingCorruptSymlink();
    testRegionSaveRejectsDanglingStageSymlink();
    testWorldDataSaveSkipsDanglingTemporarySymlinks();
    testAsyncChunkReadUsesStagedRecovery();
    testStagedExternalSiblingRecoverySaveAndReopen();
    testValidStagedExternalSidecarPromotion();
    testPostRenameDirectorySyncFailureIsExplicit();
    testSyncAndAsyncStagedRecoveryParity();
    testDirtyGenerationReplacementAndRetry();
    testPendingStructureLootDefersChunkPersistence();
    testPendingLootSurvivesChunkUnloadAndIsReloadedAtShutdown();
    testPersistenceStopReportsUnflushedChunks();
    testPersistenceStopWaitsForAcceptedDirtyRegistration();
    testPersistenceStopFromSaveCallbackCannotDeadlockWorkerJoin();
    testPersistenceStopWaitsForSynchronousFlushCall();
    testPersistenceStopWaitsForPublicRawChunkStoreCall();
    testLevelDataOwnershipIsExplicitRatherThanPathInferred();
    testSynchronousSaveBarrierFailurePreservesDirtyState();
    testSaveAdmissionIsRecheckedAfterChunkGeneration();
    testStructureLootDrainIsAtomicWithChunkSnapshot();
    testSalvagesMalformedPrimaryAndStagedSiblings();
    std::printf("persistence_recovery: %d assertions, %d failures\n",
                assertions, failures);
    return failures == 0 ? 0 : 1;
}
