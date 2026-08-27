// Persistence: Anvil region files + background flusher (plan.md Phase 1).
#pragma once
#include "World.hpp"
#include "Anvil.hpp"
#include "WorldDataManager.hpp"
#include <functional>
#include "RegionFile.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <set>
#include <thread>

namespace cppfm {

class Persistence {
public:
    Persistence(World& world, std::string worldDir, std::string biomeKey)
        : world_(world), dir_(std::move(worldDir)), worldDataManager_(dir_), biome_(std::move(biomeKey)) {}

    // ---- level.dat ----
    void setLevelStateProvider(
        std::function<void(nbt::Value& data)> provider,
        std::function<void(const nbt::Value& data)> consumer) {
        provideLevelState_ = std::move(provider);
        consumeLevelState_ = std::move(consumer);
        worldDataManager_.setLevelStateProvider(provideLevelState_, consumeLevelState_);
    }

    // Optional hooks: block-entity + entity NBT attached to saved chunks.
    void setChunkExtras(
        std::function<void(std::int32_t, std::int32_t, nbt::Value&)> writeFn,
        std::function<void(const nbt::Value&)> readFn) {
        writeExtras_ = std::move(writeFn);
        readExtras_ = std::move(readFn);
    }
    void setBiomeCodec(std::unordered_map<std::uint16_t, std::string> idxToKey,
                       std::int32_t defaultIdx) {
        biomeIdxToKey_ = std::move(idxToKey);
        defaultBiomeIndex_ = defaultIdx;
        for (auto& [k, v] : biomeIdxToKey_)
            biomeKeyToIdx_.emplace(v, k);
    }

    // plan6 §9: full persistence setters
    void setDifficulty(const std::string& d) { difficulty_ = d; }
    void setWorldBorder(double diameter, double cx=0, double cz=0) {
        worldBorderDiameter_ = diameter; worldBorderCenterX_=cx; worldBorderCenterZ_=cz;
    }
    std::string difficulty() const { return difficulty_; }
    double worldBorderDiameter() const { return worldBorderDiameter_; }
    double worldBorderCenterX() const { return worldBorderCenterX_; }
    double worldBorderCenterZ() const { return worldBorderCenterZ_; }

