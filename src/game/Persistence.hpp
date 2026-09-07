// Persistence: Anvil region files + background flusher (plan.md Phase 1).
#pragma once
#include "World.hpp"
#include "Anvil.hpp"
#include "WorldDataManager.hpp"
#include "Constants.hpp"
#include <functional>
#include "RegionFile.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <utility>

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
        biomeKeyToIdx_.clear();
        for (auto& [k, v] : biomeIdxToKey_)
            biomeKeyToIdx_[v] = k;
    }

    void setDifficulty(const std::string& d) { difficulty_ = d; }
    void setWorldBorder(double diameter, double cx=0, double cz=0) {
        worldBorderDiameter_ = diameter; worldBorderCenterX_=cx; worldBorderCenterZ_=cz;
        worldBorderLerpFrom_ = diameter; worldBorderLerpTo_ = diameter; worldBorderLerpMs_ = 0;
        worldBorderLerpRemainingTicks_ = 0; worldBorderLerpTotalTicks_ = 0;
    }
    void setWorldBorderLerp(double from, double to, std::int64_t remainingTicks) {
        worldBorderLerpFrom_ = from; worldBorderLerpTo_ = to;
        worldBorderLerpMs_ = remainingTicks * 50;
        worldBorderLerpRemainingTicks_ = remainingTicks;
        worldBorderLerpTotalTicks_ = remainingTicks;
        worldBorderDiameter_ = from;
        if (remainingTicks <= 0) worldBorderDiameter_ = to;
    }
    // interpolate per tick — Yarn WorldBorder.tick() / interpolateSize
    bool tickWorldBorder() {
        if (worldBorderLerpRemainingTicks_ <= 0) return false;
        --worldBorderLerpRemainingTicks_;
        worldBorderLerpMs_ = worldBorderLerpRemainingTicks_ * 50;
        if (worldBorderLerpRemainingTicks_ <= 0) {
            worldBorderDiameter_ = worldBorderLerpTo_;
            return true;
        }
        double prog = 1.0 - double(worldBorderLerpRemainingTicks_) / double(worldBorderLerpTotalTicks_ > 0 ? worldBorderLerpTotalTicks_ : 1);
        if (prog < 0) prog = 0;
        if (prog > 1) prog = 1;
        worldBorderDiameter_ = worldBorderLerpFrom_ + (worldBorderLerpTo_ - worldBorderLerpFrom_) * prog;
        return true;
    }
    std::string difficulty() const { return difficulty_; }
    double worldBorderDiameter() const { return worldBorderDiameter_; }
    double worldBorderCenterX() const { return worldBorderCenterX_; }
    double worldBorderCenterZ() const { return worldBorderCenterZ_; }
    double worldBorderLerpFrom() const { return worldBorderLerpFrom_; }
    double worldBorderLerpTo() const { return worldBorderLerpTo_; }
    std::int64_t worldBorderLerpMs() const { return worldBorderLerpMs_; }
    std::int64_t worldBorderLerpRemainingTicks() const { return worldBorderLerpRemainingTicks_; }

    void saveLevelData(std::int64_t worldTicks = 0, std::int64_t dayTime = 0) {
        // W16 single level.dat: DIM dirs must not own level.dat
        if (isDimensionDirectory()) return;
        // Use WorldDataManager for atomic write + version handling
        worldDataManager_.setDirectory(dir_);
        worldDataManager_.setLevelStateProvider(provideLevelState_, consumeLevelState_);
        double lerpTgt = worldBorderLerpRemainingTicks_ > 0 ? worldBorderLerpTo_ : worldBorderDiameter_;
        std::int64_t lerpMs = worldBorderLerpMs_;
        bool ok = worldDataManager_.saveLevelDataWithProviders(worldTicks, dayTime, world_,
                                                               difficulty_, worldBorderDiameter_,
                                                               worldBorderCenterX_, worldBorderCenterZ_,
                                                               lerpTgt, lerpMs);
        if (!ok)
            std::fprintf(stderr, "[Persistence] level.dat save was rejected by WorldDataManager\n");
    }
    void loadLevelData() {
        // W16: DIM dirs never own level.dat
        if (isDimensionDirectory()) return;
        worldDataManager_.setDirectory(dir_);
        worldDataManager_.setLevelStateProvider(provideLevelState_, consumeLevelState_);
        // try via manager (handles DataFixerUpper version check + atomic read) — include lerp
        double lerpTgt = worldBorderDiameter_;
        std::int64_t lerpMs = 0;
        bool ok = worldDataManager_.loadLevelData(world_, difficulty_, worldBorderDiameter_, worldBorderCenterX_, worldBorderCenterZ_, &lerpTgt, &lerpMs);
        for (const auto& ln : worldDataManager_.lastRecovery().logLines)
            std::fprintf(stderr, "[cppfm]%s\n", ln.c_str());
        if (ok) {
            worldBorderLerpFrom_ = worldBorderDiameter_;
            worldBorderLerpTo_ = lerpTgt;
            worldBorderLerpMs_ = lerpMs;
            worldBorderLerpRemainingTicks_ = (lerpMs + 49) / 50;
            worldBorderLerpTotalTicks_ = worldBorderLerpRemainingTicks_;
            if (lerpMs == 0) { worldBorderLerpFrom_ = worldBorderDiameter_; worldBorderLerpTo_ = worldBorderDiameter_; }
            return;
        }
    }

    void start() {
        std::filesystem::create_directories(dir_ + "/region");
        if (running_.load(std::memory_order_acquire) || worker_.joinable()) return;
        world_.setLoader([this](std::int32_t cx, std::int32_t cz, Chunk& c) {
            return loadChunk(cx, cz, c);
        });
        world_.setOnEdit([this](std::int32_t cx, std::int32_t cz) { markDirty(cx, cz); });
        running_.store(true, std::memory_order_release);
        try {
            worker_ = std::thread([this] { loop(); });
        } catch (...) {
            running_.store(false, std::memory_order_release);
            throw;
        }
    }
    void stop() noexcept {
        const bool wasRunning = running_.exchange(false, std::memory_order_acq_rel);
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        bool pending = false;
        {
            std::lock_guard lk(dirtyMtx_);
            pending = !dirty_.empty();
        }
        if (wasRunning || pending) {
            try {
                flushOnce(); // final save
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[Persistence] final flush failed: %s\n", e.what());
            } catch (...) {
                std::fprintf(stderr, "[Persistence] final flush failed\n");
            }
        }
    }
    // stop() is idempotent and flushes the final dirty batch.
    ~Persistence() { stop(); }

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
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[Persistence] load chunk %d,%d failed: %s\n", cx, cz, e.what());
            return false;                              // corrupt/foreign chunk: regenerate
        } catch (...) {
            std::fprintf(stderr, "[Persistence] load chunk %d,%d failed\n", cx, cz);
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
            auto [cx, cz] = chunkKeyDecode(k);
            const std::string bio = [this] {
                std::lock_guard lk(bioMtx_);
                return biomeOverride_.value_or(biome_);
            }();
            bool saveFailed = false;
            const bool found = world_.withChunk(cx, cz, [&](const Chunk& c) {
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
                    saveFailed = true;
                    std::fprintf(stderr, "[cppfm] SAVE ERROR chunk %d,%d: %s\n", cx, cz, e.what());
                } catch (...) {
                    saveFailed = true;
                    std::fprintf(stderr, "[cppfm] SAVE ERROR chunk %d,%d: unknown error\n", cx, cz);
                }
            });
            if (found && saveFailed) {
                std::lock_guard lock(dirtyMtx_);
                dirty_.insert(k);
            }
        }
    }
    bool isDirty(std::int32_t cx, std::int32_t cz) {
        std::lock_guard lk(dirtyMtx_);
        return dirty_.count(chunkKey(cx, cz)) != 0;
    }
    void markClean(std::int32_t cx, std::int32_t cz) {
        std::lock_guard lk(dirtyMtx_);
        dirty_.erase(chunkKey(cx, cz));
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
        const bool found = world_.withChunk(cx, cz, [&](const Chunk& c) {
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
            } catch (...) {
                std::fprintf(stderr, "[cppfm] FLUSH CHUNK ERROR %d,%d\n", cx, cz);
            }
        });
        if (found && !ok) {
            std::lock_guard lk(dirtyMtx_);
            dirty_.insert(chunkKey(cx, cz));
        }
        return ok;
    }

