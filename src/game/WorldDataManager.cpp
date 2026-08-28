#include "WorldDataManager.hpp"
#include "World.hpp"
#include <filesystem>
#include <fstream>

namespace cppfm {

bool WorldDataManager::saveLevelDataWithProviders(std::int64_t worldTicks, std::int64_t dayTime, World& world,
                                    const std::string& difficulty,
                                    double borderDiameter, double borderCX, double borderCZ) {
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
            wb.set("SizeLerpTarget", nbt::Value::makeDouble(borderDiameter));
            wb.set("SizeLerpTime", nbt::Value::makeLong(0));
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
            // also include any additional forced chunks from world
            for (auto k : world.allChunkKeys()) if (world.isForcedKey(k)) {
                bool already=false;
                for (auto &v: fc.list) if (v.l==k) { already=true; break; }
                if (!already) fc.list.push_back(nbt::Value::makeLong(k));
            }
            data.set("ForcedChunks", fc);
        }
        // End dragon fight data (per-dim level.dat for The End)
        if (world.dimensionId() == 1 || world.levelType() == LevelType::End) {
            nbt::Value dragon = nbt::Value::makeCompound();
            dragon.set("DragonKilled", nbt::Value::makeByte(0));
            dragon.set("PreviouslyKilled", nbt::Value::makeByte(0));
            dragon.set("Gateways", nbt::Value::makeList(nbt::Int));
            data.set("DragonFight", dragon);
        }
        // GameRules: ensure all vanilla rules present
        if (!data.get("GameRules")) {
            nbt::Value gr = nbt::Value::makeCompound();
            gr.set("doFireTick", nbt::Value::makeString("true"));
            gr.set("doMobSpawning", nbt::Value::makeString("true"));
            gr.set("randomTickSpeed", nbt::Value::makeString("3"));
            data.set("GameRules", gr);
        }
        if (provide_) provide_(data);
        root.set("Data", data);
        return saveLevelData(root);
    } catch (...) { return false; }
}

bool WorldDataManager::loadLevelData(World& world, std::string& difficultyOut,
                       double& borderDiameterOut, double& borderCXOut, double& borderCZOut) {
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