    // ---- level.dat ----
    // plan5 §1: full level.dat persistence — spawn, time, gamerules, weather.
    // plan6 §9: add Difficulty, WorldBorder, Version, WanderingTrader etc.
    // plan7: delegated to WorldDataManager with atomic rename + DataFixerUpper version check
    void saveLevelData(std::int64_t worldTicks = 0, std::int64_t dayTime = 0) {
        // Use WorldDataManager for atomic write + version handling
        worldDataManager_.setDirectory(dir_);
        worldDataManager_.setLevelStateProvider(provideLevelState_, consumeLevelState_);
        // Keep legacy inline fallback if manager fails, but primary is manager
        bool ok = worldDataManager_.saveLevelDataWithProviders(worldTicks, dayTime, world_,
                                                               difficulty_, worldBorderDiameter_,
                                                               worldBorderCenterX_, worldBorderCenterZ_);
        if (!ok) {
            // fallback: old direct write (should not happen)
            namespace nv = nbt;
            try {
                nv::Value root = nv::Value::makeCompound();
                nv::Value data = nv::Value::makeCompound();
                data.set("DataVersion", nv::Value::makeInt(kCurrentDataVersion));
                auto spawn = world_.spawnPoint();
                data.set("SpawnX", nv::Value::makeInt(spawn.x));
                data.set("SpawnY", nv::Value::makeInt(spawn.y));
                data.set("SpawnZ", nv::Value::makeInt(spawn.z));
                data.set("Time", nv::Value::makeLong(worldTicks));
                data.set("DayTime", nv::Value::makeLong(dayTime));
                data.set("LevelName", nv::Value::makeString("CppFabricMC World"));
                data.set("raining", nv::Value::makeByte(0));
                data.set("thundering", nv::Value::makeByte(0));
                {
                    int diffByte = 2;
                    if (difficulty_=="peaceful") diffByte=0;
                    else if (difficulty_=="easy") diffByte=1;
                    else if (difficulty_=="normal") diffByte=2;
                    else if (difficulty_=="hard") diffByte=3;
                    data.set("Difficulty", nv::Value::makeByte((std::int8_t)diffByte));
                    data.set("DifficultyLocked", nv::Value::makeByte(0));
                }
                {
                    nv::Value ver = nv::Value::makeCompound();
                    ver.set("Name", nv::Value::makeString("1.21.4"));
                    ver.set("Id", nv::Value::makeInt(kCurrentDataVersion));
                    ver.set("Snapshot", nv::Value::makeByte(0));
                    ver.set("Series", nv::Value::makeString("main"));
                    data.set("Version", ver);
                }
                {
                    nv::Value wb = nv::Value::makeCompound();
                    wb.set("CenterX", nv::Value::makeDouble(worldBorderCenterX_));
                    wb.set("CenterZ", nv::Value::makeDouble(worldBorderCenterZ_));
                    wb.set("Size", nv::Value::makeDouble(worldBorderDiameter_));
                    wb.set("SizeLerpTarget", nv::Value::makeDouble(worldBorderDiameter_));
                    wb.set("SizeLerpTime", nv::Value::makeLong(0));
                    wb.set("SafeZone", nv::Value::makeDouble(5.0));
                    wb.set("DamagePerBlock", nv::Value::makeDouble(0.2));
                    wb.set("DamageBuffer", nv::Value::makeDouble(5.0));
                    wb.set("WarningBlocks", nv::Value::makeInt(5));
                    wb.set("WarningTime", nv::Value::makeInt(15));
                    data.set("WorldBorder", wb);
                }
                data.set("WanderingTraderSpawnDelay", nv::Value::makeInt(0));
                data.set("WanderingTraderSpawnChance", nv::Value::makeInt(25));
                data.set("WanderingTraderId", nv::Value::makeCompound());
                data.set("WasModded", nv::Value::makeByte(0));
                data.set("allowCommands", nv::Value::makeByte(1));
                data.set("GameType", nv::Value::makeInt(1));
                if (provideLevelState_) provideLevelState_(data);
                root.set("Data", data);
                WriteBuffer out;
                nv::writeFileRoot(out, root);
                std::filesystem::create_directories(dir_);
                std::ofstream f(dir_ + "/level.dat", std::ios::binary);
                f.write(reinterpret_cast<const char*>(out.data.data()), out.data.size());
            } catch (...) {}
        }
    }
    void loadLevelData() {
        worldDataManager_.setDirectory(dir_);
        worldDataManager_.setLevelStateProvider(provideLevelState_, consumeLevelState_);
        // try via manager (handles DataFixerUpper version check + atomic read)
        bool ok = worldDataManager_.loadLevelData(world_, difficulty_, worldBorderDiameter_, worldBorderCenterX_, worldBorderCenterZ_);
        if (ok) return;
        // fallback legacy read
        try {
            std::ifstream f(dir_ + "/level.dat", std::ios::binary);
            if (!f) return;
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                             std::istreambuf_iterator<char>());
            ReadBuffer in(bytes);
            nbt::Parser parser(in);
            nbt::Value root = parser.readFileRoot();
            const auto* d = root.get("Data");
            if (!d) return;
            if (const auto* sx = d->get("SpawnX"))
                if (const auto* sy = d->get("SpawnY"))
                    if (const auto* sz = d->get("SpawnZ"))
                        world_.setSpawnPoint({sx->i, sy->i, sz->i});
            if (const auto* diff = d->get("Difficulty")) {
                int v = diff->b;
                if (v==0) difficulty_="peaceful";
                else if (v==1) difficulty_="easy";
                else if (v==2) difficulty_="normal";
                else if (v==3) difficulty_="hard";
                else if (diff->tag==nbt::String) difficulty_=diff->str;
            }
            if (const auto* wb = d->get("WorldBorder")) {
                if (auto* cx = wb->get("CenterX")) worldBorderCenterX_ = cx->d;
                if (auto* cz = wb->get("CenterZ")) worldBorderCenterZ_ = cz->d;
                if (auto* sz = wb->get("Size")) {
                    if (sz->tag==nbt::Double) worldBorderDiameter_ = sz->d;
                    else if (sz->tag==nbt::Float) worldBorderDiameter_ = sz->f;
                    else if (sz->tag==nbt::Int) worldBorderDiameter_ = sz->i;
                    else if (sz->tag==nbt::Long) worldBorderDiameter_ = (double)sz->l;
                }
            }
            if (const auto* ds = d->get("Difficulty")) {
                if (ds->tag==nbt::String) difficulty_ = ds->str;
            }
            if (consumeLevelState_) consumeLevelState_(*d);
            if (const auto* ds2 = d->get("Difficulty")) {
                if (ds2->tag==nbt::String) difficulty_ = ds2->str;
            }
        } catch (...) {}
    }

    void start() {
        std::filesystem::create_directories(dir_ + "/region");
        world_.setLoader([this](std::int32_t cx, std::int32_t cz, Chunk& c) {
            return loadChunk(cx, cz, c);
        });
        world_.setOnEdit([this](std::int32_t cx, std::int32_t cz) { markDirty(cx, cz); });
        running_ = true;
        worker_ = std::thread([this] { loop(); });
    }
    void stop() {
        if (!running_.exchange(false)) return;
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        flushOnce();                                   // final save
    }