private:
    void loop() {
        std::unique_lock lk(cvMtx_);
        while (running_.load(std::memory_order_acquire)) {
            cv_.wait_for(lk, std::chrono::seconds(3));
            if (!running_.load(std::memory_order_acquire)) break;
            try {
                flushOnce();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[Persistence] background flush failed: %s\n", e.what());
            } catch (...) {
                std::fprintf(stderr, "[Persistence] background flush failed\n");
            }
        }
    }
    bool isDimensionDirectory() const {
        const auto path = std::filesystem::path(dir_);
        for (const auto& component : path) {
            const auto name = component.string();
            if (name == "DIM-1" || name == "DIM1") return true;
        }
        return false;
    }
    std::string regionPath(std::int32_t cx, std::int32_t cz) const {
        return dir_ + "/region/r." + std::to_string(cx >> 5) + "." +
               std::to_string(cz >> 5) + ".mca";
    }

    std::string difficulty_ = "normal";
    double worldBorderDiameter_ = constants::kWorldBorderDiameter;
    double worldBorderCenterX_ = 0, worldBorderCenterZ_ = 0;
    double worldBorderLerpFrom_ = constants::kWorldBorderDiameter;
    double worldBorderLerpTo_ = constants::kWorldBorderDiameter;
    std::int64_t worldBorderLerpMs_ = 0;
    std::int64_t worldBorderLerpRemainingTicks_ = 0;
    std::int64_t worldBorderLerpTotalTicks_ = 0;
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
