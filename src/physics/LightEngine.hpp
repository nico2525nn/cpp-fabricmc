// LightEngine: incremental Minecraft-style lighting (plan3.md
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

struct LightUpdateBatch {
    // Chunks whose serialized light payload must be re-sent.
    std::unordered_set<std::int64_t> dirtyChunks;
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
};

} // namespace cppfm
