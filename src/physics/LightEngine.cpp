// LightEngine implementation: BFS block-light updates + cached sky light.
#include "LightEngine.hpp"
#include <algorithm>
#include <cstdio>

namespace cppfm {

int LightEngine::emissionOf(std::uint16_t state) const {
    const gen::BlockDef* b = gen::blockByState(state);
    return b ? b->emitLight : 0;
}

int LightEngine::opacityOf(std::uint16_t state) const {
    if (state == 0) return 0;
    const gen::BlockDef* b = gen::blockByState(state);
    if (!b) return 15;
    // emissive blocks (glowstone, sea_lantern, shroomlight, beacon) are not opaque in vanilla
    // dataset incorrectly marks them as filter 15; treat as transparent for light
    if (b->emitLight > 0) return 0;
    if (b->filterLight >= 15) {
        // water attenuates by 1 in vanilla; dataset marks it opaque
        static const std::uint16_t water = static_cast<std::uint16_t>(
            gen::blockNameToState().at("minecraft:water"));
        if (state == water || (state >= 86 && state <= 101)) return 1;
        return 15;
    }
    return b->filterLight;
}

void LightEngine::setBlockLight(std::int32_t x, std::int32_t y,
                                std::int32_t z, std::uint8_t v) {
    world_.setBlockLightRaw(x, y, z, v);
}

std::uint8_t LightEngine::blockLightAt(std::int32_t x, std::int32_t y,
                                       std::int32_t z) const {
    return world_.getBlockLight(x, y, z);
}

void LightEngine::onBlockChanged(std::int32_t x, std::int32_t y,
                                 std::int32_t z, std::uint16_t oldState,
                                 std::uint16_t newState) {
    // Block-light propagation is simulation-culled; sky rebuilds are also culled via isChunkInSimulationDistance.
    // For spawn chunks (forced / ChunkTicket SPAWN level 31) we always tick even outside player simulation radius.
    if (!world_.isChunkInSimulationDistance(x >> 4, z >> 4) && !world_.isPositionInSimulationDistance(x, z)) {
        // Still ensure sky storage exists for view distance rendering, but skip heavy BFS queuing.
        world_.ensureSkyStorage(x >> 4, z >> 4);
        return;
    }
    const int oldEmit = emissionOf(oldState);
    const int newEmit = emissionOf(newState);

    if (oldEmit > 0) removeQueue_.push({x, y, z, static_cast<std::uint8_t>(oldEmit)});
    const uint8_t cur = world_.getBlockLight(x, y, z);
    if (cur > 0 && cur > newEmit)
        removeQueue_.push({x, y, z, cur});
    else if (cur == newEmit && newEmit == 0) {}

    if (newEmit > 0) {
        setBlockLight(x, y, z, static_cast<std::uint8_t>(newEmit));
        addQueue_.push({x, y, z, static_cast<std::uint8_t>(newEmit)});
    }
    // opacity change: re-run neighbors through add queue so light flows back
    if (opacityOf(oldState) != opacityOf(newState)) {
        static constexpr int DX[6] = {1,-1,0,0,0,0};
        static constexpr int DY[6] = {0,0,1,-1,0,0};
        static constexpr int DZ[6] = {0,0,0,0,1,-1};
        for (int d = 0; d < 6; ++d) {
            const int nx = x + DX[d], ny = y + DY[d], nz = z + DZ[d];
            const auto nl = world_.getBlockLight(nx, ny, nz);
            if (nl > 1) {
                removeQueue_.push({nx, ny, nz, nl});
                setBlockLight(nx, ny, nz, 0);
                addQueue_.push({nx, ny, nz, nl});
            } else if (nl == 1) {
                addQueue_.push({nx, ny, nz, nl});
            }
        }
        // orthogonal neighbor loop is redundant and caused double scheduling. Keep only the 3×3.
        auto schedSky = [&](std::int32_t cxx, std::int32_t czz) {
            if (!world_.isChunkInSimulationDistance(cxx, czz)) return;
            world_.generateChunkIfMissing(cxx, czz);
            world_.ensureSkyStorage(cxx, czz);
            pendingSkyRebuild_.insert(chunkKey(cxx, czz));
        };
        const std::int32_t bcx = x >> 4, bcz = z >> 4;
        schedSky(bcx, bcz);
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx || dz) schedSky(bcx + dx, bcz + dz);
    }
    // sky light cache is invalidated wholesale for the chunk
    world_.ensureSkyStorage(x >> 4, z >> 4);
}

