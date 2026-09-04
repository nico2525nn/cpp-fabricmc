// "完全な明るさ伝播（BFS）").
//
// * Block light: classic two-queue BFS (removal queue + addition queue)
//   seeded from emissive blocks; stored per-block in a nibble array inside
//   each Chunk.
// * Sky light: per-column heightmap fill + BFS spread, cached per chunk and
//   recomputed when the chunk is edited.
// * The engine collects the set of touched chunks per tick so the session can
//   broadcast UpdateLight packets exactly where light changed.
#pragma once
#include <cstdint>
#include <queue>
#include <unordered_set>
#include <vector>
#include "../game/World.hpp"

namespace cppfm {

// helper kept for callers that need immediate 3×3 (drain uses unified expanded set).
struct LightUpdateQueue {
    std::unordered_set<std::int64_t> dirty;
    void mark(std::int32_t cx, std::int32_t cz) { dirty.insert(chunkKey(cx, cz)); }
    void markAndNeighbors(std::int32_t cx, std::int32_t cz) {
        for (int dz=-1; dz<=1; ++dz) for (int dx=-1; dx<=1; ++dx) mark(cx+dx, cz+dz);
    }
    std::unordered_set<std::int64_t> drain() { auto v=dirty; dirty.clear(); return v; }
    bool empty() const { return dirty.empty(); }
    std::size_t size() const { return dirty.size(); }
};

struct LightUpdateBatch {
    // Chunks whose serialized light payload must be re-sent.
    std::unordered_set<std::int64_t> dirtyChunks;
    LightUpdateQueue queue;
};

class LightEngine {
public:
    explicit LightEngine(World& world) : world_(world) {}

    // Called whenever a block changes (both directions).
    void onBlockChanged(std::int32_t x, std::int32_t y, std::int32_t z,
                        std::uint16_t oldState, std::uint16_t newState);

    // Drains queued work; returns chunks whose light arrays changed.
    LightUpdateBatch drain();

    std::uint8_t blockLightAt(std::int32_t x, std::int32_t y,
                              std::int32_t z) const;
    // Recompute sky-light BFS cache for a whole chunk (used by serializers).
    void ensureSkyLight(std::int32_t cx, std::int32_t cz);

private:
    struct Node {
        std::int32_t x, y, z;
        std::uint8_t level;
    };

    int emissionOf(std::uint16_t state) const;
    int opacityOf(std::uint16_t state) const;

    void addLight(std::int32_t x, std::int32_t y, std::int32_t z,
                  std::uint8_t level);
    void removeLight(std::int32_t x, std::int32_t y, std::int32_t z);
    void setBlockLight(std::int32_t x, std::int32_t y, std::int32_t z,
                       std::uint8_t v);

    World& world_;
    std::queue<Node> addQueue_;
    std::queue<Node> removeQueue_;
    // pending sky-light rebuilds (opacity changes, diagonal neighbors)
    std::unordered_set<std::int64_t> pendingSkyRebuild_;
    // chunks dirtied by cross-chunk sky BFS propagation
    std::unordered_set<std::int64_t> skyDirtyExtra_;
};

} // namespace cppfm
