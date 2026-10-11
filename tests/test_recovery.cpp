// test_recovery — plan46 §2 (O-07/O-08): crash recovery + session.lock.
// Self-contained unit test (no server spawn):
//  - loadWithRecovery 3-stage matrix (dat / dat_old / fresh)
//  - corruption injection matrix: truncate × bitflip × empty
//  - playerdata isolation + quarantine
//  - session.lock acquire/release/stale
//  - corrupt region entry -> RegionFile::load empty (regenerate path)
#include "../src/game/World.hpp"
#include "../src/game/WorldDataManager.hpp"
#include "../src/game/SessionLock.hpp"
#include "../src/game/PlayerDataRecovery.hpp"
#include "../src/game/RegionFile.hpp"
#include "../src/core/NBT.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
using namespace cppfm;

#include "Harness.hpp"

static std::string mkTmp(const char* tag) {
    const char* base = std::getenv("CPPFM_RECOVERY_WORLD_PREFIX");
    if (!base || !*base) base = "/tmp/opencode";
    char tmpl[256];
    std::snprintf(tmpl, sizeof(tmpl), "%s/recov_%s_XXXXXX", base, tag);
    if (!::mkdtemp(tmpl)) { std::fprintf(stderr, "mkdtemp failed\n"); std::exit(2); }
    return std::string(tmpl);
}

static void writeFile(const std::string& p, const std::vector<std::uint8_t>& d) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(d.data()), d.size());
}
static std::vector<std::uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
}

static World makeWorld() { return World("minecraft:plains", LevelType::Normal, 12345ULL); }

// Save a valid level.dat (+_old via second save) into dir. Returns raw bytes.
static std::vector<std::uint8_t> saveGoodLevel(const std::string& dir) {
    World w = makeWorld();
    WorldDataManager m(dir);
    std::string diff = "normal";
    double dia = 59999968.0, cx = 0, cz = 0;
    bool ok = m.saveLevelDataWithProviders(100, 1000, w, diff, dia, cx, cz);
    CHECK(ok, "setup: saveLevelDataWithProviders ok");
    // second save produces level.dat_old backup
    ok = m.saveLevelDataWithProviders(200, 2000, w, diff, dia, cx, cz);
    CHECK(ok, "setup: second save (dat_old backup) ok");
    CHECK(fs::exists(dir + "/level.dat"), "setup: level.dat exists");
    CHECK(fs::exists(dir + "/level.dat_old"), "setup: level.dat_old exists");
    return readFile(dir + "/level.dat");
}

static void corruptTruncate(const std::string& p) {
    auto b = readFile(p);
    if (b.size() > 12) b.resize(12);
    writeFile(p, b);
}
static void corruptBitflip(const std::string& p) {
    auto b = readFile(p);
    for (std::size_t i = 0; i < b.size(); i += 7) b[i] ^= 0xFF;
    if (b.empty()) b.push_back(0xFF);
    writeFile(p, b);
}
static void corruptEmpty(const std::string& p) {
    writeFile(p, {});
}

