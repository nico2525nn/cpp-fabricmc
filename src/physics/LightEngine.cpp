// LightEngine implementation: BFS block-light updates + cached sky light.
#include "LightEngine.hpp"
#include <algorithm>

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
        // Water attenuates by one in vanilla.  Never infer this from generated
        // numeric state ranges: registry order is data-driven and can change.
        if (b->name == "minecraft:water") return 1;
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
    constexpr std::int32_t kMinCoord = -(1 << 25);
    constexpr std::int32_t kMaxCoord = (1 << 25) - 1;
    const auto geometry = world_.verticalGeometry();
    if (x < kMinCoord || x > kMaxCoord || z < kMinCoord || z > kMaxCoord ||
        !geometry.containsY(y)) return;
    const std::int32_t chunkX = x >> 4;
    const std::int32_t chunkZ = z >> 4;
    const bool hadSkyCache = geometry.hasSkylight &&
                             world_.hasSkyLightCache(chunkX, chunkZ);
    const int oldOpacity = opacityOf(oldState);
    const int newOpacity = opacityOf(newState);
    const bool opacityChanged = oldOpacity != newOpacity;
    const bool needsSkyRebuild =
        geometry.hasSkylight && (!hadSkyCache || opacityChanged);
    if (geometry.hasSkylight && opacityChanged)
        world_.invalidateSkyLight(chunkX, chunkZ);
    // Block-light propagation is simulation-culled; sky rebuilds are also culled via isChunkInSimulationDistance.
    // For spawn chunks (forced / ChunkTicket SPAWN level 31) we always tick even outside player simulation radius.
    if (!world_.isChunkInSimulationDistance(x >> 4, z >> 4) && !world_.isPositionInSimulationDistance(x, z)) {
        // Still ensure sky storage exists for view distance rendering, but skip heavy BFS queuing.
        if (geometry.hasSkylight) world_.ensureSkyStorage(chunkX, chunkZ);
        return;
    }
    const int oldEmit = emissionOf(oldState);
    const int newEmit = emissionOf(newState);
    std::queue<Node> additions;
    std::queue<Node> removals;
    std::unordered_set<std::int64_t> skyRebuilds;

    if (oldEmit > 0) removals.push({x, y, z, static_cast<std::uint8_t>(oldEmit)});
    const uint8_t cur = world_.getBlockLight(x, y, z);
    if (cur > 0 && cur > newEmit)
        removals.push({x, y, z, cur});

    if (newEmit > 0) {
        setBlockLight(x, y, z, static_cast<std::uint8_t>(newEmit));
        additions.push({x, y, z, static_cast<std::uint8_t>(newEmit)});
    }
    if (newOpacity < oldOpacity) {
        // Opening a path can expose previously dark cells to existing light.
        // Seed propagation from adjacent lit cells without clearing them;
        // removal work for a closing path is already seeded at the changed
        // cell above and walks outward to surviving boundary light.
        static constexpr int DX[6] = {1,-1,0,0,0,0};
        static constexpr int DY[6] = {0,0,1,-1,0,0};
        static constexpr int DZ[6] = {0,0,0,0,1,-1};
        for (int d = 0; d < 6; ++d) {
            const int nx = x + DX[d], ny = y + DY[d], nz = z + DZ[d];
            const auto nl = world_.getBlockLight(nx, ny, nz);
            if (nl > 0) additions.push({nx, ny, nz, nl});
        }
    }
    if (geometry.hasSkylight) {
        // A newly generated chunk has no valid sky cache. Even a mutation
        // that preserves opacity (air -> crop) must initialize it.
        if (needsSkyRebuild) {
            // Orthogonal neighbor loop is redundant and caused double
            // scheduling. Keep only the 3x3 neighborhood.
            auto schedSky = [&](std::int32_t cxx, std::int32_t czz) {
                if (!world_.isChunkInSimulationDistance(cxx, czz)) return;
                // This callback runs under World’s ordered mutation gate.
                // Defer missing-chunk load/generation to drain(), after the
                // callback sequence has returned.
                world_.ensureSkyStorage(cxx, czz);
                skyRebuilds.insert(chunkKey(cxx, czz));
            };
            const std::int32_t bcx = x >> 4, bcz = z >> 4;
            schedSky(bcx, bcz);
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dz) schedSky(bcx + dx, bcz + dz);
        }
        // Sky-light cache is invalidated wholesale for the chunk.
        world_.ensureSkyStorage(chunkX, chunkZ);
    }

    // Publish only after all World reads/writes are complete. drain() holds
    // queueMutex_ only while swapping these containers, never while calling
    // back into World (which would invert the World-mutation lock order).
    std::lock_guard lock(queueMutex_);
    while (!removals.empty()) {
        removeQueue_.push(removals.front());
        removals.pop();
    }
    while (!additions.empty()) {
        addQueue_.push(additions.front());
        additions.pop();
    }
    pendingSkyRebuild_.insert(skyRebuilds.begin(), skyRebuilds.end());
}