LightUpdateBatch LightEngine::drain() {
    static const bool dbg = getenv("CPPFM_LIGHT_DEBUG") != nullptr;
    LightUpdateBatch batch;
    auto mark = [&](std::int32_t x, std::int32_t z) {
        batch.dirtyChunks.insert(chunkKey(x >> 4, z >> 4));
    };

    constexpr int DX[6] = {1,-1,0,0,0,0};
    constexpr int DY[6] = {0,0,1,-1,0,0};
    constexpr int DZ[6] = {0,0,0,0,1,-1};

    while (!removeQueue_.empty()) {
        const Node n = removeQueue_.front();
        removeQueue_.pop();
        // clear this cell when it still holds the expected value
        if (world_.getBlockLight(n.x, n.y, n.z) == n.level)
            setBlockLight(n.x, n.y, n.z, 0);
        if (dbg)
            std::fprintf(stderr, "[light] rm pop (%d,%d,%d) L=%u\n",
                         n.x, n.y, n.z, n.level);
        mark(n.x, n.z);
        for (int d = 0; d < 6; ++d) {
            const int nx = n.x + DX[d], ny = n.y + DY[d], nz = n.z + DZ[d];
            if (ny < kMinY || ny >= kMaxY) continue;
            const auto nl = world_.getBlockLight(nx, ny, nz);
            if (nl == 0) continue;
            if (nl < n.level) {
                setBlockLight(nx, ny, nz, 0);
                removeQueue_.push({nx, ny, nz, nl});
            } else if (nl >= n.level) {
                addQueue_.push({nx, ny, nz, nl});   // boundary re-add
            }
        }
    }
    while (!addQueue_.empty()) {
        const Node n = addQueue_.front();
        addQueue_.pop();
        mark(n.x, n.z);
        for (int d = 0; d < 6; ++d) {
            const int nx = n.x + DX[d], ny = n.y + DY[d], nz = n.z + DZ[d];
            if (ny < kMinY || ny >= kMaxY) continue;
            const std::uint16_t ns = world_.getBlock(nx, ny, nz);
            const int opacity = std::max(1, opacityOf(ns));
            const int target = n.level - opacity;
            if (target <= 0) continue;
            const auto cur = world_.getBlockLight(nx, ny, nz);
            if (cur < target) {
                setBlockLight(nx, ny, nz, static_cast<std::uint8_t>(target));
                addQueue_.push({nx, ny, nz, static_cast<std::uint8_t>(target)});
            }
        }
    }

    // Now single unified expansion via base → expanded with hasChunk||hasSkyLightCache guard, matching Yarn LevelLightEngine.
    {
        std::unordered_set<std::int64_t> skyRebuildSet;
        skyRebuildSet.reserve(batch.dirtyChunks.size() + pendingSkyRebuild_.size() + 8);
        for (auto k : batch.dirtyChunks) skyRebuildSet.insert(k);
        for (auto k : pendingSkyRebuild_) skyRebuildSet.insert(k);
        pendingSkyRebuild_.clear();
        skyDirtyExtra_.clear();
        for (auto k : skyRebuildSet) {
            auto [skx, skz] = chunkKeyDecode(k);
            ensureSkyLight(skx, skz);
        }
        std::unordered_set<std::int64_t> base;
        base.reserve(batch.dirtyChunks.size() + skyRebuildSet.size() + skyDirtyExtra_.size() + 8);
        for (auto k : batch.dirtyChunks) base.insert(k);
        for (auto k : skyRebuildSet) base.insert(k);
        for (auto k : skyDirtyExtra_) base.insert(k);
        skyDirtyExtra_.clear();
        // single 3×3 expansion — only for chunks that exist (hasChunk or hasSkyLightCache) to avoid empty UpdateLight
        std::unordered_set<std::int64_t> expanded;
        expanded.reserve(base.size() * 9 + 8);
        for (auto k : base) {
            auto [cxx, czz] = chunkKeyDecode(k);
            for (int dz=-1; dz<=1; ++dz) {
                for (int dx=-1; dx<=1; ++dx) {
                    const std::int32_t ncx = cxx + dx, ncz = czz + dz;
                    if (world_.hasSkyLightCache(ncx, ncz) || world_.hasChunk(ncx, ncz))
                        expanded.insert(chunkKey(ncx, ncz));
                }
            }
        }
        batch.dirtyChunks = std::move(expanded);
        for (auto k : batch.dirtyChunks) { auto [dkx, dkz] = chunkKeyDecode(k); batch.queue.mark(dkx, dkz); }
    }
    return batch;
}

// ------------------------------------------------------------------ skylight