int main() {
    std::printf("=== test_recovery — plan46 §2 O-07/O-08 ===\n");

    // -- 1) happy path: level.dat loads, source=Dat ---------------------------
    {
        std::string dir = mkTmp("happy");
        saveGoodLevel(dir);
        World w = makeWorld();
        WorldDataManager m(dir);
        std::string diff = "peaceful";
        double dia = 1, cx = 9, cz = 9;
        RecoveryResult r;
        bool ok = m.loadWithRecovery(w, diff, dia, cx, cz, nullptr, nullptr, r);
        CHECK(ok && r.ok && r.src == LevelSource::Dat, "happy: src=Dat ok=1");
        CHECK(!r.logLines.empty(), "happy: logLines non-empty (O-07c)");
        CHECK(diff == "normal", "happy: difficulty round-trips");
        CHECK(w.seed() == 12345ULL, "happy: random seed round-trips");
    }

    // -- 1a) floating-point integral fields must reject +2^63 --------------
    {
        std::string dir = mkTmp("int64-double-overflow");
        const auto good = saveGoodLevel(dir);
        ReadBuffer input(good);
        nbt::Parser parser(input);
        auto root = parser.readFileRoot();
        auto* data = root.get("Data");
        CHECK(data != nullptr, "int64 boundary fixture has a Data compound");
        if (data) {
            data->set("RandomSeed", nbt::Value::makeDouble(0x1p63));
            WriteBuffer encoded;
            nbt::writeFileRoot(encoded, root);
            writeFile(dir + "/level.dat", encoded.data);
            World world = makeWorld();
            WorldDataManager manager(dir);
            std::string difficulty = "normal";
            double diameter = 59999968.0, centerX = 0, centerZ = 0;
            const bool loaded = manager.tryLoadFile(
                dir + "/level.dat", world, difficulty, diameter,
                centerX, centerZ);
            CHECK(!loaded && world.seed() == 12345ULL,
                  "RandomSeed +2^63 is rejected before narrowing or mutating the world");
        }
    }

    // -- 1aa) rejected recovery candidates cannot leak partially applied state
    {
        const std::string dir = mkTmp("rejected-level-candidate-state");
        const auto good = saveGoodLevel(dir);

        auto makeRoot = [&](const std::vector<std::uint8_t>& bytes) {
            ReadBuffer input(bytes);
            nbt::Parser parser(input);
            return parser.readFileRoot();
        };
        auto writeRoot = [&](const std::string& path, const nbt::Value& root) {
            WriteBuffer encoded;
            nbt::writeFileRoot(encoded, root);
            writeFile(path, encoded.data);
        };
        const auto eraseDataKeys = [](nbt::Value& data,
                                      const std::vector<std::string>& keys) {
            data.comp.erase(
                std::remove_if(data.comp.begin(), data.comp.end(),
                    [&](const auto& entry) {
                        return std::find(keys.begin(), keys.end(), entry.first) != keys.end();
                    }),
                data.comp.end());
        };

        auto rejectedRoot = makeRoot(good);
        auto* rejectedData = rejectedRoot.get("Data");
        CHECK(rejectedData != nullptr, "recovery candidate fixture has Data");
        if (rejectedData) {
            rejectedData->set("RandomSeed", nbt::Value::makeLong(987654321));
            rejectedData->set("SpawnX", nbt::Value::makeInt(110));
            rejectedData->set("SpawnY", nbt::Value::makeInt(111));
            rejectedData->set("SpawnZ", nbt::Value::makeInt(112));
            rejectedData->set("SpawnAngle", nbt::Value::makeFloat(33.0f));
            rejectedData->set("Difficulty", nbt::Value::makeByte(3));
            auto poisonedBorder = nbt::Value::makeCompound();
            poisonedBorder.set("CenterX", nbt::Value::makeDouble(1001.0));
            poisonedBorder.set("CenterZ", nbt::Value::makeDouble(1002.0));
            poisonedBorder.set("Size", nbt::Value::makeDouble(1003.0));
            poisonedBorder.set("SizeLerpTarget", nbt::Value::makeDouble(1004.0));
            poisonedBorder.set("SizeLerpTime", nbt::Value::makeLong(1005));
            rejectedData->set("WorldBorder", std::move(poisonedBorder));
            auto poisonedForced = nbt::Value::makeList(nbt::Long);
            poisonedForced.list.push_back(
                nbt::Value::makeLong(chunkKey(20, 21)));
            rejectedData->set("ForcedChunks", std::move(poisonedForced));
        }
        writeRoot(dir + "/level.dat", rejectedRoot);

        auto fallbackRoot = makeRoot(good);
        auto* fallbackData = fallbackRoot.get("Data");
        CHECK(fallbackData != nullptr, "fallback candidate fixture has Data");
        if (fallbackData) {
            // The .new candidate intentionally omits every field poisoned in
            // level.dat. If the rejected primary mutates live state, its
            // values must not survive acceptance of this fallback.
            eraseDataKeys(*fallbackData,
                {"RandomSeed", "SpawnX", "SpawnY", "SpawnZ", "SpawnAngle",
                 "Difficulty", "WorldBorder", "ForcedChunks"});
        }
        writeRoot(dir + "/level.dat.new", fallbackRoot);

        World world = makeWorld();
        const World::SpawnPoint originalSpawn{1, 2, 3, 4.0f};
        world.setSpawnPoint(originalSpawn);
        world.setChunkForced(5, 6, true);
        world.markChunkLoadStarted();
        std::string difficulty = "peaceful";
        double diameter = 10.0, centerX = 20.0, centerZ = 30.0;
        double lerpTarget = 40.0;
        std::int64_t lerpMs = 50;
        int consumeCount = 0;
        WorldDataManager manager(dir);
        manager.setLevelStateProvider({}, [&](const nbt::Value&) { ++consumeCount; });
        RecoveryResult recovery;
        const bool loaded = manager.loadWithRecovery(
            world, difficulty, diameter, centerX, centerZ,
            &lerpTarget, &lerpMs, recovery);
        const auto spawn = world.spawnPoint();
        const auto forced = world.forcedChunkKeys();
        CHECK(loaded && recovery.ok && recovery.src == LevelSource::DatNew,
              "seed-rejected primary falls back to the valid .new candidate");
        CHECK(world.seed() == 12345ULL && spawn.x == originalSpawn.x &&
                  spawn.y == originalSpawn.y && spawn.z == originalSpawn.z &&
                  spawn.angle == originalSpawn.angle && difficulty == "peaceful" &&
                  diameter == 10.0 && centerX == 20.0 && centerZ == 30.0 &&
                  lerpTarget == 40.0 && lerpMs == 50 &&
                  forced == std::vector<std::int64_t>{chunkKey(5, 6)} &&
                  consumeCount == 1,
              "rejected primary leaves seed, spawn, difficulty, border, forced chunks, and consumer untouched");
    }

    // -- 1ab) commit failures are fatal, never another candidate rejection --
    {
        const std::string dir = mkTmp("accepted-level-commit-failure");
        const auto good = saveGoodLevel(dir);
        writeFile(dir + "/level.dat.new", good);

        World world = makeWorld();
        WorldDataManager manager(dir);
        int consumeCalls = 0;
        manager.setLevelStateProvider({}, [&](const nbt::Value&) {
            ++consumeCalls;
            throw std::runtime_error("injected level-state consumer failure");
        });
        std::string difficulty = "normal";
        double diameter = 59999968.0, centerX = 0.0, centerZ = 0.0;
        RecoveryResult recovery;
        bool propagated = false;
        try {
            (void)manager.loadWithRecovery(
                world, difficulty, diameter, centerX, centerZ,
                nullptr, nullptr, recovery);
        } catch (const std::runtime_error&) {
            propagated = true;
        }
        CHECK(propagated && consumeCalls == 1,
              "post-commit consumer failure escapes instead of retrying another level candidate");
        CHECK(fs::exists(dir + "/level.dat") &&
                  fs::exists(dir + "/level.dat.new") &&
                  !fs::exists(dir + "/level.dat.corrupt"),
              "fatal commit failure leaves both original recovery files untouched");
    }

    // -- 1ac) rename success with a failed parent sync remains loadable but is
    // explicitly reported as uncertain durability. --------------------------
    {
        const std::string dir = mkTmp("level-promotion-sync-uncertain");
        const auto good = saveGoodLevel(dir);
        fs::remove(dir + "/level.dat");
        fs::remove(dir + "/level.dat_old");
        writeFile(dir + "/level.dat.new", good);

        World world = makeWorld();
        WorldDataManager manager(dir);
        std::string difficulty = "normal";
        double diameter = 59999968.0, centerX = 0.0, centerZ = 0.0;
        RecoveryResult recovery;
        storage_file_ops::failNextDirectorySyncForTesting();
        const bool loaded = manager.loadWithRecovery(
            world, difficulty, diameter, centerX, centerZ,
            nullptr, nullptr, recovery);
        CHECK(loaded && recovery.ok && recovery.src == LevelSource::DatNew &&
                  recovery.durabilityUncertain,
              "post-rename level directory-sync failure reports accepted data with uncertain durability");
        CHECK(fs::exists(dir + "/level.dat") &&
                  readFile(dir + "/level.dat") == good &&
                  !fs::exists(dir + "/level.dat.new"),
              "durability warning does not fall through to fresh-world generation or misclassify accepted NBT");
    }

    // -- 1b) unknown level metadata survives repeated save cycles ------------
    {
        std::string dir = mkTmp("opaque-level-tags");
        const auto good = saveGoodLevel(dir);
        ReadBuffer input(good);
        nbt::Parser parser(input);
        auto root = parser.readFileRoot();
        root.set("example:root_metadata",
                 nbt::Value::makeString("keep root marker"));
        auto* data = root.get("Data");
        CHECK(data != nullptr, "opaque level fixture has a Data compound");
        if (data) {
            data->set("example:data_metadata",
                      nbt::Value::makeString("keep data marker"));
            data->set("GameType", nbt::Value::makeInt(1));
            auto worldGenSettings = nbt::Value::makeCompound();
            worldGenSettings.set("seed", nbt::Value::makeLong(987654321));
            data->set("WorldGenSettings", std::move(worldGenSettings));
            auto wanderingTraderId = nbt::Value::makeCompound();
            wanderingTraderId.set("example:uuid",
                                  nbt::Value::makeString("retained trader"));
            data->set("WanderingTraderId",
                      std::move(wanderingTraderId));
            auto dragonFight = nbt::Value::makeCompound();
            dragonFight.set("DragonKilled", nbt::Value::makeByte(1));
            dragonFight.set("example:progress", nbt::Value::makeString("kept"));
            data->set("DragonFight", std::move(dragonFight));
            if (auto* border = data->get("WorldBorder"))
                border->set("example:border_metadata",
                            nbt::Value::makeString("keep border marker"));
            WriteBuffer encoded;
            nbt::writeFileRoot(encoded, root);
            writeFile(dir + "/level.dat", encoded.data);

            World world = makeWorld();
            WorldDataManager manager(dir);
            manager.setLevelStateProvider(
                [](nbt::Value& level) {
                    level.set("WanderingTraderSpawnDelay",
                              nbt::Value::makeInt(300));
                    level.set("WanderingTraderSpawnChance",
                              nbt::Value::makeInt(35));
                    // Match the server's modeled fields: the existing UUID
                    // is opaque here and must not be reset to an empty value.
                }, {});
            std::string difficulty = "normal";
            double diameter = 59999968.0, centerX = 0, centerZ = 0;
            RecoveryResult recovery;
            const bool loaded = manager.loadWithRecovery(
                world, difficulty, diameter, centerX, centerZ,
                nullptr, nullptr, recovery);
            const bool firstSaved = manager.saveLevelDataWithProviders(
                300, 3000, world, difficulty, diameter, centerX, centerZ);
            const bool secondSaved = manager.saveLevelDataWithProviders(
                400, 4000, world, difficulty, diameter, centerX, centerZ);

            const auto savedBytes = readFile(dir + "/level.dat");
            ReadBuffer savedInput(savedBytes);
            nbt::Parser savedParser(savedInput);
            const auto savedRoot = savedParser.readFileRoot();
            const auto* savedData = savedRoot.get("Data");
            const auto* rootMarker =
                savedRoot.get("example:root_metadata");
            const auto* dataMarker = savedData
                ? savedData->get("example:data_metadata") : nullptr;
            const auto* savedGameType = savedData
                ? savedData->get("GameType") : nullptr;
            const auto* savedWorldGen = savedData
                ? savedData->get("WorldGenSettings") : nullptr;
            const auto* savedDragon = savedData
                ? savedData->get("DragonFight") : nullptr;
            const auto* savedTrader = savedData
                ? savedData->get("WanderingTraderId") : nullptr;
            const auto* traderMarker = savedTrader
                ? savedTrader->get("example:uuid") : nullptr;
            const auto* savedBorder = savedData
                ? savedData->get("WorldBorder") : nullptr;
            const auto* borderMarker = savedBorder
                ? savedBorder->get("example:border_metadata") : nullptr;
            const auto* dragonKilled = savedDragon
                ? savedDragon->get("DragonKilled") : nullptr;
            CHECK(loaded && firstSaved && secondSaved && rootMarker &&
                      rootMarker->tag == nbt::String &&
                      rootMarker->str == "keep root marker" && dataMarker &&
                      dataMarker->tag == nbt::String &&
                      dataMarker->str == "keep data marker" &&
                      savedGameType && savedGameType->tag == nbt::Int &&
                      savedGameType->i == 1 && savedWorldGen &&
                      savedWorldGen->tag == nbt::Compound && savedTrader &&
                      savedTrader->tag == nbt::Compound && traderMarker &&
                      traderMarker->tag == nbt::String &&
                      traderMarker->str == "retained trader" && savedDragon &&
                      dragonKilled && dragonKilled->tag == nbt::Byte &&
                      dragonKilled->b == 1 && borderMarker &&
                      borderMarker->tag == nbt::String &&
                      borderMarker->str == "keep border marker",
                  "level.dat saves preserve unknown root/data tags and unmodeled world state across repeated writes");
        }
    }

    // -- 1c) switching directories detaches the previous world's template ---
    {
        const std::string firstDir = mkTmp("metadata-template-first");
        const std::string secondDir = mkTmp("metadata-template-second");
        const auto firstBytes = saveGoodLevel(firstDir);
        ReadBuffer firstInput(firstBytes);
        nbt::Parser firstParser(firstInput);
        auto firstRoot = firstParser.readFileRoot();
        firstRoot.set("example:world_owner",
                      nbt::Value::makeString("first world"));
        WriteBuffer firstEncoded;
        nbt::writeFileRoot(firstEncoded, firstRoot);
        writeFile(firstDir + "/level.dat", firstEncoded.data);
        fs::create_directories(secondDir);

        WorldDataManager manager(firstDir);
        World firstWorld = makeWorld();
        std::string difficulty = "normal";
        double diameter = 59999968.0, centerX = 0, centerZ = 0;
        RecoveryResult recovery;
        const bool loadedFirst = manager.loadWithRecovery(
            firstWorld, difficulty, diameter, centerX, centerZ,
            nullptr, nullptr, recovery);
        manager.setDirectory(secondDir);
        World secondWorld = makeWorld();
        const bool savedSecond = manager.saveLevelDataWithProviders(
            10, 100, secondWorld, difficulty, diameter, centerX, centerZ);

        const auto secondBytes = readFile(secondDir + "/level.dat");
        ReadBuffer secondInput(secondBytes);
        nbt::Parser secondParser(secondInput);
        const auto secondRoot = secondParser.readFileRoot();
        const auto* owner = secondRoot.get("example:world_owner");
        CHECK(loadedFirst && savedSecond && owner == nullptr,
              "changing world directories clears opaque metadata from the previous save template");
    }

    // -- 1d) directory changes cannot split an in-flight metadata save ------
    {
        const std::string sourceDir = mkTmp("metadata-save-source");
        const std::string destinationDir = mkTmp("metadata-save-destination");
        const auto sourceBytes = saveGoodLevel(sourceDir);
        ReadBuffer sourceInput(sourceBytes);
        nbt::Parser sourceParser(sourceInput);
        auto sourceRoot = sourceParser.readFileRoot();
        sourceRoot.set("example:world_owner",
                       nbt::Value::makeString("source world"));
        WriteBuffer sourceEncoded;
        nbt::writeFileRoot(sourceEncoded, sourceRoot);
        writeFile(sourceDir + "/level.dat", sourceEncoded.data);

        WorldDataManager manager(sourceDir);
        World sourceWorld = makeWorld();
        std::string difficulty = "normal";
        double diameter = 59999968.0, centerX = 0, centerZ = 0;
        const bool loadedSource = manager.tryLoadFile(
            sourceDir + "/level.dat", sourceWorld, difficulty,
            diameter, centerX, centerZ);

        std::mutex mutex;
        std::condition_variable changed;
        bool providerEntered = false;
        bool releaseProvider = false;
        bool switchAttempted = false;
        bool switchCompleted = false;
        bool savedSource = false;
        manager.setLevelStateProvider(
            [&](nbt::Value&) {
                std::unique_lock lock(mutex);
                providerEntered = true;
                changed.notify_all();
                changed.wait(lock, [&] { return releaseProvider; });
            }, {});
        World saveWorld = makeWorld();
        std::thread saving([&] {
            savedSource = manager.saveLevelDataWithProviders(
                20, 200, saveWorld, difficulty, diameter, centerX, centerZ);
        });
        bool entered = false;
        {
            std::unique_lock lock(mutex);
            entered = changed.wait_for(lock, std::chrono::seconds(5),
                                       [&] { return providerEntered; });
        }
        std::thread switching;
        if (entered) {
            switching = std::thread([&] {
                {
                    std::lock_guard lock(mutex);
                    switchAttempted = true;
                }
                changed.notify_all();
                manager.setDirectory(destinationDir);
                {
                    std::lock_guard lock(mutex);
                    switchCompleted = true;
                }
                changed.notify_all();
            });
        }
        bool attempted = false;
        bool switchedBeforeSaveCommitted = false;
        if (entered) {
            std::unique_lock lock(mutex);
            attempted = changed.wait_for(lock, std::chrono::seconds(5),
                                         [&] { return switchAttempted; });
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            lock.lock();
            switchedBeforeSaveCommitted = switchCompleted;
            releaseProvider = true;
            lock.unlock();
            changed.notify_all();
        } else {
            std::lock_guard lock(mutex);
            releaseProvider = true;
            changed.notify_all();
        }
        saving.join();
        if (switching.joinable()) switching.join();

        const auto savedSourceBytes = readFile(sourceDir + "/level.dat");
        ReadBuffer savedSourceInput(savedSourceBytes);
        nbt::Parser savedSourceParser(savedSourceInput);
        const auto savedSourceRoot = savedSourceParser.readFileRoot();
        const auto* owner = savedSourceRoot.get("example:world_owner");
        CHECK(loadedSource && entered && attempted &&
                  !switchedBeforeSaveCommitted && switchCompleted &&
                  savedSource && owner && owner->tag == nbt::String &&
                  owner->str == "source world" &&
                  !fs::exists(destinationDir + "/level.dat"),
              "changing directories waits until the in-flight metadata save commits to its original world");
    }

    // -- 1b) a complete .new is the most recent committed save candidate ----
    {
        std::string dir = mkTmp("dat_new");
        const auto good = saveGoodLevel(dir);
        const std::vector<std::uint8_t> olderQuarantine{0x51, 0x52, 0x53};
        writeFile(dir + "/level.dat.corrupt", olderQuarantine);
        writeFile(dir + "/level.dat.new", good);
        corruptTruncate(dir + "/level.dat");
        World w = makeWorld();
        WorldDataManager m(dir);
        std::string diff = "x";
        double dia = 1, cx = 9, cz = 9;
        RecoveryResult r;
        const bool ok = m.loadWithRecovery(w, diff, dia, cx, cz, nullptr, nullptr, r);
        CHECK(ok && r.ok && r.src == LevelSource::DatNew,
              "dat.new: valid temporary file is recovered");
        CHECK(readFile(dir + "/level.dat.corrupt") == olderQuarantine &&
                  fs::exists(dir + "/level.dat.corrupt.1") &&
                  fs::exists(dir + "/level.dat") &&
                  !fs::exists(dir + "/level.dat.new"),
              "dat.new: quarantine collision preserves older evidence and promotes candidate");
        CHECK(readFile(dir + "/level.dat.corrupt.1").size() == 12,
              "dat.new: corrupt primary bytes move to the next unused quarantine name");
        CHECK(readFile(dir + "/level.dat") == good,
              "dat.new: promoted bytes are unchanged");
        CHECK(diff == "normal", "dat.new: difficulty round-trips");
    }

    // -- 1c) an unquarantinable primary must not be overwritten by .new ----
    {
        std::string dir = mkTmp("dat_new_quarantine_full");
        const auto good = saveGoodLevel(dir);
        auto corruptPrimary = good;
        if (corruptPrimary.size() > 12) corruptPrimary.resize(12);
        writeFile(dir + "/level.dat", corruptPrimary);
        writeFile(dir + "/level.dat.new", good);
        for (std::size_t suffix = 0; suffix < 10000; ++suffix) {
            const auto candidate = dir + "/level.dat.corrupt" +
                (suffix == 0 ? std::string{} : "." + std::to_string(suffix));
            writeFile(candidate, std::vector<std::uint8_t>{0xA5});
        }

        World w = makeWorld();
        WorldDataManager m(dir);
        std::string diff = "x";
        double dia = 1, cx = 9, cz = 9;
        RecoveryResult r;
        const bool ok = m.loadWithRecovery(w, diff, dia, cx, cz,
                                           nullptr, nullptr, r);
        CHECK(ok && r.ok && r.src == LevelSource::DatNew,
              "dat.new remains a usable in-memory source when quarantine names are exhausted");
        CHECK(readFile(dir + "/level.dat") == corruptPrimary &&
                  readFile(dir + "/level.dat.new") == good,
              "failed quarantine never replaces the corrupt primary or consumes the valid temporary candidate");
        std::error_code stageError;
        const bool foreignStagePathCreated = fs::create_directory(
            dir + "/level.dat.new.tmp", stageError);
        std::error_code backupError;
        const auto oldBackup = dir + "/level.dat_old";
        fs::remove(oldBackup, backupError);
        backupError.clear();
        const bool backupPathBlocked = fs::create_directory(
            oldBackup, backupError);
        const bool saveAfterRecovery = foreignStagePathCreated &&
            backupPathBlocked && m.saveLevelDataWithProviders(
                300, 3000, w, diff, dia, cx, cz);
        const auto publishedCandidate = readFile(dir + "/level.dat.new");
        bool candidateIsValid = false;
        try {
            ReadBuffer candidateInput(publishedCandidate);
            nbt::Parser candidateParser(candidateInput);
            const auto candidateRoot = candidateParser.readFileRoot();
            candidateIsValid = candidateRoot.get("Data") != nullptr;
        } catch (...) {
        }
        CHECK(foreignStagePathCreated && backupPathBlocked &&
                  !saveAfterRecovery && candidateIsValid &&
                  readFile(dir + "/level.dat") == corruptPrimary,
              "backup failure preserves the corrupt primary and already-published valid candidate");
        CHECK(fs::is_directory(dir + "/level.dat.new.tmp") &&
                  fs::is_directory(oldBackup),
              "collision-safe staging and failed backup preserve caller-owned paths");
        std::error_code cleanupError;
        fs::remove_all(dir, cleanupError);
        CHECK(!cleanupError,
              "dat.new quarantine exhaustion test removes its isolated temporary world");
    }

    // -- 2) corruption matrix: dat bad × {truncate,bitflip,empty}, old good ---
    const char* kinds[3] = {"truncate", "bitflip", "empty"};
    for (int k = 0; k < 3; ++k) {
        std::string dir = mkTmp(kinds[k]);
        saveGoodLevel(dir);
        if (k == 0) corruptTruncate(dir + "/level.dat");
        else if (k == 1) corruptBitflip(dir + "/level.dat");
        else corruptEmpty(dir + "/level.dat");
        World w = makeWorld();
        WorldDataManager m(dir);
        std::string diff = "x";
        double dia = 1, cx = 9, cz = 9;
        RecoveryResult r;
        bool ok = m.loadWithRecovery(w, diff, dia, cx, cz, nullptr, nullptr, r);
        char msg[160];
        std::snprintf(msg, sizeof(msg), "matrix %s: falls back to DatOld ok=1", kinds[k]);
        CHECK(ok && r.ok && r.src == LevelSource::DatOld, msg);
        std::snprintf(msg, sizeof(msg), "matrix %s: corrupt dat quarantined", kinds[k]);
        CHECK(fs::exists(dir + "/level.dat.corrupt"), msg);
        std::snprintf(msg, sizeof(msg), "matrix %s: logLines>=2 (attempt+source)", kinds[k]);
        CHECK(r.logLines.size() >= 2, msg);
    }

    // -- 3) both bad -> Fresh + caller-visible failure -------------------------
    {
        std::string dir = mkTmp("fresh");
        saveGoodLevel(dir);
        corruptBitflip(dir + "/level.dat");
        corruptTruncate(dir + "/level.dat_old");
        World w = makeWorld();
        WorldDataManager m(dir);
        std::string diff = "x";
        double dia = 1, cx = 9, cz = 9;
        RecoveryResult r;
        bool ok = m.loadWithRecovery(w, diff, dia, cx, cz, nullptr, nullptr, r);
        CHECK(!ok && !r.ok && r.src == LevelSource::Fresh, "both-bad: src=Fresh ok=0");
        CHECK(!r.logLines.empty(), "both-bad: logLines non-empty (O-07c)");
    }

    // A dangling symlink is an existing path entry, not an absent level.dat.
    // Recovery may proceed only after moving the link itself aside.
    {
        std::string dir = mkTmp("fresh_dangling_symlink");
        const auto primary = fs::path(dir) / "level.dat";
        std::error_code linkError;
        fs::create_symlink("missing-level-target", primary, linkError);
        CHECK(!linkError, "setup: create dangling level.dat symlink");
        if (!linkError) {
            World w = makeWorld();
            WorldDataManager m(dir);
            std::string diff = "x";
            double dia = 1, cx = 9, cz = 9;
            RecoveryResult r;
            bool threw = false;
            bool ok = false;
            try {
                ok = m.loadWithRecovery(w, diff, dia, cx, cz,
                                        nullptr, nullptr, r);
            } catch (const std::runtime_error&) {
                threw = true;
            }
            const auto sourceStatus = fs::symlink_status(primary, linkError);
            const bool sourceRetained = !linkError &&
                sourceStatus.type() == fs::file_type::symlink;
            const auto quarantined = fs::path(primary.string() + ".corrupt");
            std::error_code quarantineError;
            const auto quarantineStatus =
                fs::symlink_status(quarantined, quarantineError);
            const bool linkQuarantined = !quarantineError &&
                quarantineStatus.type() == fs::file_type::symlink;
            CHECK((threw && sourceRetained) ||
                      (!threw && !ok && r.src == LevelSource::Fresh &&
                       linkQuarantined),
                  "dangling level.dat link is quarantined or blocks fresh-world recovery");
        }
        std::error_code cleanupError;
        fs::remove_all(dir, cleanupError);
        CHECK(!cleanupError,
              "dangling-symlink recovery test removes its isolated world");
    }

    // A valid level.dat_old remains the rollback copy if a failed primary
    // cannot be quarantined before startup would permit the next save.
    {
        std::string dir = mkTmp("dat_old_quarantine_failure");
        saveGoodLevel(dir);
        const std::vector<std::uint8_t> corrupt{0x04, 0x05, 0x06};
        writeFile(dir + "/level.dat", corrupt);
        writeFile(dir + "/level.dat.new", corrupt);
        const auto acceptedBackup = readFile(dir + "/level.dat_old");
        storage_file_ops::failNextDirectorySyncForTesting();

        World w = makeWorld();
        WorldDataManager m(dir);
        std::string diff = "x";
        double dia = 1, cx = 9, cz = 9;
        RecoveryResult r;
        bool refusedUnsafeRecovery = false;
        try {
            (void)m.loadWithRecovery(w, diff, dia, cx, cz,
                                    nullptr, nullptr, r);
        } catch (const std::runtime_error&) {
            refusedUnsafeRecovery = true;
        }
        CHECK(refusedUnsafeRecovery && r.src == LevelSource::DatOld && r.ok,
              "valid level.dat_old does not start with an unpreserved corrupt primary");
        CHECK(readFile(dir + "/level.dat") == corrupt &&
                  readFile(dir + "/level.dat_old") == acceptedBackup &&
                  !acceptedBackup.empty() && acceptedBackup != corrupt,
              "failed quarantine preserves both corrupt source and last known-good backup");
        std::error_code cleanupError;
        fs::remove_all(dir, cleanupError);
        CHECK(!cleanupError,
              "old-backup quarantine test removes its isolated world");
    }

    // If recovery cannot durably preserve an unreadable candidate, startup
    // must stop instead of treating the directory as a fresh world.
    {
        std::string dir = mkTmp("fresh_quarantine_failure");
        const std::vector<std::uint8_t> corrupt{0x01, 0x02, 0x03};
        writeFile(dir + "/level.dat", corrupt);
        writeFile(dir + "/level.dat.new", corrupt);
        writeFile(dir + "/level.dat_old", corrupt);
        storage_file_ops::failNextDirectorySyncForTesting();
        World w = makeWorld();
        WorldDataManager m(dir);
        std::string diff = "x";
        double dia = 1, cx = 9, cz = 9;
        RecoveryResult r;
        bool refusedFreshFallback = false;
        try {
            (void)m.loadWithRecovery(w, diff, dia, cx, cz, nullptr, nullptr, r);
        } catch (const std::runtime_error&) {
            refusedFreshFallback = true;
        }
        CHECK(refusedFreshFallback && r.src == LevelSource::Fresh && !r.ok,
              "recovery refuses fresh-world fallback when an unreadable file cannot be preserved");
        CHECK(readFile(dir + "/level.dat") == corrupt &&
                  fs::exists(dir + "/level.dat.corrupt"),
              "failed quarantine retains original bytes and any evidence copy");
        std::error_code cleanupError;
        fs::remove_all(dir, cleanupError);
        CHECK(!cleanupError,
              "failed-quarantine recovery test removes its isolated world");
    }

    // -- 4) playerdata isolation ------------------------------------------------
    {
        std::string dir = mkTmp("player");
        fs::create_directories(dir);
        // 4a: missing file -> false, no quarantine
        {
            bool unsafe = true;
            bool ok = loadPlayerDataIsolated(dir + "/nobody.dat",
                                             [](const std::string&) { return true; },
                                             nullptr, nullptr, &unsafe);
            CHECK(!ok && !unsafe && !fs::exists(dir + "/nobody.dat.corrupt"), "playerdata: missing -> false, no quarantine");
        }
        // 4b: corrupt file -> false + quarantine
        {
            const std::string p = dir + "/bad.dat";
            writeFile(p, std::vector<std::uint8_t>{0x01, 0x02, 0x03, 0xFF});
            std::string q;
            bool ok = loadPlayerDataIsolated(p,
                                             [](const std::string&) { return false; }, &q);
            CHECK(!ok && fs::exists(p + ".corrupt") && !fs::exists(p), "playerdata: corrupt -> quarantined");
            CHECK(!q.empty(), "playerdata: quarantine path reported");
        }
        // 4c: existing quarantine is preserved; the new recovery copy gets a suffix.
        {
            const std::string p = dir + "/collision.dat";
            const std::string priorQuarantine = p + ".corrupt";
            const std::vector<std::uint8_t> sourceBytes{0x02, 0x03, 0x04};
            const std::vector<std::uint8_t> priorBytes{0x09, 0x08};
            writeFile(p, sourceBytes);
            writeFile(priorQuarantine, priorBytes);
            std::string q;
            bool unsafe = false;
            const bool ok = loadPlayerDataIsolated(
                p, [](const std::string&) { return false; }, &q, nullptr,
                &unsafe);
            std::ifstream priorFile(priorQuarantine, std::ios::binary);
            const std::vector<std::uint8_t> retainedPrior(
                (std::istreambuf_iterator<char>(priorFile)),
                std::istreambuf_iterator<char>());
            std::ifstream quarantineFile(q, std::ios::binary);
            const std::vector<std::uint8_t> retainedSource(
                (std::istreambuf_iterator<char>(quarantineFile)),
                std::istreambuf_iterator<char>());
            CHECK(!ok && !unsafe && q == priorQuarantine + ".1" &&
                      !fs::exists(p) && retainedPrior == priorBytes &&
                      retainedSource == sourceBytes,
                  "playerdata: quarantine collisions never replace older recovery data");
        }
        // 4d: a non-file at the active path cannot be quarantined; callers must fail closed.
        {
            const std::string p = dir + "/not-a-file.dat";
            fs::create_directories(p);
            bool unsafe = false;
            const bool ok = loadPlayerDataIsolated(
                p, [](const std::string&) { return false; }, nullptr, nullptr,
                &unsafe);
            CHECK(!ok && unsafe && fs::is_directory(p),
                  "playerdata: quarantine failure is reported unsafe and retains the active source");
        }
#ifndef _WIN32
        // A dangling symlink or FIFO is an existing unsafe entry, not a
        // missing/corrupt player save. Never follow it or block on a writer.
        {
            const std::string p = dir + "/dangling-link.dat";
            fs::create_symlink(dir + "/missing-player-target", p);
            bool loaderCalled = false;
            bool unsafe = false;
            const bool ok = loadPlayerDataIsolated(
                p,
                [&](const std::string&) {
                    loaderCalled = true;
                    return true;
                }, nullptr, nullptr, &unsafe);
            std::error_code linkError;
            const auto linkStatus = fs::symlink_status(p, linkError);
            CHECK(!ok && unsafe && !loaderCalled && !linkError &&
                      fs::is_symlink(linkStatus) && !fs::exists(p + ".corrupt"),
                  "playerdata: dangling symlink is preserved and rejected without following it");
        }
        {
            const std::string p = dir + "/fifo.dat";
            const bool fifoCreated = ::mkfifo(p.c_str(), 0600) == 0;
            bool loaderCalled = false;
            bool unsafe = false;
            const bool ok = fifoCreated && loadPlayerDataIsolated(
                p,
                [&](const std::string&) {
                    loaderCalled = true;
                    return false;
                }, nullptr, nullptr, &unsafe);
            CHECK(fifoCreated && !ok && unsafe && !loaderCalled && fs::exists(p),
                  "playerdata: FIFO is rejected without waiting for a writer");
        }
#endif
        // 4e: an unsupported source remains active and is reported unsafe.
        {
            const std::string p = dir + "/unsupported.dat";
            const std::vector<std::uint8_t> sourceBytes{0x0A, 0x01, 0x02};
            writeFile(p, sourceBytes);
            bool unsupported = false;
            bool unsafe = false;
            const bool ok = loadPlayerDataIsolated(
                p,
                [&](const std::string&) {
                    unsupported = true;
                    return false;
                }, nullptr, &unsupported, &unsafe);
            CHECK(!ok && unsupported && unsafe && fs::exists(p) &&
                      !fs::exists(p + ".corrupt") && readFile(p) == sourceBytes,
                  "playerdata: unsupported content stays at its active path and blocks fresh-state saves");
        }
        // 4f: good file -> true, untouched
        {
            const std::string p = dir + "/good.dat";
            writeFile(p, std::vector<std::uint8_t>{0x0A, 0x00});
            bool ok = loadPlayerDataIsolated(p,
                                             [](const std::string&) { return true; });
            CHECK(ok && fs::exists(p) && !fs::exists(p + ".corrupt"), "playerdata: good -> true, untouched");
        }
        // 4g: loader throws -> false + quarantine
        {
            const std::string p = dir + "/throw.dat";
            writeFile(p, std::vector<std::uint8_t>{0x00});
            bool ok = loadPlayerDataIsolated(p, [](const std::string&) -> bool {
                throw std::runtime_error("boom");
            });
            CHECK(!ok && fs::exists(p + ".corrupt"), "playerdata: throwing loader -> quarantined");
        }
        // Operational/resource failures are not evidence that the source is
        // corrupt. The production loader marks these failures explicitly so
        // admission fails closed without moving the active player file.
        for (const bool allocationFailure : {false, true}) {
            const std::string p = dir +
                (allocationFailure ? "/allocation-failure.dat"
                                   : "/io-failure.dat");
            const std::vector<std::uint8_t> sourceBytes{0x0A, 0x0B, 0x0C};
            writeFile(p, sourceBytes);
            bool preserveOriginal = false;
            bool unsafe = false;
            const bool ok = loadPlayerDataIsolated(
                p,
                [&](const std::string&) {
                    try {
                        if (allocationFailure) throw std::bad_alloc();
                        throw std::system_error(
                            std::make_error_code(std::errc::io_error),
                            "injected player-data read failure");
                    } catch (const std::bad_alloc&) {
                        preserveOriginal = true;
                        return false;
                    } catch (const std::system_error&) {
                        preserveOriginal = true;
                        return false;
                    }
                }, nullptr, &preserveOriginal, &unsafe);
            CHECK(!ok && preserveOriginal && unsafe && fs::exists(p) &&
                      !fs::exists(p + ".corrupt") && readFile(p) == sourceBytes,
                  allocationFailure
                      ? "playerdata: allocation failure preserves the active source and blocks admission"
                      : "playerdata: transient I/O failure preserves the active source and blocks admission");
        }
        // 4h: quarantine bytes and its directory entry are synced before the
        // source is removed; the unlink is then followed by another dir sync.
        {
            const std::string p = dir + "/sync-order.dat";
            const std::vector<std::uint8_t> sourceBytes{0x10, 0x20, 0x30, 0x40};
            writeFile(p, sourceBytes);
            std::string q;
            std::vector<std::string> syncOrder;
            std::vector<bool> sourceExistsAtDirectorySync;
            const std::vector<std::string> expectedSyncOrder{
                "file", "directory", "directory"};
            const std::vector<bool> expectedSourcePresence{true, false};
            const bool ok = loadPlayerDataIsolatedWithSync(
                p, [](const std::string&) { return false; },
                [&](const fs::path& syncedPath) {
                    syncOrder.emplace_back("file");
                    CHECK(syncedPath == fs::path(q) && fs::exists(p),
                          "playerdata: quarantine file sync precedes source removal");
                    return readFile(syncedPath.string()) == sourceBytes;
                },
                [&](const fs::path&) {
                    syncOrder.emplace_back("directory");
                    sourceExistsAtDirectorySync.push_back(fs::exists(p));
                    return true;
                },
                &q);
            CHECK(!ok && q == p + ".corrupt" && !fs::exists(p) &&
                      readFile(q) == sourceBytes,
                  "playerdata: durable quarantine replaces the active source");
            CHECK(syncOrder == expectedSyncOrder &&
                      sourceExistsAtDirectorySync == expectedSourcePresence,
                  "playerdata: quarantine entry synced before unlink and directory synced afterward");
        }
        // 4i: failure to sync the quarantine contents or its new directory
        // entry must leave the active source in place and block fresh saves.
        for (const bool failFileSync : {true, false}) {
            const std::string p = dir + (failFileSync ? "/file-sync-fail.dat"
                                                       : "/entry-sync-fail.dat");
            const std::vector<std::uint8_t> sourceBytes{0x51, 0x62, 0x73};
            writeFile(p, sourceBytes);
            std::string q;
            bool unsafe = false;
            int fileSyncCalls = 0;
            int directorySyncCalls = 0;
            const bool ok = loadPlayerDataIsolatedWithSync(
                p, [](const std::string&) { return false; },
                [&](const fs::path&) {
                    ++fileSyncCalls;
                    return !failFileSync;
                },
                [&](const fs::path&) {
                    ++directorySyncCalls;
                    return failFileSync;
                },
                &q, nullptr, &unsafe);
            CHECK(!ok && unsafe && fs::exists(p) && readFile(p) == sourceBytes &&
                      !q.empty() && fs::exists(q) && readFile(q) == sourceBytes,
                  failFileSync
                      ? "playerdata: file sync failure retains original source"
                      : "playerdata: pre-unlink directory sync failure retains original source");
            CHECK(fileSyncCalls == 1 &&
                      directorySyncCalls == (failFileSync ? 0 : 1),
                  "playerdata: failed pre-unlink durability gate stops before source removal");
        }
        // 4j: if syncing the unlink fails, retain the already-durable backup,
        // restore the active name best-effort, and report unsafe either way.
        {
            const std::string p = dir + "/unlink-sync-fail.dat";
            const std::vector<std::uint8_t> sourceBytes{0xA1, 0xB2, 0xC3};
            writeFile(p, sourceBytes);
            std::string q;
            bool unsafe = false;
            int directorySyncCalls = 0;
            int fileSyncCalls = 0;
            const bool ok = loadPlayerDataIsolatedWithSync(
                p, [](const std::string&) { return false; },
                [&](const fs::path& syncedPath) {
                    ++fileSyncCalls;
                    return (syncedPath == fs::path(q) && fileSyncCalls == 1) ||
                           (syncedPath == fs::path(p) && fileSyncCalls == 2);
                },
                [&](const fs::path&) {
                    ++directorySyncCalls;
                    return directorySyncCalls != 2;
                },
                &q, nullptr, &unsafe);
            CHECK(!ok && unsafe && fs::exists(p) && readFile(p) == sourceBytes &&
                      fs::exists(q) && readFile(q) == sourceBytes,
                  "playerdata: failed post-unlink directory sync restores source and retains durable quarantine");
            CHECK(fileSyncCalls == 2 && directorySyncCalls == 3,
                  "playerdata: failed unlink sync triggers source file and directory resync");
        }
        // 4k: model the Windows write-through move strategy. The source is
        // flushed first, a colliding destination is never replaced, and the
        // no-replace move installs a quarantine name while removing the source
        // atomically. The directory-sync callback must not be used as a stand-in
        // for MOVEFILE_WRITE_THROUGH.
        {
            const std::string p = dir + "/write-through-move.dat";
            const std::string priorQuarantine = p + ".corrupt";
            const std::vector<std::uint8_t> sourceBytes{0xD1, 0xD2, 0xD3};
            const std::vector<std::uint8_t> priorBytes{0xE1, 0xE2};
            writeFile(p, sourceBytes);
            writeFile(priorQuarantine, priorBytes);
            std::string q;
            bool unsafe = false;
            int directorySyncCalls = 0;
            int moveCalls = 0;
            std::vector<std::string> operations;
            const std::vector<std::string> expectedOperations{
                "sync-source", "durable-move", "durable-move"};
            const bool ok = loadPlayerDataIsolatedWithDurableMove(
                p, [](const std::string&) { return false; },
                [&](const fs::path& syncedPath) {
                    operations.emplace_back("sync-source");
                    return syncedPath == fs::path(p) &&
                           readFile(p) == sourceBytes;
                },
                [&](const fs::path&) {
                    ++directorySyncCalls;
                    return false;
                },
                [&](const fs::path& source, const fs::path& destination) {
                    operations.emplace_back("durable-move");
                    ++moveCalls;
                    CHECK(source == fs::path(p) && fs::exists(source) &&
                              readFile(source.string()) == sourceBytes,
                          "playerdata: source remains until durable move operation");
                    if (fs::exists(destination))
                        return PlayerDataQuarantineMoveResult::DestinationExists;
                    std::error_code moveError;
                    fs::rename(source, destination, moveError);
                    return moveError ? PlayerDataQuarantineMoveResult::Failed
                                     : PlayerDataQuarantineMoveResult::Moved;
                },
                &q, nullptr, &unsafe);
            CHECK(!ok && !unsafe && q == priorQuarantine + ".1" &&
                      !fs::exists(p) && readFile(q) == sourceBytes &&
                      readFile(priorQuarantine) == priorBytes,
                  "playerdata: durable move uses unused quarantine name and preserves prior quarantine");
            CHECK(operations == expectedOperations && moveCalls == 2 &&
                      directorySyncCalls == 0,
                  "playerdata: durable-move strategy flushes source and never relies on directory-sync no-op");
        }
        // 4l: when the write-through move cannot be confirmed, fail closed and
        // leave the original active file untouched.
        {
            const std::string p = dir + "/write-through-fail.dat";
            const std::vector<std::uint8_t> sourceBytes{0xF1, 0xF2};
            writeFile(p, sourceBytes);
            bool unsafe = false;
            int directorySyncCalls = 0;
            int moveCalls = 0;
            const bool ok = loadPlayerDataIsolatedWithDurableMove(
                p, [](const std::string&) { return false; },
                [&](const fs::path& syncedPath) {
                    return syncedPath == fs::path(p);
                },
                [&](const fs::path&) {
                    ++directorySyncCalls;
                    return true;
                },
                [&](const fs::path& source, const fs::path&) {
                    ++moveCalls;
                    CHECK(fs::exists(source),
                          "playerdata: failed durable move leaves source present");
                    return PlayerDataQuarantineMoveResult::Failed;
                },
                nullptr, nullptr, &unsafe);
            CHECK(!ok && unsafe && fs::exists(p) && readFile(p) == sourceBytes,
                  "playerdata: unconfirmed durable move retains active source and fails closed");
            CHECK(moveCalls == 1 && directorySyncCalls == 0,
                  "playerdata: failed durable move does not claim directory durability");
        }
    }

    // -- 5) session.lock ---------------------------------------------------------
    {
        std::string dir = mkTmp("lock");
        SessionLock a;
        bool live = true;
        CHECK(a.acquire(dir, live) && a.held() && !live, "sessionlock: acquire ok, no live holder");
#ifdef __unix__
        SessionLock duplicate;
        bool duplicateLive = false;
        CHECK(!duplicate.acquire(dir, duplicateLive) && duplicateLive && !duplicate.held(),
              "sessionlock: second owner is rejected while first lock is held");
#endif
        {
            std::ifstream f(dir + "/session.lock");
            std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            CHECK(SessionLock::parsePid(c) == SessionLock::selfPid(), "sessionlock: lock holds our pid");
        }
        a.release();
        CHECK(fs::exists(dir + "/session.lock"),
              "sessionlock: release keeps diagnostic inode for safe hand-off");
        // A stale record (dead pid) -> acquire proceeds, no live flag.
        {
            std::ofstream f(dir + "/session.lock", std::ios::trunc);
            f << "99999999 1\n";
        }
        SessionLock b;
        bool live2 = true;
        CHECK(b.acquire(dir, live2) && !live2, "sessionlock: stale lock overwritten, live=0");
    }

    // -- 6) corrupt region entry -> load empty (regenerate, O-07b) ---------------
    {
        std::string dir = mkTmp("region");
        const std::string mca = dir + "/r.0.0.mca";
        // header: entry (0,0) -> sector 2, 1 sector; rest zero
        std::vector<std::uint8_t> hdr(8192, 0);
        hdr[0] = 0; hdr[1] = 0; hdr[2] = 2; hdr[3] = 1;
        std::vector<std::uint8_t> sector(4096, 0);
        // length=6, comp=2, 5 bytes of garbage zlib
        sector[0] = 0; sector[1] = 0; sector[2] = 0; sector[3] = 6;
        sector[4] = 2;
        sector[5] = 0x78; sector[6] = 0x9C; sector[7] = 0xFF; sector[8] = 0xFF; sector[9] = 0xFF;
        writeFile(mca, hdr);
        std::ofstream f(mca, std::ios::binary | std::ios::app);
        f.write(reinterpret_cast<const char*>(sector.data()), sector.size());
        f.close();
        RegionFile rf(mca);
        // NB: RegionFile::load may throw on garbage zlib; Persistence::loadChunk
        // catches it and regenerates. Mirror that contract here.
        bool regen = false;
        try {
            auto bytes = rf.load(0, 0);
            regen = bytes.empty();
        } catch (...) { regen = true; }
        CHECK(regen, "region: garbage chunk -> regenerate path (O-07b)");
        auto missing = rf.load(5, 5);
        CHECK(missing.empty(), "region: absent chunk -> empty");
    }

    std::printf("test_recovery: %d PASS %d FAIL\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