LightUpdateBatch LightEngine::drain() {
    // World edits publish block state and their light-queue callbacks under
    // this same recursive gate. Hold it for one propagation batch so a block
    // cannot be removed between the removal and addition passes and emit a
    // stale UpdateLight field from the intermediate state.
    auto mutationGate = world_.lockMutationGate();
    LightUpdateBatch batch;
    std::queue<Node> additions;
    std::queue<Node> removals;
    std::unordered_set<std::int64_t> pendingSkyRebuilds;
    std::unordered_set<std::int64_t> extraSky;
    {
        std::lock_guard lock(queueMutex_);
        additions.swap(addQueue_);
        removals.swap(removeQueue_);
        pendingSkyRebuilds.swap(pendingSkyRebuild_);
        extraSky.swap(skyDirtyExtra_);
    }
    auto mark = [&](std::int32_t x, std::int32_t z) {
        batch.dirtyChunks.insert(chunkKey(x >> 4, z >> 4));
    };

    constexpr int DX[6] = {1,-1,0,0,0,0};
    constexpr int DY[6] = {0,0,1,-1,0,0};
    constexpr int DZ[6] = {0,0,0,0,1,-1};

    while (!removals.empty()) {
        const Node n = removals.front();
        removals.pop();
        const int currentEmission = emissionOf(
            world_.getBlock(n.x, n.y, n.z));
        if (currentEmission > 0) {
            if (currentEmission >= n.level) {
                // A queued removal may outlive a rapid remove/re-place. The
                // current block still supports this field; retain any higher
                // value contributed by another nearby emitter.
                const auto existing = world_.getBlockLight(n.x, n.y, n.z);
                const auto sourceLevel = std::max(
                    existing, static_cast<std::uint8_t>(currentEmission));
                if (existing != sourceLevel)
                    setBlockLight(n.x, n.y, n.z, sourceLevel);
                additions.push({n.x, n.y, n.z, sourceLevel});
                mark(n.x, n.z);
                continue;
            }

            // A weaker replacement must relinquish the old source's excess;
            // the removal walk below will reseed surviving boundary light.
            const auto sourceLevel = static_cast<std::uint8_t>(currentEmission);
            if (world_.getBlockLight(n.x, n.y, n.z) != sourceLevel)
                setBlockLight(n.x, n.y, n.z, sourceLevel);
            additions.push({n.x, n.y, n.z, sourceLevel});
        } else if (world_.getBlockLight(n.x, n.y, n.z) == n.level) {
            setBlockLight(n.x, n.y, n.z, 0);
        }
        mark(n.x, n.z);
        for (int d = 0; d < 6; ++d) {
            const int nx = n.x + DX[d], ny = n.y + DY[d], nz = n.z + DZ[d];
            if (ny < world_.minY() || ny >= world_.maxY()) continue;
            const auto nl = world_.getBlockLight(nx, ny, nz);
            if (nl == 0) continue;
            if (nl < n.level) {
                setBlockLight(nx, ny, nz, 0);
                removals.push({nx, ny, nz, nl});
            } else if (nl >= n.level) {
                additions.push({nx, ny, nz, nl});   // boundary re-add
            }
        }
    }
    while (!additions.empty()) {
        Node n = additions.front();
        additions.pop();
        const int currentEmission = emissionOf(
            world_.getBlock(n.x, n.y, n.z));
        const auto existing = world_.getBlockLight(n.x, n.y, n.z);
        if (currentEmission > 0) {
            // A concurrent remove/re-place can clear the source after its new
            // addition was queued. Reassert its own emission, but preserve a
            // stronger value already propagated into this emitter's cell.
            const auto ownEmission = static_cast<std::uint8_t>(currentEmission);
            n.level = std::max(existing, ownEmission);
            if (existing < ownEmission)
                setBlockLight(n.x, n.y, n.z, ownEmission);
        } else {
            // Discard stale seeds whose source cell was cleared or reduced by
            // the removal pass; never recreate light from the queued value
            // alone.
            n.level = existing;
            if (n.level == 0) continue;
        }
        mark(n.x, n.z);
        for (int d = 0; d < 6; ++d) {
            const int nx = n.x + DX[d], ny = n.y + DY[d], nz = n.z + DZ[d];
            if (ny < world_.minY() || ny >= world_.maxY()) continue;
            const std::uint16_t ns = world_.getBlock(nx, ny, nz);
            const int opacity = std::max(1, opacityOf(ns));
            const int target = n.level - opacity;
            if (target <= 0) continue;
            const auto cur = world_.getBlockLight(nx, ny, nz);
            if (cur < target) {
                setBlockLight(nx, ny, nz, static_cast<std::uint8_t>(target));
                additions.push({nx, ny, nz, static_cast<std::uint8_t>(target)});
            }
        }
    }

    // Now single unified expansion via base → expanded with hasChunk||hasSkyLightCache guard, matching Yarn LevelLightEngine.
    {
        std::unordered_set<std::int64_t> skyRebuildSet;
        skyRebuildSet.reserve(batch.dirtyChunks.size() + pendingSkyRebuilds.size() + 8);
        for (auto k : batch.dirtyChunks) skyRebuildSet.insert(k);
        for (auto k : pendingSkyRebuilds) skyRebuildSet.insert(k);
        for (auto k : skyRebuildSet) {
            auto [skx, skz] = chunkKeyDecode(k);
            ensureSkyLight(skx, skz);
        }
        std::unordered_set<std::int64_t> base;
        base.reserve(batch.dirtyChunks.size() + skyRebuildSet.size() + extraSky.size() + 8);
        for (auto k : batch.dirtyChunks) base.insert(k);
        for (auto k : skyRebuildSet) base.insert(k);
        for (auto k : extraSky) base.insert(k);
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

std::size_t LightEngine::pendingNodeCount() const {
    std::lock_guard lock(queueMutex_);
    return addQueue_.size() + removeQueue_.size();
}

std::size_t LightEngine::pendingSkyCount() const {
    std::lock_guard lock(queueMutex_);
    return pendingSkyRebuild_.size() + skyDirtyExtra_.size();
}

// ------------------------------------------------------------------ skylight

void LightEngine::ensureSkyLight(std::int32_t cx, std::int32_t cz) {
    const auto geometry = world_.verticalGeometry();
    if (!geometry.hasSkylight) return;
    // Lighting is a consumer of chunk tickets, not a chunk-loading policy.
    // Only the requested chunk is generated here; missing neighbours are
    // treated as empty until normal view/simulation loading makes them
    // available and requests their own rebuild.
    world_.generateChunkIfMissing(cx, cz);

    std::array<std::uint16_t, kSectionsPerChunk * 4096> blocks{};
    World::ChunkRevisionSnapshot sourceChunks;
    const auto targetKey = chunkKey(cx, cz);
    if (!world_.withChunk(cx, cz, [&](const Chunk& c) {
            sourceChunks.try_emplace(
                targetKey, World::ChunkVersion{c.revision, c.incarnation()});
            for (std::size_t i = 0; i < blocks.size(); ++i)
                blocks[i] = c.blockAt(i);
        }))
        return;

    const int top = geometry.maxY() - 1;

    // Heightmap of the first light-blocking block per column.  The one-block
    // margin lets the local pass detect overhangs at chunk edges without
    // reading a world cell while holding a lighting write lock.
    int surf[18][18];
    auto columnBlocker = [&](std::int64_t wx, std::int64_t wz) -> int {
        int blocker = geometry.minY - 1;
        const auto ncx = static_cast<std::int32_t>(wx >> 4);
        const auto ncz = static_cast<std::int32_t>(wz >> 4);
        const auto key = chunkKey(ncx, ncz);
        const int lx = static_cast<int>(wx & 15);
        const int lz = static_cast<int>(wz & 15);
        const bool found = world_.withChunk(ncx, ncz, [&](const Chunk& c) {
            sourceChunks.try_emplace(
                key, World::ChunkVersion{c.revision, c.incarnation()});
            for (int y = top; y >= geometry.minY; --y) {
                const auto state = c.blockAt(Chunk::index(
                    (y - kMinY) >> 4, (y - kMinY) & 15, lz, lx));
                if (opacityOf(state) >= 15) {
                    blocker = y;
                    break;
                }
            }
        });
        if (!found) sourceChunks.try_emplace(key, std::nullopt);
        return blocker;
    };
    for (int z = -1; z <= 16; ++z)
        for (int x = -1; x <= 16; ++x)
            surf[z + 1][x + 1] = columnBlocker(cx * 16 + x, cz * 16 + z);

    std::array<std::uint8_t, (kSectionsPerChunk * 4096 + 1) / 2> sky{};
    auto indexAt = [](std::int32_t x, std::int32_t y,
                      std::int32_t z) -> std::size_t {
        const int wy = y - kMinY;
        return Chunk::index(wy >> 4, wy & 15, z & 15, x & 15);
    };
    auto skyAt = [&](std::int32_t x, std::int32_t y,
                     std::int32_t z) -> std::uint8_t {
        if (!geometry.containsY(y)) return 0;
        return Chunk::getNibble(sky, indexAt(x, y, z));
    };
    auto setSky = [&](std::int32_t x, std::int32_t y,
                      std::int32_t z, std::uint8_t value) {
        if (!geometry.containsY(y)) return;
        Chunk::setNibble(sky, indexAt(x, y, z), value);
    };
    auto stateAt = [&](std::int32_t x, std::int32_t y,
                       std::int32_t z) -> std::uint16_t {
        if (!geometry.containsY(y)) return 0;
        return blocks[Chunk::index((y - kMinY) >> 4,
                                   (y - kMinY) & 15, z & 15, x & 15)];
    };

    struct QN { std::int32_t x, y, z; std::uint8_t l; };
    std::queue<QN> q;
    constexpr int DX[4] = {1,-1,0,0};
    constexpr int DZ[4] = {0,0,1,-1};

    // pass 1: vertical fill.  This is intentionally computed in local
    // storage: the old implementation acquired the world mutex once per
    // block and made a single edit more expensive than the server tick.
    for (int lz = 0; lz < 16; ++lz)
        for (int lx = 0; lx < 16; ++lx) {
            const std::int32_t wx = cx * 16 + lx, wz = cz * 16 + lz;
            const int blocker = surf[lz + 1][lx + 1];
            int level = 15;
            for (int y = top; y > blocker; --y) {
                const int op = opacityOf(stateAt(wx, y, wz));
                if (level <= 0) break;
                setSky(wx, y, wz, static_cast<std::uint8_t>(level));
                level = std::max(0, level - op);
            }

            // Boundary seeds from the illuminated portion of this column can
            // spread sideways into shadowed neighbours (under overhangs /
            // beside cliffs).  The source-light check filters the range.
            const int runBottom = blocker + 1;
            for (int d = 0; d < 4; ++d) {
                const int nbSurf =
                    surf[lz + 1 + DZ[d]][lx + 1 + DX[d]];
                const int from = std::max(runBottom, nbSurf - 15);
                const int to = std::min(top, nbSurf);
                for (int y = std::max(from, geometry.minY);
                     y <= to && y < geometry.maxY(); ++y) {
                    const auto value = skyAt(wx, y, wz);
                    if (value > 0) q.push({wx, y, wz, value});
                }
            }
        }

    // pass 2: bounded BFS spread inside the requested chunk.  A neighbouring
    // chunk is rebuilt by its own ticket/edit; crossing into an absent chunk
    // here would either generate the world recursively or require retaining a
    // second mutable cache while this one is being committed.
    while (!q.empty()) {
        const QN n = q.front(); q.pop();
        static constexpr int SDX[6] = {1,-1,0,0,0,0};
        static constexpr int SDY[6] = {0,0,1,-1,0,0};
        static constexpr int SDZ[6] = {0,0,0,0,1,-1};
        for (int d = 0; d < 6; ++d) {
            const std::int32_t nx = n.x + SDX[d], ny = n.y + SDY[d],
                               nz = n.z + SDZ[d];
            if (!geometry.containsY(ny)) continue;
            if ((nx >> 4) != cx || (nz >> 4) != cz) continue;
            const std::uint16_t ns = stateAt(nx, ny, nz);
            const int op = opacityOf(ns);
            if (op >= 15) continue;
            const int target = n.l - std::max(1, op);
            if (target <= 0) continue;
            const auto current = skyAt(nx, ny, nz);
            if (current < target) {
                setSky(nx, ny, nz, static_cast<std::uint8_t>(target));
                q.push({nx, ny, nz, static_cast<std::uint8_t>(target)});
            }
        }
    }

    // One exclusive lock publishes a complete, self-consistent cache.  A
    // serializer can therefore never observe a half-rebuilt nibble array.
    if (!world_.publishSkyLightCache(cx, cz, sourceChunks, sky)) {
        std::lock_guard lock(queueMutex_);
        pendingSkyRebuild_.insert(targetKey);
    }
}

} // namespace cppfm
