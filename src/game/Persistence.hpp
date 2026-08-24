// Persistence: Anvil region files + background flusher (plan.md Phase 1).
#pragma once
#include "World.hpp"
#include "Anvil.hpp"
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
        : world_(world), dir_(std::move(worldDir)), biome_(std::move(biomeKey)) {}

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
            if (!chunkFromNBT(root, out, {}, bio)) return false;
            if (!bio.empty()) {
                std::lock_guard lk(bioMtx_);
                biomeOverride_ = bio;
            }
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
                    nbt::Value root = chunkToNBT(cx, cz, c, bio);
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

    World& world_;
    std::string dir_;
    std::string biome_;
    std::optional<std::string> biomeOverride_;
    std::mutex bioMtx_;

    std::mutex dirtyMtx_;
    std::set<std::int64_t> dirty_;
    std::thread worker_;
    std::condition_variable cv_;
    std::mutex cvMtx_;
    std::atomic<bool> running_{false};
};

} // namespace cppfm
