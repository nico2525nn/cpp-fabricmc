#include "WorldDataManager.hpp"
#include "World.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>

namespace cppfm {

namespace {
bool readInt32(const nbt::Value& value, std::int32_t& out) {
    if (value.tag == nbt::Int) {
        out = value.i;
        return true;
    }
    if (value.tag == nbt::Long &&
        value.l >= std::numeric_limits<std::int32_t>::min() &&
        value.l <= std::numeric_limits<std::int32_t>::max()) {
        out = static_cast<std::int32_t>(value.l);
        return true;
    }
    return false;
}

bool readInt64(const nbt::Value& value, std::int64_t& out) {
    if (value.tag == nbt::Long) {
        out = value.l;
        return true;
    }
    if (value.tag == nbt::Int) {
        out = value.i;
        return true;
    }
    // double(INT64_MAX) rounds up to +2^63, outside int64_t. Use an
    // exclusive upper bound so that value cannot reach an out-of-range cast.
    constexpr double kInt64Min = -0x1p63;
    constexpr double kInt64MaxExclusive = 0x1p63;
    if (value.tag == nbt::Double && std::isfinite(value.d) &&
        value.d >= kInt64Min && value.d < kInt64MaxExclusive) {
        out = static_cast<std::int64_t>(value.d);
        return true;
    }
    return false;
}

bool readDouble(const nbt::Value& value, double& out) {
    switch (value.tag) {
        case nbt::Double: out = value.d; break;
        case nbt::Float: out = value.f; break;
        case nbt::Int: out = static_cast<double>(value.i); break;
        case nbt::Long: out = static_cast<double>(value.l); break;
        default: return false;
    }
    return std::isfinite(out);
}

bool validLevelDataShape(const nbt::Value& data) {
    if (data.tag != nbt::Compound) return false;

    if (const auto* version = data.get("DataVersion")) {
        std::int64_t ignored = 0;
        if (!readInt64(*version, ignored)) return false;
    }
    if (const auto* seed = data.get("RandomSeed")) {
        std::int64_t ignored = 0;
        if (!readInt64(*seed, ignored)) return false;
    }

    for (const char* key : {"SpawnX", "SpawnY", "SpawnZ"}) {
        if (const auto* value = data.get(key)) {
            std::int32_t ignored = 0;
            if (!readInt32(*value, ignored)) return false;
        }
    }
    if (const auto* angle = data.get("SpawnAngle")) {
        double ignored = 0;
        if ((angle->tag != nbt::Float && angle->tag != nbt::Double) ||
            !readDouble(*angle, ignored)) return false;
    }
    if (const auto* difficulty = data.get("Difficulty")) {
        if (difficulty->tag == nbt::Byte) {
            if (difficulty->b < 0 || difficulty->b > 3) return false;
        } else if (difficulty->tag == nbt::String) {
            if (difficulty->str != "peaceful" && difficulty->str != "easy" &&
                difficulty->str != "normal" && difficulty->str != "hard")
                return false;
        } else {
            return false;
        }
    }
    if (const auto* gameType = data.get("GameType")) {
        std::int32_t mode = 0;
        if (gameType->tag == nbt::Byte) mode = gameType->b;
        else if (gameType->tag == nbt::Short) mode = gameType->s;
        else if (!readInt32(*gameType, mode)) return false;
        if (mode < 0 || mode > 3) return false;
    }

    if (const auto* border = data.get("WorldBorder")) {
        if (border->tag != nbt::Compound) return false;
        for (const char* key : {"CenterX", "CenterZ", "Size", "SizeLerpTarget"}) {
            if (const auto* value = border->get(key)) {
                double ignored = 0;
                if (!readDouble(*value, ignored)) return false;
            }
        }
        if (const auto* value = border->get("SizeLerpTime")) {
            std::int64_t ignored = 0;
            if (!readInt64(*value, ignored)) return false;
        }
    }

    if (const auto* forced = data.get("ForcedChunks")) {
        if (forced->tag != nbt::List ||
            (forced->listElement != nbt::End &&
             forced->listElement != nbt::Int &&
             forced->listElement != nbt::Long))
            return false;
        for (const auto& value : forced->list) {
            if (value.tag != nbt::Int && value.tag != nbt::Long) return false;
        }
    }
    return true;
}

std::string quarantineFile(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return {};
    for (std::size_t suffix = 0; suffix < 10000; ++suffix) {
        const std::string candidate = path + ".corrupt" +
            (suffix == 0 ? std::string{} : "." + std::to_string(suffix));
        ec.clear();
        const auto outcome = storage_file_ops::moveFileWithoutReplace(
            path, candidate, ec);
        if (outcome == storage_file_ops::MoveWithoutReplaceStatus::Moved)
            return candidate;
        if (outcome == storage_file_ops::MoveWithoutReplaceStatus::
                           MovedWithUncertainSourceRemoval) {
            std::fprintf(stderr,
                         "[WorldDataManager] quarantine destination %s is durable, but source unlink durability is uncertain\n",
                         candidate.c_str());
            return candidate;
        }
        if (ec == std::errc::file_exists) continue;
        return {};
    }
    return {};
}
}

bool WorldDataManager::saveLevelDataWithProviders(std::int64_t worldTicks, std::int64_t dayTime, World& world,
                                    const std::string& difficulty,
                                    double borderDiameter, double borderCX, double borderCZ,
                                    double borderLerpTarget, std::int64_t borderLerpMs) {
    // Keep the directory and the opaque-root template in one transaction.
    // setDirectory() takes this same lock before replacing either value.
    std::lock_guard saveLock(fileMutex());
    if (!ownsLevelData()) return false;
    try {
        nbt::Value root = nbt::Value::makeCompound();
        nbt::Value data = nbt::Value::makeCompound();
        {
            std::lock_guard lock(levelRootMtx_);
            if (hasLoadedLevelRoot_) root = loadedLevelRoot_;
        }
        if (const auto* savedData = root.get("Data");
            savedData && savedData->tag == nbt::Compound)
            data = *savedData;
        data.set("DataVersion", nbt::Value::makeInt(kCurrentDataVersion));
        data.set("RandomSeed", nbt::Value::makeLong(
            static_cast<std::int64_t>(world.seed())));
        auto spawn = world.spawnPoint();
        data.set("SpawnX", nbt::Value::makeInt(spawn.x));
        data.set("SpawnY", nbt::Value::makeInt(spawn.y));
        data.set("SpawnZ", nbt::Value::makeInt(spawn.z));
        data.set("SpawnAngle", nbt::Value::makeFloat(spawn.angle));
        data.set("Time", nbt::Value::makeLong(worldTicks));
        data.set("DayTime", nbt::Value::makeLong(dayTime));
        data.set("LevelName", nbt::Value::makeString("CppFabricMC World"));
        if (!data.get("raining"))
            data.set("raining", nbt::Value::makeByte(0));
        if (!data.get("thundering"))
            data.set("thundering", nbt::Value::makeByte(0));
        {
            int diffByte = 2;
            if (difficulty=="peaceful") diffByte=0;
            else if (difficulty=="easy") diffByte=1;
            else if (difficulty=="normal") diffByte=2;
            else if (difficulty=="hard") diffByte=3;
            data.set("Difficulty", nbt::Value::makeByte((std::int8_t)diffByte));
            if (!data.get("DifficultyLocked"))
                data.set("DifficultyLocked", nbt::Value::makeByte(0));
        }
        {
            nbt::Value ver = nbt::Value::makeCompound();
            if (const auto* savedVersion = data.get("Version");
                savedVersion && savedVersion->tag == nbt::Compound)
                ver = *savedVersion;
            ver.set("Name", nbt::Value::makeString("1.21.4"));
            ver.set("Id", nbt::Value::makeInt(kCurrentDataVersion));
            ver.set("Snapshot", nbt::Value::makeByte(0));
            ver.set("Series", nbt::Value::makeString("main"));
            data.set("Version", ver);
        }
        {
            nbt::Value wb = nbt::Value::makeCompound();
            if (const auto* savedBorder = data.get("WorldBorder");
                savedBorder && savedBorder->tag == nbt::Compound)
                wb = *savedBorder;
            wb.set("CenterX", nbt::Value::makeDouble(borderCX));
            wb.set("CenterZ", nbt::Value::makeDouble(borderCZ));
            wb.set("Size", nbt::Value::makeDouble(borderDiameter));
            double lerpTgt = (borderLerpTarget < 0 ? borderDiameter : borderLerpTarget);
            std::int64_t lerpMs = (borderLerpMs < 0 ? 0 : borderLerpMs);
            wb.set("SizeLerpTarget", nbt::Value::makeDouble(lerpTgt));
            wb.set("SizeLerpTime", nbt::Value::makeLong(lerpMs));
            wb.set("SafeZone", nbt::Value::makeDouble(5.0));
            wb.set("DamagePerBlock", nbt::Value::makeDouble(0.2));
            wb.set("DamageBuffer", nbt::Value::makeDouble(5.0));
            wb.set("WarningBlocks", nbt::Value::makeInt(5));
            wb.set("WarningTime", nbt::Value::makeInt(15));
            data.set("WorldBorder", wb);
        }
        if (!data.get("WanderingTraderSpawnDelay"))
            data.set("WanderingTraderSpawnDelay", nbt::Value::makeInt(0));
        if (!data.get("WanderingTraderSpawnChance"))
            data.set("WanderingTraderSpawnChance", nbt::Value::makeInt(25));
        if (!data.get("WanderingTraderId"))
            data.set("WanderingTraderId", nbt::Value::makeCompound());
        if (!data.get("WasModded"))
            data.set("WasModded", nbt::Value::makeByte(0));
        if (!data.get("allowCommands"))
            data.set("allowCommands", nbt::Value::makeByte(1));
        if (!data.get("GameType"))
            data.set("GameType", nbt::Value::makeInt(0));
        // ForcedChunks — Yarn ForcedChunkState: only ticket FORCED set, truncate 256 (W17 strict)
        {
            nbt::Value fc = nbt::Value::makeList(nbt::Long);
            // W17: persist ticketManager forced set via World::forcedChunkKeys(), not allChunkKeys scan. Vanilla ForcedChunkState stores
            // ChunkPos.toLong(x,z) long[] with 256 limit. W17 strict: SPAWN (spawn chunk loader) must NOT be persisted here; only FORCED
            // from /forceload. forcedChunkKeys() already returns only FORCED (addSpawnTicket no longer pollutes set).
            auto forced = world.forcedChunkKeys();
            if (forced.size() > constants::kMaxForcedChunks) {
                std::fprintf(stderr, "[WorldDataManager] ForcedChunks %zu >%d, truncating to %d (vanilla limit)\n", forced.size(), constants::kMaxForcedChunks, constants::kMaxForcedChunks);
                forced.resize(constants::kMaxForcedChunks);
            }
            for (auto k : forced) fc.list.push_back(nbt::Value::makeLong(k));
            // Note: spawn 5x5 SPAWN tickets are recreated on startup via GameServer::init (SPAWN level 31)
            // and are NOT persisted here (W17) to avoid inflating maxLoadedChunks (W19) with implicit forced.
            data.set("ForcedChunks", fc);
        }
        // W16 single level.dat: DragonFight Gateways 12 intact (vanilla 1.21.4 Data.DragonFight) Yarn PrimaryLevelData
        // EnderDragonFight.Data 12 Gateways, single world/level.dat 26.1 moves to ender_dragon_fight.dat, but 1.21.4 keeps it in level.dat.
        if (!data.get("DragonFight")) {
            nbt::Value dragon = nbt::Value::makeCompound();
            dragon.set("DragonKilled", nbt::Value::makeByte(0));
            dragon.set("PreviouslyKilled", nbt::Value::makeByte(0));
            nbt::Value gw = nbt::Value::makeList(nbt::Int);
            gw.list.reserve(12);
            for (int i = 0; i < 12; ++i) gw.list.push_back(nbt::Value::makeInt(0));
            dragon.set("Gateways", std::move(gw));
            dragon.set("NeedsStateScanning", nbt::Value::makeByte(0));
            nbt::Value exitPos = nbt::Value::makeCompound();
            exitPos.set("X", nbt::Value::makeInt(0));
            exitPos.set("Y", nbt::Value::makeInt(65));
            exitPos.set("Z", nbt::Value::makeInt(0));
            dragon.set("ExitPortalLocation", std::move(exitPos));
            data.set("DragonFight", std::move(dragon));
        }
        auto ensureGr = [&](nbt::Value &gr){
            auto ensure = [&](const char* k, const char* v){ if (!gr.get(k)) gr.set(k, nbt::Value::makeString(v)); };
            ensure("doFireTick","true"); ensure("mobGriefing","true"); ensure("keepInventory","false");
            ensure("doMobSpawning","true"); ensure("doDaylightCycle","true"); ensure("randomTickSpeed","3");
            ensure("doWeatherCycle","true"); ensure("announceAdvancements","true"); ensure("naturalRegeneration","true");
            ensure("doImmediateRespawn","false"); ensure("drowningDamage","true"); ensure("fallDamage","true");
            ensure("fireDamage","true"); ensure("freezeDamage","true"); ensure("doTileDrops","true");
            ensure("doMobLoot","true"); ensure("doEntityDrops","true"); ensure("commandBlockOutput","true");
            ensure("logAdminCommands","true"); ensure("showDeathMessages","true"); ensure("sendCommandFeedback","true");
            ensure("reducedDebugInfo","false"); ensure("spectatorsGenerateChunks","true"); ensure("spawnRadius","10");
            ensure("maxEntityCramming","24"); ensure("doLimitedCrafting","false"); ensure("maxCommandChainLength","65536");
            ensure("disableElytraMovementCheck","false"); ensure("disableRaids","false"); ensure("doInsomnia","true");
            ensure("doPatrolSpawning","true"); ensure("doTraderSpawning","true"); ensure("doWardenSpawning","true");
            ensure("forgiveDeadPlayers","true"); ensure("universalAnger","false"); ensure("playersSleepingPercentage","100");
            ensure("blockExplosionDropDecay","true");
            ensure("mobExplosionDropDecay","true"); ensure("tntExplosionDropDecay","false");
            ensure("waterSourceConversion","true"); ensure("lavaSourceConversion","false");
            ensure("globalSoundEvents","true"); ensure("snowAccumulationHeight","1");
            ensure("commandModificationBlockLimit","32768"); ensure("maxCommandForkCount","65536");
            ensure("doVinesSpread","true"); ensure("enderPearlsVanishOnDeath","true");
            ensure("projectilesCanBreakBlocks","true"); ensure("playersNetherPortalDefaultDelay","80");
            ensure("playersNetherPortalCreativeDelay","0"); ensure("disablePlayerMovementCheck","false");
            ensure("spawnChunkRadius","2");
        };
        if (!data.get("GameRules")) {
            nbt::Value gr = nbt::Value::makeCompound();
            ensureGr(gr);
            data.set("GameRules", gr);
        } else {
            if (auto* gr = data.get("GameRules")) ensureGr(*gr);
        }
        if (provide_) provide_(data);
        root.set("Data", data);
        if (!saveLevelData(root)) return false;
        {
            std::lock_guard rootLock(levelRootMtx_);
            loadedLevelRoot_ = std::move(root);
            hasLoadedLevelRoot_ = true;
        }
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[WorldDataManager] level data save failed: %s\n", e.what());
        return false;
    } catch (...) {
        std::fprintf(stderr, "[WorldDataManager] level data save failed\n");
        return false;
    }
}

bool WorldDataManager::tryLoadFile(const std::string& path, World& world, std::string& difficultyOut,
                       double& borderDiameterOut, double& borderCXOut, double& borderCZOut,
                       double* borderLerpTargetOut, std::int64_t* borderLerpMsOut) {
    std::lock_guard lock(fileMutex());
    bool stateCommitStarted = false;
    try {
        std::vector<std::uint8_t> bytes;
        std::error_code readError;
        const auto readStatus = storage_file_ops::readRegularFileBounded(
            path, WorldDataManager::kMaxLevelDataBytes, bytes, readError);
        if (readStatus == storage_file_ops::BoundedReadStatus::TooLarge) {
            std::fprintf(stderr, "[WorldDataManager] refusing oversized level data: %s\n",
                         path.c_str());
            return false;
        }
        if (readStatus != storage_file_ops::BoundedReadStatus::Read ||
            bytes.empty())
            return false;
        ReadBuffer in(bytes);
        nbt::Parser parser(in);
        nbt::Value root = parser.readFileRoot();
        const auto* d = root.get("Data");
        if (!d) return false;
        if (!validLevelDataShape(*d)) return false;
        // version check (DataFixerUpper-like bump, in-memory only)
        checkAndFixVersion(root);
        d = root.get("Data");
        if (!d || !validLevelDataShape(*d)) return false;
        // Retain the accepted document, not just fields this implementation
        // currently understands. Future save cycles merge live values into
        // this template so vanilla, datapack, and mod extension NBT survives.
        nbt::Value acceptedRoot = root;
        std::optional<std::uint64_t> loadedSeed;
        if (const auto* storedSeed = d->get("RandomSeed")) {
            std::int64_t parsedSeed = 0;
            if (!readInt64(*storedSeed, parsedSeed)) return false;
            loadedSeed = static_cast<std::uint64_t>(parsedSeed);
        }

        // Decode the whole candidate into temporaries first. Recovery may try
        // another file after this one is rejected (for example, because its
        // seed cannot replace a world whose chunk loading has already begun).
        // No output parameter or live World field may be changed on that path.
        std::string candidateDifficulty = difficultyOut;
        std::optional<World::SpawnPoint> candidateSpawn;
        std::optional<double> candidateBorderDiameter;
        std::optional<double> candidateBorderCX;
        std::optional<double> candidateBorderCZ;
        std::optional<double> candidateBorderLerpTarget;
        std::optional<std::int64_t> candidateBorderLerpMs;
        std::vector<std::pair<std::int32_t, std::int32_t>> candidateForcedChunks;
        bool hasCandidateForcedChunks = false;

        const auto* sx = d->get("SpawnX");
        const auto* sy = d->get("SpawnY");
        const auto* sz = d->get("SpawnZ");
        if ((sx || sy || sz) && (!sx || !sy || !sz)) return false;
        if (sx && sy && sz) {
            std::int32_t spawnX = 0, spawnY = 0, spawnZ = 0;
            if (!readInt32(*sx, spawnX) || !readInt32(*sy, spawnY) ||
                !readInt32(*sz, spawnZ)) return false;
            float angle = 0.f;
            if (const auto* a = d->get("SpawnAngle")) {
                double parsedAngle = 0;
                if (!readDouble(*a, parsedAngle)) return false;
                angle = static_cast<float>(parsedAngle);
            }
            candidateSpawn = World::SpawnPoint{spawnX, spawnY, spawnZ, angle};
        }
        if (const auto* diff = d->get("Difficulty")) {
            if (diff->tag == nbt::Byte) {
                int v = diff->b;
                if (v == 0) candidateDifficulty = "peaceful";
                else if (v == 1) candidateDifficulty = "easy";
                else if (v == 2) candidateDifficulty = "normal";
                else if (v == 3) candidateDifficulty = "hard";
            } else if (diff->tag == nbt::String) {
                candidateDifficulty = diff->str;
            }
        }
        if (const auto* wb = d->get("WorldBorder")) {
            double parsed = 0;
            if (auto* cx = wb->get("CenterX")) {
                if (!readDouble(*cx, parsed)) return false;
                candidateBorderCX = parsed;
            }
            if (auto* cz = wb->get("CenterZ")) {
                if (!readDouble(*cz, parsed)) return false;
                candidateBorderCZ = parsed;
            }
            if (auto* borderSize = wb->get("Size")) {
                if (!readDouble(*borderSize, parsed)) return false;
                candidateBorderDiameter = parsed;
            }
            if (borderLerpTargetOut || borderLerpMsOut) {
                double tgt = candidateBorderDiameter.value_or(borderDiameterOut);
                std::int64_t ms = 0;
                if (auto* lt = wb->get("SizeLerpTarget")) {
                    if (!readDouble(*lt, tgt)) return false;
                }
                if (auto* lm = wb->get("SizeLerpTime")) {
                    if (!readInt64(*lm, ms)) return false;
                }
                if (borderLerpTargetOut) candidateBorderLerpTarget = tgt;
                if (borderLerpMsOut) candidateBorderLerpMs = ms;
            }
        }
        // ForcedChunks — Yarn ForcedChunkState: restore LongSet with 256 cap and sign-correct ChunkPos.toLong
        if (const auto* fc = d->get("ForcedChunks")) {
            hasCandidateForcedChunks = true;
            if (fc->list.size() > constants::kMaxForcedChunks) {
                std::fprintf(stderr, "[WorldDataManager] load ForcedChunks %zu >%d, truncating to %d\n", fc->list.size(), constants::kMaxForcedChunks, constants::kMaxForcedChunks);
            }
            size_t count = 0;
            candidateForcedChunks.reserve(
                std::min<std::size_t>(fc->list.size(), constants::kMaxForcedChunks));
            for (const auto& v : fc->list) {
                if (count >= constants::kMaxForcedChunks) break;
                std::int64_t key = 0;
                if (v.tag == nbt::Long) key = v.l;
                else if (v.tag == nbt::Int) key = v.i;
                else continue;
                // ChunkPos.toLong: (long)x<<32 | (z & 0xffffffffL) — sign-extend via uint32_t cast already
                std::int32_t cx = static_cast<std::int32_t>(key >> 32);
                std::int32_t cz = static_cast<std::int32_t>(key & 0xFFFFFFFFLL);
                candidateForcedChunks.emplace_back(cx, cz);
                ++count;
            }
        }

        // setSeed() is the only remaining candidate rejection point that can
        // depend on live World state. It is deliberately checked before any
        // other candidate value is committed, so recovery can safely continue
        // to level.dat.new or level.dat_old without leaking rejected metadata.
        if (loadedSeed) {
            // setSeed may allocate/rebuild world-generation state, so an
            // exception from it must also abort startup rather than trigger
            // fallback after a potentially partial mutation.
            stateCommitStarted = true;
            if (!world.setSeed(*loadedSeed)) {
                // World::setSeed returns false only before touching terrain or
                // worldgen state, so this is a safe candidate rejection.
                stateCommitStarted = false;
                std::fprintf(stderr,
                             "[WorldDataManager] refusing seed replacement after chunk generation\n");
                return false;
            }
        }

        // From here onward an exception is not a corrupt-candidate result:
        // state may already be partly committed, and trying another file
        // would combine two saves. Let startup fail closed instead.
        stateCommitStarted = true;
        if (candidateSpawn) world.setSpawnPoint(*candidateSpawn);
        difficultyOut.swap(candidateDifficulty);
        if (candidateBorderDiameter) borderDiameterOut = *candidateBorderDiameter;
        if (candidateBorderCX) borderCXOut = *candidateBorderCX;
        if (candidateBorderCZ) borderCZOut = *candidateBorderCZ;
        if (borderLerpTargetOut && candidateBorderLerpTarget)
            *borderLerpTargetOut = *candidateBorderLerpTarget;
        if (borderLerpMsOut && candidateBorderLerpMs)
            *borderLerpMsOut = *candidateBorderLerpMs;
        if (hasCandidateForcedChunks) {
            world.clearForcedChunks();
            for (const auto& [cx, cz] : candidateForcedChunks)
                world.restoreForcedChunk(cx, cz);
        }

        if (consume_) consume_(*d);
        {
            std::lock_guard rootLock(levelRootMtx_);
            loadedLevelRoot_ = std::move(acceptedRoot);
            hasLoadedLevelRoot_ = true;
        }
        return true;
    } catch (const std::exception& e) {
        if (stateCommitStarted) {
            std::fprintf(stderr,
                         "[WorldDataManager] accepted level data could not be applied (%s): %s\n",
                         path.c_str(), e.what());
            throw;
        }
        std::fprintf(stderr, "[WorldDataManager] level data load failed (%s): %s\n",
                     path.c_str(), e.what());
        return false;
    } catch (...) {
        if (stateCommitStarted) {
            std::fprintf(stderr,
                         "[WorldDataManager] accepted level data could not be applied (%s)\n",
                         path.c_str());
            throw;
        }
        std::fprintf(stderr, "[WorldDataManager] level data load failed (%s)\n", path.c_str());
        return false;
    }
}

bool WorldDataManager::loadWithRecovery(World& world, std::string& difficultyOut,
                          double& borderDiameterOut, double& borderCXOut, double& borderCZOut,
                          double* borderLerpTargetOut, std::int64_t* borderLerpMsOut,
                          RecoveryResult& out) {
    std::lock_guard lock(fileMutex());
    out = RecoveryResult{};
    if (!ownsLevelData()) {
        out.logLines.emplace_back(
            "[recovery] dimension storage has no level.dat; using shared world metadata");
        lastRecovery_ = out;
        return false;
    }
    const std::string dat = dir_ + "/level.dat";
    const std::string datNew = dir_ + "/level.dat.new";
    const std::string old = dir_ + "/level.dat_old";
    char line[256];
    if (tryLoadFile(dat, world, difficultyOut, borderDiameterOut, borderCXOut, borderCZOut,
                    borderLerpTargetOut, borderLerpMsOut)) {
        out.src = LevelSource::Dat;
        out.ok = true;
        std::snprintf(line, sizeof(line), "[recovery] level source=%s ok=1", levelSourceName(out.src));
        out.logLines.emplace_back(line);
        lastRecovery_ = out;
        return true;
    }
    std::snprintf(line, sizeof(line),
                  "[recovery] level.dat unreadable, trying level.dat.new");
    out.logLines.emplace_back(line);
    std::fprintf(stderr, "[cppfm] level.dat corrupt, trying level.dat.new\n");
    if (tryLoadFile(datNew, world, difficultyOut, borderDiameterOut, borderCXOut, borderCZOut,
                    borderLerpTargetOut, borderLerpMsOut)) {
        out.src = LevelSource::DatNew;
        out.ok = true;
        std::snprintf(line, sizeof(line),
                      "[recovery] level source=%s ok=1 (promoting temporary)",
                      levelSourceName(out.src));
        out.logLines.emplace_back(line);

        // A complete .new is the last committed save candidate.  Quarantine
        // the failed primary before promoting it so recovery never destroys
        // the bytes that explain the original failure.
        if (const auto quarantined = quarantineFile(dat); !quarantined.empty()) {
            std::snprintf(line, sizeof(line),
                          "[recovery] quarantined level.dat -> %s",
                          quarantined.c_str());
            out.logLines.emplace_back(line);
        } else {
            std::error_code existsError;
            const bool datExists = std::filesystem::exists(dat, existsError);
            if (existsError || datExists) {
                // The in-memory world already came from the complete .new
                // candidate. Keep that candidate and the primary untouched
                // when the failed primary cannot be quarantined.
                std::snprintf(line, sizeof(line),
                              "[recovery] retained level.dat.new; primary quarantine failed%s",
                              existsError ? " while checking level.dat" : "");
                out.logLines.emplace_back(line);
                lastRecovery_ = out;
                return true;
            }
            std::snprintf(line, sizeof(line),
                          "[recovery] level.dat disappeared before quarantine");
            out.logLines.emplace_back(line);
        }
        std::error_code promoteError;
        if (!persistence_detail::replaceFile(datNew, dat, promoteError)) {
            std::snprintf(line, sizeof(line),
                          "[recovery] could not promote level.dat.new: %s",
                          promoteError.message().c_str());
            out.logLines.emplace_back(line);
        } else if (!persistence_detail::syncDirectory(
                       std::filesystem::path(dir_))) {
            out.durabilityUncertain = true;
            std::snprintf(line, sizeof(line),
                          "[recovery] promoted level.dat.new, but directory durability is uncertain");
            out.logLines.emplace_back(line);
        } else {
            std::snprintf(line, sizeof(line),
                          "[recovery] promoted level.dat.new -> level.dat");
            out.logLines.emplace_back(line);
        }
        lastRecovery_ = out;
        return true;
    }
    std::snprintf(line, sizeof(line),
                  "[recovery] level.dat.new unreadable, trying level.dat_old");
    out.logLines.emplace_back(line);
    std::fprintf(stderr, "[cppfm] level.dat.new corrupt, trying level.dat_old\n");
    if (tryLoadFile(old, world, difficultyOut, borderDiameterOut, borderCXOut, borderCZOut,
                    borderLerpTargetOut, borderLerpMsOut)) {
        out.src = LevelSource::DatOld;
        out.ok = true;
        std::snprintf(line, sizeof(line), "[recovery] level source=%s ok=1 (dat quarantined below)",
                      levelSourceName(out.src));
        out.logLines.emplace_back(line);
        // Preserve every failed candidate for forensics.  If a previous
        // incident already produced .corrupt, quarantineFile chooses a
        // numbered suffix instead of overwriting evidence.
        for (const auto& failed : {dat, datNew}) {
            if (const auto quarantined = quarantineFile(failed); !quarantined.empty()) {
                std::snprintf(line, sizeof(line),
                              "[recovery] quarantined %s -> %s",
                              failed.c_str(), quarantined.c_str());
                out.logLines.emplace_back(line);
            }
        }
        lastRecovery_ = out;
        return true;
    }
    std::snprintf(line, sizeof(line),
                  "[recovery] level.dat, level.dat.new and level.dat_old unreadable, generating fresh");
    out.logLines.emplace_back(line);
    std::fprintf(stderr, "[cppfm] level.dat, .new and _old unreadable, generating fresh\n");
    for (const auto& failed : {dat, datNew, old})
        (void)quarantineFile(failed);
    out.src = LevelSource::Fresh;
    out.ok = false; // caller generates a fresh world
    lastRecovery_ = out;
    return false;
}

bool WorldDataManager::loadLevelData(World& world, std::string& difficultyOut,
                       double& borderDiameterOut, double& borderCXOut, double& borderCZOut,
                       double* borderLerpTargetOut, std::int64_t* borderLerpMsOut) {
    RecoveryResult r;
    return loadWithRecovery(world, difficultyOut, borderDiameterOut, borderCXOut, borderCZOut,
                            borderLerpTargetOut, borderLerpMsOut, r);
}

} // namespace cppfm