void LightEngine::ensureSkyLight(std::int32_t cx, std::int32_t cz) {
    // Generate the chunk and its four neighbours so column sampling works.
    world_.generateChunkIfMissing(cx, cz);
    world_.generateChunkIfMissing(cx - 1, cz);
    world_.generateChunkIfMissing(cx + 1, cz);
    world_.generateChunkIfMissing(cx, cz - 1);
    world_.generateChunkIfMissing(cx, cz + 1);
    world_.ensureSkyStorage(cx, cz);

    const int top = kMaxY - 1;

    // Heightmap of first light-blocking block per column (with 1-block margin so boundary detection at chunk edges is accurate).
    int surf[18][18];
    auto columnBlocker = [&](std::int64_t wx, std::int64_t wz) -> int {
        for (int y = top; y >= kMinY; --y)
            if (opacityOf(world_.getBlock(static_cast<std::int32_t>(wx), y,
                                          static_cast<std::int32_t>(wz))) >= 15)
                return y;                            // blocker height
        return kMinY - 1;
    };
    for (int z = -1; z <= 16; ++z)
        for (int x = -1; x <= 16; ++x)
            surf[z + 1][x + 1] = columnBlocker(cx * 16 + x, cz * 16 + z);

    struct QN { std::int32_t x, y, z; std::uint8_t l; };
    std::queue<QN> q;
    constexpr int DX[4] = {1,-1,0,0};
    constexpr int DZ[4] = {0,0,1,-1};

    // pass 1: vertical fill — full light from sky down to the blocker Clear shadowed region first so stale light does not persist
    for (int lz = 0; lz < 16; ++lz)
        for (int lx = 0; lx < 16; ++lx) {
            const std::int32_t wx = cx * 16 + lx, wz = cz * 16 + lz;
            const int blocker = surf[lz + 1][lx + 1];
            for (int y = blocker; y >= kMinY; --y)
                world_.setSkyLightRaw(wx, y, wz, 0);
        }
    for (int lz = 0; lz < 16; ++lz)
        for (int lx = 0; lx < 16; ++lx) {
            const std::int32_t wx = cx * 16 + lx, wz = cz * 16 + lz;
            const int blocker = surf[lz + 1][lx + 1];
            bool lit = true;
            int attenuated = 0;
            for (int y = top; y > blocker; --y) {
                const std::uint16_t st = world_.getBlock(wx, y, wz);
                const int op = opacityOf(st);
                if (op > 0) {
                    attenuated += op;
                    if (attenuated >= 15) lit = false;
                }
                const std::uint8_t v = lit ? 15 : 0;
                world_.setSkyLightRaw(wx, y, wz, v);
                if (!lit) break;
            }
            // boundary seeds: lit cells that can spread sideways into shadowed
            // neighbours (under overhangs / beside cliffs) — fixed to use nbSurf
            if (lit || true) {
                const int runBottom = blocker + 1;
                for (int d = 0; d < 4; ++d) {
                    const int nbSurf =
                        surf[lz + 1 + DZ[d]][lx + 1 + DX[d]];
                    const int from = std::max(runBottom, nbSurf - 15);
                    const int to = std::min(top, nbSurf);   // neighbor's dark top
                    for (int y = std::max(from, kMinY); y <= to && y < kMaxY; ++y) {
                        if (world_.getSkyLight(wx, y, wz) > 0)
                            q.push({wx, y, wz,
                                    world_.getSkyLight(wx, y, wz)});
                    }
                }
            }
        }

    // pass 2: BFS spread — now cross-chunk to fix side propagation
    while (!q.empty()) {
        const QN n = q.front(); q.pop();
        static constexpr int SDX[6] = {1,-1,0,0,0,0};
        static constexpr int SDY[6] = {0,0,1,-1,0,0};
        static constexpr int SDZ[6] = {0,0,0,0,1,-1};
        for (int d = 0; d < 6; ++d) {
            const std::int32_t nx = n.x + SDX[d], ny = n.y + SDY[d],
                               nz = n.z + SDZ[d];
            if (ny < kMinY || ny >= kMaxY) continue;
            const std::int32_t ncx = nx >> 4, ncz = nz >> 4;
            if (ncx != cx || ncz != cz) {
                world_.generateChunkIfMissing(ncx, ncz);
                world_.ensureSkyStorage(ncx, ncz);
            }
            const std::uint16_t ns = world_.getBlock(nx, ny, nz);
            const int op = opacityOf(ns);
            if (op >= 15) continue;
            const std::uint8_t target =
                static_cast<std::uint8_t>(n.l - std::max(1, op));
            if (target <= 0) continue;
            if (world_.getSkyLight(nx, ny, nz) < target) {
                world_.setSkyLightRaw(nx, ny, nz, target);
                if (ncx != cx || ncz != cz)
                    skyDirtyExtra_.insert(chunkKey(ncx, ncz));
                q.push({nx, ny, nz, target});
            }
        }
    }
}

} // namespace cppfm
