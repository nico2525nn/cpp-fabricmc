// World: superflat world storage, generation, block get/set.
// Chunk model: 24 sections of 16x16x16 block state ids (flat arrays; simple & fast).
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include "generated/BlockStates.hpp"

namespace cppfm {

constexpr int kSectionsPerChunk = 24;      // 384 / 16
constexpr int kMinY = -64;
constexpr int kMaxY = kMinY + 384;         // 320
constexpr int kSeaLevelFlat = -63;         // observed on reference flat worlds

inline constexpr std::int64_t chunkKey(std::int32_t cx, std::int32_t cz) {
    return (static_cast<std::int64_t>(static_cast<std::uint32_t>(cx)) << 32)
         | static_cast<std::uint32_t>(cz);
}

struct Chunk {
    // layout: [section][yInSection][z][x]
    std::array<std::uint16_t, kSectionsPerChunk * 4096> blocks{};
    std::uint64_t revision = 0;

    static constexpr std::size_t index(int section, int yIn, int z, int x) {
        return (static_cast<std::size_t>(section) * 4096)
             + (static_cast<std::size_t>(yIn) * 256)
             + (static_cast<std::size_t>(z) * 16)
             + static_cast<std::size_t>(x);
    }
};

class World {
public:
    explicit World(std::string biomeKey) : biome_(std::move(biomeKey)) {}

    // NOTE: logically const (lazy generation); mutex is mutable
    // Double-checked, atomic lazy generation (safe under concurrent joins).
    void generateChunkIfMissing(std::int32_t cx, std::int32_t cz) const {
        {
            std::shared_lock lock(mutex_);
            if (chunks_.count(chunkKey(cx, cz))) return;
        }
        auto c = std::make_unique<Chunk>();
        fillFlat(*c);
        std::unique_lock lock(mutex_);
        chunks_.try_emplace(chunkKey(cx, cz), std::move(c));  // never replaces
    }
    // Runs fn(chunk) while holding the world read lock. Use for any access that
    // must not race with chunk replacement or edits.
    template <typename Fn>
    bool withChunk(std::int32_t cx, std::int32_t cz, Fn&& fn) const {
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it == chunks_.end()) return false;
        fn(*it->second);
        return true;
    }
    bool hasChunk(std::int32_t cx, std::int32_t cz) const {
        std::shared_lock lock(mutex_);
        return chunks_.count(chunkKey(cx, cz)) != 0;
    }

    std::uint16_t getBlock(std::int32_t x, std::int32_t y, std::int32_t z) const {
        if (y < kMinY || y >= kMaxY) return 0;
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end()) return 0;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        return it->second->blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)];
    }
    void setBlock(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
        if (y < kMinY || y >= kMaxY) return;
        generateChunkIfMissing(x >> 4, z >> 4);
        std::unique_lock lock(mutex_);
        auto& c = *chunks_.at(chunkKey(x >> 4, z >> 4));
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] = state;
        ++c.revision;
    }
    const Chunk* tryGet(std::int32_t cx, std::int32_t cz) const {
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        return it == chunks_.end() ? nullptr : it->second.get();
    }

    const std::string& biomeKey() const { return biome_; }

private:
    void fillFlat(Chunk& c) const {
        const auto& map = gen::blockNameToState();
        const std::uint16_t bedrock = static_cast<std::uint16_t>(map.at("minecraft:bedrock"));
        const std::uint16_t dirt    = static_cast<std::uint16_t>(map.at("minecraft:dirt"));
        const std::uint16_t grass   = static_cast<std::uint16_t>(map.at("minecraft:grass_block"));
        for (int y = kMinY; y <= -61; ++y) {
            const std::uint16_t st = (y == -64) ? bedrock : (y == -61 ? grass : dirt);
            const int sec = ((y - kMinY) >> 4), yi = (y - kMinY) & 15;
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x)
                    c.blocks[Chunk::index(sec, yi, z, x)] = st;
        }
    }

    mutable std::shared_mutex mutex_;
    mutable std::unordered_map<std::int64_t, std::unique_ptr<Chunk>> chunks_;
    std::string biome_;
};

} // namespace cppfm
