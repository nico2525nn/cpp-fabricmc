#include "WorldDataManager.hpp"
#include "World.hpp"
#include <filesystem>
#include <fstream>

namespace cppfm {

bool WorldDataManager::saveLevelDataWithProviders(std::int64_t worldTicks, std::int64_t dayTime, World& world,
                                    const std::string& difficulty,
                                    double borderDiameter, double borderCX, double borderCZ,
                                    double borderLerpTarget, std::int64_t borderLerpMs) {
    try {
        nbt::Value root = nbt::Value::makeCompound();
        nbt::Value data = nbt::Value::makeCompound();
        data.set("DataVersion", nbt::Value::makeInt(kCurrentDataVersion));
        auto spawn = world.spawnPoint();
        data.set("SpawnX", nbt::Value::makeInt(spawn.x));
        data.set("SpawnY", nbt::Value::makeInt(spawn.y));
        data.set("SpawnZ", nbt::Value::makeInt(spawn.z));
        data.set("Time", nbt::Value::makeLong(worldTicks));
        data.set("DayTime", nbt::Value::makeLong(dayTime));
        data.set("LevelName", nbt::Value::makeString("CppFabricMC World"));
        data.set("raining", nbt::Value::makeByte(0));
        data.set("thundering", nbt::Value::makeByte(0));
        {
            int diffByte = 2;
            if (difficulty=="peaceful") diffByte=0;
            else if (difficulty=="easy") diffByte=1;
            else if (difficulty=="normal") diffByte=2;
            else if (difficulty=="hard") diffByte=3;
            data.set("Difficulty", nbt::Value::makeByte((std::int8_t)diffByte));
            data.set("DifficultyLocked", nbt::Value::makeByte(0));
        }
        {
            nbt::Value ver = nbt::Value::makeCompound();
            ver.set("Name", nbt::Value::makeString("1.21.4"));
            ver.set("Id", nbt::Value::makeInt(kCurrentDataVersion));
            ver.set("Snapshot", nbt::Value::makeByte(0));
            ver.set("Series", nbt::Value::makeString("main"));
            data.set("Version", ver);
        }
        {
            nbt::Value wb = nbt::Value::makeCompound();
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
        data.set("WanderingTraderSpawnDelay", nbt::Value::makeInt(0));
        data.set("WanderingTraderSpawnChance", nbt::Value::makeInt(25));
        data.set("WanderingTraderId", nbt::Value::makeCompound());
        data.set("WasModded", nbt::Value::makeByte(0));
        data.set("allowCommands", nbt::Value::makeByte(1));
        data.set("GameType", nbt::Value::makeInt(1));
        // ForcedChunks (spawn chunks + forced): 5x5 around spawn, stored as long array for vanilla compat
        {
            nbt::Value fc = nbt::Value::makeList(nbt::Long);
            auto sp = world.spawnPoint();
            int scx = sp.x >> 4, scz = sp.z >> 4;
            for (int dz=-2; dz<=2; ++dz) for (int dx=-2; dx<=2; ++dx) {
                std::int64_t key = (static_cast<std::int64_t>(static_cast<std::uint32_t>(scx+dx))<<32) | static_cast<std::uint32_t>(scz+dz);
                fc.list.push_back(nbt::Value::makeLong(key));
            }
            // also include any additional forced chunks from world — W17 ticketManager persistence
            for (auto k : world.forcedChunkKeys()) {
                bool already=false;
                for (auto &v: fc.list) if (v.l==k) { already=true; break; }
                if (!already) fc.list.push_back(nbt::Value::makeLong(k));
            }
            for (auto k : world.ticketManager().allTicketKeys()) {
                if (world.ticketManager().getMinLevel(static_cast<std::int32_t>(k>>32), static_cast<std::int32_t>(k & 0xFFFFFFFFLL)) > 31) continue;
                bool already=false;
                for (auto &v: fc.list) if (v.l==k) { already=true; break; }
                if (!already) fc.list.push_back(nbt::Value::makeLong(k));
            }
            data.set("ForcedChunks", fc);
        }
        // End dragon fight data — single level.dat must contain DragonFight for vanilla compat (W16)
        // Always write for overworld single-file level.dat; vanilla stores DragonFight in world/level.dat even though End is dim 1.
        {
            nbt::Value dragon = nbt::Value::makeCompound();
            dragon.set("DragonKilled", nbt::Value::makeByte(0));
            dragon.set("PreviouslyKilled", nbt::Value::makeByte(0));
            dragon.set("Gateways", nbt::Value::makeList(nbt::Int));
            data.set("DragonFight", dragon);
        }
        // GameRules: ensure all vanilla rules present — 37 defaults (strict)
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
            ensure("commandModificationBlockLimit","32768"); ensure("maxBlockModifications","32768");
            ensure("maxCommandForkCount","65536"); ensure("doVinesSpread","true");
            ensure("enderPearlsVanishOnDeath","true"); ensure("projectilesCanBreakBlocks","true");
            ensure("playersNetherPortalDefaultDelay","80"); ensure("playersNetherPortalCreativeDelay","0");
            ensure("spawnChunkRadius","2"); ensure("spawnerBlocksEnabled","true");
            ensure("disablePlayerMovementCheck","false");
        };
        if (!data.get("GameRules")) {
            nbt::Value gr = nbt::Value::makeCompound();
            ensureGr(gr);
            data.set("GameRules", gr);
        } else {
            if (auto* gr = data.get("GameRules")) ensureGr(*const_cast<nbt::Value*>(gr));
        }
        if (provide_) provide_(data);
        root.set("Data", data);
        return saveLevelData(root);
    } catch (...) { return false; }
}

bool WorldDataManager::loadLevelData(World& world, std::string& difficultyOut,
                       double& borderDiameterOut, double& borderCXOut, double& borderCZOut,
                       double* borderLerpTargetOut, std::int64_t* borderLerpMsOut) {
    try {
        nbt::Value root;
        if (!loadRaw(root)) return false;
        const auto* d = root.get("Data");
        if (!d) return false;
        if (const auto* sx = d->get("SpawnX"))
            if (const auto* sy = d->get("SpawnY"))
                if (const auto* sz = d->get("SpawnZ"))
                    world.setSpawnPoint({sx->i, sy->i, sz->i});
        if (const auto* diff = d->get("Difficulty")) {
            if (diff->tag == nbt::Byte) {
                int v = diff->b;
                if (v==0) difficultyOut="peaceful";
                else if (v==1) difficultyOut="easy";
                else if (v==2) difficultyOut="normal";
                else if (v==3) difficultyOut="hard";
            } else if (diff->tag == nbt::String) difficultyOut = diff->str;
        }
        if (const auto* wb = d->get("WorldBorder")) {
            if (auto* cx = wb->get("CenterX")) borderCXOut = cx->d;
            if (auto* cz = wb->get("CenterZ")) borderCZOut = cz->d;
            if (auto* sz = wb->get("Size")) {
                if (sz->tag==nbt::Double) borderDiameterOut = sz->d;
                else if (sz->tag==nbt::Float) borderDiameterOut = sz->f;
                else if (sz->tag==nbt::Int) borderDiameterOut = sz->i;
                else if (sz->tag==nbt::Long) borderDiameterOut = (double)sz->l;
            }
            if (borderLerpTargetOut || borderLerpMsOut) {
                double tgt = borderDiameterOut;
                std::int64_t ms = 0;
                if (auto* lt = wb->get("SizeLerpTarget")) {
                    if (lt->tag==nbt::Double) tgt = lt->d;
                    else if (lt->tag==nbt::Float) tgt = lt->f;
                    else if (lt->tag==nbt::Int) tgt = lt->i;
                    else if (lt->tag==nbt::Long) tgt = (double)lt->l;
                }
                if (auto* lm = wb->get("SizeLerpTime")) {
                    if (lm->tag==nbt::Long) ms = lm->l;
                    else if (lm->tag==nbt::Int) ms = lm->i;
                    else if (lm->tag==nbt::Double) ms = (std::int64_t)lm->d;
                }
                if (borderLerpTargetOut) *borderLerpTargetOut = tgt;
                if (borderLerpMsOut) *borderLerpMsOut = ms;
            }
        }
        if (const auto* ds = d->get("Difficulty")) {
            if (ds->tag==nbt::String) difficultyOut = ds->str;
        }
        // ForcedChunks (ChunkTicket SPAWN level 31) — restore spawn chunk loader tickets
        if (const auto* fc = d->get("ForcedChunks")) {
            world.clearForcedChunks();
            for (auto &v : fc->list) {
                std::int64_t key = 0;
                if (v.tag == nbt::Long) key = v.l;
                else if (v.tag == nbt::Int) key = v.i;
                else continue;
                std::int32_t cx = static_cast<std::int32_t>(key >> 32);
                std::int32_t cz = static_cast<std::int32_t>(key & 0xFFFFFFFFLL);
                world.restoreForcedChunk(cx, cz);
                // ensure chunk exists for spawn loader (defer generation to when needed, but pre-generate here if possible)
                // world.generateChunkIfMissing(cx, cz); // not needed during load to avoid recursion
            }
        }
        if (consume_) consume_(*d);
        if (const auto* ds2 = d->get("Difficulty")) {
            if (ds2->tag==nbt::String) difficultyOut = ds2->str;
        }
        return true;
    } catch (...) { return false; }
}

} // namespace cppfm