    // World loader: read chunk from its region file; false = not stored.
    bool loadChunk(std::int32_t cx, std::int32_t cz, Chunk& out) {
        try {
            RegionFile rf(regionPath(cx, cz));
            auto bytes = rf.load(cx & 31, cz & 31);
            if (bytes.empty()) return false;
            ReadBuffer in(bytes);
            nbt::Parser parser(in);
            nbt::Value root = parser.readFileRoot();
            std::string bio;
            if (!chunkFromNBT(root, out, {}, bio,
                              [this](const std::string& k) -> std::int32_t {
                                  auto it = biomeKeyToIdx_.find(k);
                                  return it != biomeKeyToIdx_.end()
                                             ? it->second : -1;
                              }))
                return false;
            if (!bio.empty()) {
                std::lock_guard lk(bioMtx_);
                biomeOverride_ = bio;
            }
            if (readExtras_) readExtras_(root);
            return true;
        } catch (...) {
            return false;                              // corrupt/foreign chunk: regenerate
        }
    }

    void markDirty(std::int32_t cx, std::int32_t cz) {
        if (!running_) return;
        {
            std::lock_guard lk(dirtyMtx_);
            dirty_.insert(chunkKey(cx, cz));
        }
        cv_.notify_all();
    }

    void flushOnce() {
        std::set<std::int64_t> batch;
        {
            std::lock_guard lk(dirtyMtx_);
            batch.swap(dirty_);
        }
        for (auto k : batch) {
            const std::int32_t cx = static_cast<std::int32_t>(k >> 32);
            const std::int32_t cz = static_cast<std::int32_t>(k & 0xFFFFFFFFLL);
            const std::string bio = [this] {
                std::lock_guard lk(bioMtx_);
                return biomeOverride_.value_or(biome_);
            }();
            world_.withChunk(cx, cz, [&](const Chunk& c) {
                try {
                    nbt::Value root = chunkToNBT(cx, cz, c, bio,
                                                 &biomeIdxToKey_);
                    if (writeExtras_) writeExtras_(cx, cz, root);
                    WriteBuffer out;
                    nbt::writeFileRoot(out, root);
                    RegionFile rf(regionPath(cx, cz));
                    rf.store(cx & 31, cz & 31, out.data);
                    std::fprintf(stderr, "[cppfm] saved r.%d.%d mca (%zu bytes nbt)\n",
                                 cx >> 5, cz >> 5, out.data.size());
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "[cppfm] SAVE ERROR chunk %d,%d: %s\n", cx, cz, e.what());
                }
            });
        }
    }
    bool isDirty(std::int32_t cx, std::int32_t cz) {
        std::lock_guard lk(dirtyMtx_);
        return dirty_.count(chunkKey(cx, cz)) != 0;
    }
    bool flushChunk(std::int32_t cx, std::int32_t cz) {
        {
            std::lock_guard lk(dirtyMtx_);
            dirty_.erase(chunkKey(cx, cz));
        }
        const std::string bio = [this] {
            std::lock_guard lk(bioMtx_);
            return biomeOverride_.value_or(biome_);
        }();
        bool ok = false;
        world_.withChunk(cx, cz, [&](const Chunk& c) {
            try {
                nbt::Value root = chunkToNBT(cx, cz, c, bio, &biomeIdxToKey_);
                if (writeExtras_) writeExtras_(cx, cz, root);
                WriteBuffer out;
                nbt::writeFileRoot(out, root);
                RegionFile rf(regionPath(cx, cz));
                rf.store(cx & 31, cz & 31, out.data);
                ok = true;
                std::fprintf(stderr, "[cppfm] flushChunk %d,%d (%zu bytes)\n", cx, cz, out.data.size());
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[cppfm] FLUSH CHUNK ERROR %d,%d: %s\n", cx, cz, e.what());
            }
        });
        return ok;
    }

private:
    void loop() {
        std::unique_lock lk(cvMtx_);
        while (running_) {
            cv_.wait_for(lk, std::chrono::seconds(3));
            if (!running_) break;
            flushOnce();
        }
    }
    std::string regionPath(std::int32_t cx, std::int32_t cz) const {
        return dir_ + "/region/r." + std::to_string(cx >> 5) + "." +
               std::to_string(cz >> 5) + ".mca";
    }

    std::string difficulty_ = "normal";
    double worldBorderDiameter_ = 29999984;
    double worldBorderCenterX_ = 0, worldBorderCenterZ_ = 0;
    World& world_;
    std::string dir_;
    WorldDataManager worldDataManager_;
    std::string biome_;
    std::optional<std::string> biomeOverride_;
    std::mutex bioMtx_;
    std::unordered_map<std::uint16_t, std::string> biomeIdxToKey_;
    std::unordered_map<std::string, std::uint16_t> biomeKeyToIdx_;
    std::int32_t defaultBiomeIndex_ = 0;
    std::function<void(std::int32_t, std::int32_t, nbt::Value&)> writeExtras_;
    std::function<void(const nbt::Value&)> readExtras_;
public:
    std::function<void(nbt::Value&)> provideLevelState_;
    std::function<void(const nbt::Value&)> consumeLevelState_;
private:

    std::mutex dirtyMtx_;
    std::set<std::int64_t> dirty_;
    std::thread worker_;
    std::condition_variable cv_;
    std::mutex cvMtx_;
    std::atomic<bool> running_{false};
};

} // namespace cppfm
