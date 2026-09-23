// World: superflat world storage, generation, block get/set.
// Chunk model: 24 sections of 16x16x16 block state ids (flat arrays; simple & fast).
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>
#include "generated/BlockStates.hpp"
#include "TerrainGen.hpp"
#include "../worldgen/MultiNoise.hpp"
#include "../worldgen/ChunkGenerator.hpp"
#include "../worldgen/StructureManager.hpp"
#include "ChunkTicket.hpp"
#include "Constants.hpp"

namespace cppfm {

constexpr int kSectionsPerChunk = 24;      // 384 / 16
constexpr int kMinY = -64;
constexpr int kMaxY = kMinY + 384;         // 320
constexpr int kSeaLevelFlat = -63;         // observed on reference flat worlds

inline constexpr std::int64_t chunkKey(std::int32_t cx, std::int32_t cz) {
    return (static_cast<std::int64_t>(static_cast<std::uint32_t>(cx)) << 32)
         | static_cast<std::uint32_t>(cz);
}
inline constexpr std::pair<std::int32_t,std::int32_t> chunkKeyDecode(std::int64_t k) {
    return {static_cast<std::int32_t>(k >> 32), static_cast<std::int32_t>(k & 0xFFFFFFFFLL)};
}

template <typename Callback, typename... Args>
void invokeWorldHook(Callback& callback, const char* name,
                     Args&&... args) noexcept {
    if (!callback) return;
    try {
        callback(std::forward<Args>(args)...);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[World] %s failed: %s\n", name, e.what());
    } catch (...) {
        std::fprintf(stderr, "[World] %s failed\n", name);
    }
}

struct Chunk {
    // layout: [section][yInSection][z][x]
    std::array<std::uint16_t, kSectionsPerChunk * 4096> blocks{};
    // Per-cell biomes: 24 sections × 64 cells (4×4×4), values are the synced biome-registry indices used directly on the wire.
    std::array<std::uint16_t, kSectionsPerChunk * 64> biomes{};
    // Block light, 4 bits per block (same index layout as `blocks`).
    std::array<std::uint8_t, (kSectionsPerChunk * 4096 + 1) / 2> blockLightNib{};
    // Cached sky light (built lazily by the LightEngine), 4 bits per block.
    std::shared_ptr<std::array<std::uint8_t,
                               (kSectionsPerChunk * 4096 + 1) / 2>> skyLight;
    // Storage allocation is not the same as a valid lighting calculation.
    // Keep that distinction explicit so a zero-filled cache is never sent as
    // authoritative skylight or used for crop light checks.
    bool skyLightReady = false;
    std::uint64_t revision = 0;

    // Clear all state before recycling an unloaded chunk allocation.  Keeping
    // a small bounded pool avoids repeated malloc/free page churn during long
    // traversals without keeping the chunk logically loaded in `chunks_`.
    void resetForReuse() {
        blocks.fill(0);
        biomes.fill(0);
        blockLightNib.fill(0);
        skyLight.reset();
        skyLightReady = false;
        revision = 0;
    }

    static constexpr std::size_t index(int section, int yIn, int z, int x) {
        return (static_cast<std::size_t>(section) * 4096)
             + (static_cast<std::size_t>(yIn) * 256)
             + (static_cast<std::size_t>(z) * 16)
             + static_cast<std::size_t>(x);
    }
    static constexpr std::size_t biomeIndex(int section, int cellY,
                                            int cellZ, int cellX) {
        return (static_cast<std::size_t>(section) * 64)
             + (static_cast<std::size_t>(cellY) * 16)
             + (static_cast<std::size_t>(cellZ) * 4)
             + static_cast<std::size_t>(cellX);
    }
    static std::uint8_t getNibble(const std::array<std::uint8_t,
                                  (kSectionsPerChunk * 4096 + 1) / 2>& a,
                                  std::size_t i) {
        return (i & 1) ? static_cast<std::uint8_t>(a[i >> 1] >> 4)
                       : static_cast<std::uint8_t>(a[i >> 1] & 0x0F);
    }
    static void setNibble(std::array<std::uint8_t,
                          (kSectionsPerChunk * 4096 + 1) / 2>& a,
                          std::size_t i, std::uint8_t v) {
        const std::uint8_t hi = a[i >> 1] & 0xF0;
        const std::uint8_t lo = a[i >> 1] & 0x0F;
        a[i >> 1] = (i & 1) ? static_cast<std::uint8_t>(lo | (v << 4))
                            : static_cast<std::uint8_t>(hi | (v & 0x0F));
    }
};

enum class LevelType { Flat, Normal, Nether, End };

class World {
    friend class worldgen::ChunkGenerator;
    friend class worldgen::FlatLevelSource;
    friend class worldgen::NormalLevelSource;
    friend class worldgen::NetherLevelSource;
    friend class worldgen::EndLevelSource;
public:
    World(std::string biomeKey, LevelType level, std::uint64_t seed)
        : biome_(std::move(biomeKey)), level_(level), terrain_(seed), srv_seed(seed) {
        initWorldgen();
    }
    ~World();

    LevelType levelType() const { return level_; }
    std::uint64_t seed() const noexcept { return srv_seed; }
    // Level metadata is loaded before the first chunk is generated. Refuse a
    // seed change after chunks exist; changing it then would create terrain
    // seams in an already authoritative world.
    bool setSeed(std::uint64_t seed) {
        if (!chunks_.empty()) return false;
        terrain_ = TerrainGenerator(seed);
        srv_seed = seed;
        initWorldgen();
        return true;
    }
    struct SpawnPoint {
        std::int32_t x=0, y=-60, z=0;
        float angle=0.f;
    };
    SpawnPoint spawnPt = {};
    SpawnPoint spawnPoint() const { return spawnPt; }
    void setSpawnPoint(const SpawnPoint& sp) { spawnPt = sp; }
    int seaLevel() const { return level_ == LevelType::Normal ? 63 : kSeaLevelFlat; }
    bool isFlat() const { return level_ == LevelType::Flat; }

    // Feet Y for a fresh spawn at this column (after ensuring the chunk exists).
    int surfaceFeetY(std::int32_t wx, std::int32_t wz) {
        generateChunkIfMissing(wx >> 4, wz >> 4);
        int col = 4;
        withChunk(wx >> 4, wz >> 4, [&](const Chunk& c) {
            for (int ry = kSectionsPerChunk * 16 - 1; ry >= 0; --ry)
                if (c.blocks[Chunk::index(ry >> 4, ry & 15, wz & 15, wx & 15)] != 0) { col = ry + 1; break; }
        });
        return kMinY + col;
    }

    // Read the surface only when the chunk is already loaded.  Commands such
    // as /locate must not synchronously create and serialize an arbitrary
    // structure chunk just to choose a display Y coordinate.
    std::optional<int> surfaceFeetYIfLoaded(std::int32_t wx,
                                             std::int32_t wz) const {
        int col = 4;
        if (!withChunk(wx >> 4, wz >> 4, [&](const Chunk& c) {
                for (int ry = kSectionsPerChunk * 16 - 1; ry >= 0; --ry) {
                    if (c.blocks[Chunk::index(ry >> 4, ry & 15,
                                               wz & 15, wx & 15)] != 0) {
                        col = ry + 1;
                        break;
                    }
                }
            }))
            return std::nullopt;
        return kMinY + col;
    }

    // NOTE: logically const (lazy generation); mutex is mutable Loader hook: return true if it filled the chunk (e.g., from disk).
    void setLoader(std::function<bool(std::int32_t, std::int32_t, Chunk&)> l) {
        std::lock_guard lock(hooksMtx_);
        loader_ = std::move(l);
    }
    void setOnEdit(std::function<void(std::int32_t, std::int32_t)> cb) {
        std::lock_guard lock(hooksMtx_);
        onEdit_ = std::move(cb);
    }
    void setOnBlockChanged(std::function<void(std::int32_t, std::int32_t,
                                               std::int32_t, std::uint16_t,
                                               std::uint16_t)> cb) {
        std::lock_guard lock(hooksMtx_);
        onBlockChanged_ = std::move(cb);
    }
    void setOnBlockPlace(std::function<void(std::int32_t, std::int32_t, std::int32_t, std::uint16_t, std::uint16_t)> cb) {
        std::lock_guard lock(hooksMtx_);
        onBlockPlace_ = std::move(cb);
    }
    void setOnBlockBreak(std::function<void(std::int32_t, std::int32_t, std::int32_t, std::uint16_t, std::uint16_t)> cb) {
        std::lock_guard lock(hooksMtx_);
        onBlockBreak_ = std::move(cb);
    }
    void setOnBlockNeighborChange(std::function<void(std::int32_t, std::int32_t, std::int32_t, std::uint16_t)> cb) {
        std::lock_guard lock(hooksMtx_);
        onBlockNeighborChange_ = std::move(cb);
    }
    void addOnBlockPlaceListener(std::function<void(std::int32_t, std::int32_t, std::int32_t, std::uint16_t, std::uint16_t)> h) {
        std::lock_guard lock(hooksMtx_);
        blockPlaceListeners_.push_back(std::move(h));
    }
    void addOnBlockBreakListener(std::function<void(std::int32_t, std::int32_t, std::int32_t, std::uint16_t, std::uint16_t)> h) {
        std::lock_guard lock(hooksMtx_);
        blockBreakListeners_.push_back(std::move(h));
    }
    void addOnBlockNeighborChangeListener(std::function<void(std::int32_t, std::int32_t, std::int32_t, std::uint16_t)> h) {
        std::lock_guard lock(hooksMtx_);
        blockNeighborChangeListeners_.push_back(std::move(h));
    }
    void fireBlockPlace(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t oldSt, std::uint16_t newSt) {
        std::function<void(std::int32_t, std::int32_t, std::int32_t,
                           std::uint16_t, std::uint16_t)> hook;
        std::vector<decltype(hook)> listeners;
        {
            std::lock_guard lock(hooksMtx_);
            hook = onBlockPlace_;
            listeners = blockPlaceListeners_;
        }
        invokeWorldHook(hook, "block-place hook", x, y, z, oldSt, newSt);
        for (auto& h : listeners)
            invokeWorldHook(h, "block-place listener", x, y, z, oldSt, newSt);
    }
    void fireBlockBreak(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t oldSt, std::uint16_t newSt) {
        std::function<void(std::int32_t, std::int32_t, std::int32_t,
                           std::uint16_t, std::uint16_t)> hook;
        std::vector<decltype(hook)> listeners;
        {
            std::lock_guard lock(hooksMtx_);
            hook = onBlockBreak_;
            listeners = blockBreakListeners_;
        }
        invokeWorldHook(hook, "block-break hook", x, y, z, oldSt, newSt);
        for (auto& h : listeners)
            invokeWorldHook(h, "block-break listener", x, y, z, oldSt, newSt);
    }
    void fireBlockNeighborChange(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t neighborState) {
        std::function<void(std::int32_t, std::int32_t, std::int32_t,
                           std::uint16_t)> hook;
        std::vector<decltype(hook)> listeners;
        {
            std::lock_guard lock(hooksMtx_);
            hook = onBlockNeighborChange_;
            listeners = blockNeighborChangeListeners_;
        }
        invokeWorldHook(hook, "neighbor-change hook", x, y, z, neighborState);
        for (auto& h : listeners)
            invokeWorldHook(h, "neighbor-change listener", x, y, z, neighborState);
    }
    void onBlockNeighborChange(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t ns) { fireBlockNeighborChange(x,y,z,ns); }
    void onBlockPlace(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t o, std::uint16_t n) { fireBlockPlace(x,y,z,o,n); }
    void onBlockBreak(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t o, std::uint16_t n) { fireBlockBreak(x,y,z,o,n); }

    // ---- block-light accessors (nibble storage inside Chunk) --------------
    std::uint8_t getBlockLight(std::int32_t x, std::int32_t y,
                               std::int32_t z) const {
        if (y < kMinY || y >= kMaxY) return 0;
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end()) return 0;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        const std::size_t i = Chunk::index(wy >> 4, wy & 15, lz, lx);
        return Chunk::getNibble(it->second->blockLightNib, i);
    }
    void setBlockLightRaw(std::int32_t x, std::int32_t y, std::int32_t z,
                          std::uint8_t v) {
        if (y < kMinY || y >= kMaxY) return;
        generateChunkIfMissing(x >> 4, z >> 4);
        std::unique_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end()) return;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        const std::size_t i = Chunk::index(wy >> 4, wy & 15, lz, lx);
        Chunk::setNibble(it->second->blockLightNib, i, v & 0x0F);
    }
    // Sky light cache accessors (nullptr when not yet computed).
    std::uint8_t getSkyLight(std::int32_t x, std::int32_t y,
                             std::int32_t z) const {
        if (y < kMinY || y >= kMaxY) return 0;
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end() || !it->second->skyLight || !it->second->skyLightReady) return 0;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        const std::size_t i = Chunk::index(wy >> 4, wy & 15, lz, lx);
        return Chunk::getNibble(*it->second->skyLight, i);
    }
    bool hasSkyLightCache(std::int32_t cx, std::int32_t cz) const {
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        return it != chunks_.end() && it->second->skyLightReady &&
               static_cast<bool>(it->second->skyLight);
    }
    void setSkyLightRaw(std::int32_t x, std::int32_t y, std::int32_t z,
                        std::uint8_t v) {
        if (y < kMinY || y >= kMaxY) return;
        std::unique_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end()) return;
        if (!it->second->skyLight)
            it->second->skyLight =
                std::make_shared<std::array<std::uint8_t,
                                            (kSectionsPerChunk * 4096 + 1) / 2>>();
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        const std::size_t i = Chunk::index(wy >> 4, wy & 15, lz, lx);
        Chunk::setNibble(*it->second->skyLight, i, v & 0x0F);
    }
    void ensureSkyStorage(std::int32_t cx, std::int32_t cz) {
        std::unique_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it == chunks_.end()) return;
        if (!it->second->skyLight)
            it->second->skyLight =
                std::make_shared<std::array<std::uint8_t,
                                            (kSectionsPerChunk * 4096 + 1) / 2>>();
    }
    void invalidateSkyLight(std::int32_t cx, std::int32_t cz) {
        std::unique_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it != chunks_.end()) it->second->skyLightReady = false;
    }
    void markSkyLightReady(std::int32_t cx, std::int32_t cz) {
        std::unique_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it != chunks_.end() && it->second->skyLight) it->second->skyLightReady = true;
    }

    // Block update: check if block above needs to fall (sand/gravel)
    void scheduleNeighborUpdates(std::int32_t x, std::int32_t y, std::int32_t z) {
        // Check block above for gravity
        const auto above = getBlock(x, y + 1, z);
        const auto& names = gen::blockNameToState();
        static const uint16_t sand  = (uint16_t)names.at("minecraft:sand");
        static const uint16_t gravel= (uint16_t)names.at("minecraft:gravel");
        if (above == sand || above == gravel) {
            setBlockInternal(x, y+1, z, 0);
            int fallY = y;
            while (fallY > kMinY && getBlock(x, fallY-1, z) == 0) --fallY;
            setBlockInternal(x, fallY, z, above);
            fireEdit(x >> 4, z >> 4);
        }
        // Torch/support blocks pop off if support removed
        static const uint16_t torch = (uint16_t)names.at("minecraft:torch");
        if (above == torch) {
            setBlockInternal(x, y+1, z, 0);
            fireEdit(x >> 4, z >> 4);
        }
    }

private:
    static constexpr std::size_t kMaxRecycledChunks = 128;

    void fireEdit(std::int32_t cx, std::int32_t cz) const {
        std::function<void(std::int32_t, std::int32_t)> hook;
        {
            std::lock_guard lock(hooksMtx_);
            hook = onEdit_;
        }
        invokeWorldHook(hook, "edit hook", cx, cz);
    }

    std::unique_ptr<Chunk> acquireChunk() const {
        if (recycledChunks_.empty()) return std::make_unique<Chunk>();
        auto out = std::move(recycledChunks_.back());
        recycledChunks_.pop_back();
        out->resetForReuse();
        return out;
    }

    void recycleChunk(std::unique_ptr<Chunk> chunk) const {
        if (!chunk || recycledChunks_.size() >= kMaxRecycledChunks) return;
        chunk->resetForReuse();
        recycledChunks_.push_back(std::move(chunk));
    }

    void setBlockInternal(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
        if (y < kMinY || y >= kMaxY) return;
        if (gen::blockByState(state) == nullptr) return;
        generateChunkIfMissing(x >> 4, z >> 4);
        std::unique_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end()) return;
        auto& c = *it->second;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)] = state;
        ++c.revision;
    }

public:

    // Double-checked, atomic lazy generation (safe under concurrent joins).
    void generateChunkIfMissing(std::int32_t cx, std::int32_t cz) const;
    // Inline delegation kept for header-only builds; actual impl in WorldGen.cpp
private:
    void generateChunkFallback(Chunk& c, std::int32_t cx, std::int32_t cz) const {
        if (level_ == LevelType::Flat) fillFlat(c);
        else fillTerrainV3(c, cx, cz);
    }
public:
    // Runs fn(chunk) while holding the world read lock. Use for any access that must not race with chunk replacement or edits.
    template <typename Fn>
    bool withChunk(std::int32_t cx, std::int32_t cz, Fn&& fn) const {
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it == chunks_.end()) return false;
        fn(*it->second);
        return true;
    }
    // Mutable counterpart for subsystems that update a whole chunk-owned
    // cache (lighting, for example).  Holding one exclusive lock around the
    // callback avoids a lock/unlock cycle for every cell in a 384-high chunk.
    template <typename Fn>
    bool withChunkMutable(std::int32_t cx, std::int32_t cz, Fn&& fn) {
        std::unique_lock lock(mutex_);
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
        std::uint16_t old = 0;
        {
            std::unique_lock lock(mutex_);
            auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
            if (it == chunks_.end()) return;
            auto& c = *it->second;
            const int lx = x & 15, lz = z & 15, wy = y - kMinY;
            auto& block = c.blocks[Chunk::index(wy >> 4, wy & 15, lz, lx)];
            old = block;
            if (old == state) return;
            block = state;
            ++c.revision;
        }
        // A state-identical write is not a block update in vanilla.  The
        // early return above prevents redstone, fluid, light, and Fabric
        // block hooks from recursively reprocessing an already-stable state.
        std::function<void(std::int32_t, std::int32_t, std::int32_t,
                           std::uint16_t, std::uint16_t)> blockChanged;
        {
            std::lock_guard lock(hooksMtx_);
            blockChanged = onBlockChanged_;
        }
        invokeWorldHook(blockChanged, "block-change hook", x, y, z, old, state);
        // Block Event Bus firing
        if (old == 0 && state != 0) fireBlockPlace(x,y,z,old,state);
        else if (old != 0 && state == 0) fireBlockBreak(x,y,z,old,state);
        else if (old != state) {
            // treat as neighbor change for adjacent? also fire neighbor for subscribers
            fireBlockNeighborChange(x,y,z,state);
        }
        // also notify neighbors for IBlockBehavior onNeighborChange
        static constexpr int DX[6] = {1,-1,0,0,0,0};
        static constexpr int DY[6] = {0,0,1,-1,0,0};
        static constexpr int DZ[6] = {0,0,0,0,1,-1};
        for (int d=0; d<6; ++d) {
            fireBlockNeighborChange(x+DX[d], y+DY[d], z+DZ[d], state);
        }
        fireEdit(x >> 4, z >> 4);
    }
    // Block state updates loop over six neighbors and notify subscribers.
    void updateBlockState(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t newState) {
        setBlock(x, y, z, newState);
        static constexpr int DX[6] = {1,-1,0,0,0,0};
        static constexpr int DY[6] = {0,0,1,-1,0,0};
        static constexpr int DZ[6] = {0,0,0,0,1,-1};
        for (int d = 0; d < 6; ++d) {
            onBlockNeighborChange(x + DX[d], y + DY[d], z + DZ[d], newState);
        }
    }
    struct BlockPosI { std::int32_t x=0, y=0, z=0; };
    void updateBlockState(BlockPosI pos, std::uint16_t newState) {
        updateBlockState(pos.x, pos.y, pos.z, newState);
    }
    // Return a stable snapshot.  Returning a raw pointer after releasing the
    // world lock allowed eraseChunk() to invalidate it while a caller was
    // serializing the chunk.
    std::optional<Chunk> tryGetCopy(std::int32_t cx, std::int32_t cz) const {
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it == chunks_.end()) return std::nullopt;
        return *it->second;
    }
    std::vector<std::int64_t> allChunkKeys() const {
        std::shared_lock lock(mutex_);
        std::vector<std::int64_t> out;
        out.reserve(chunks_.size());
        for (auto& kv : chunks_) out.push_back(kv.first);
        return out;
    }
    // Sum of revisions of the chunk (cheap "did anything change" probe).
    std::uint64_t revisionAt(std::int32_t cx, std::int32_t cz) const {
        std::shared_lock lock(mutex_);
        auto it = chunks_.find(chunkKey(cx, cz));
        return it == chunks_.end() ? 0 : it->second->revision;
    }

    bool eraseChunk(std::int32_t cx, std::int32_t cz) {
        // Keep generation and eviction in the same order so a concurrent
        // border crossing cannot race the bounded allocation pool.
        std::lock_guard generationLock(generationMtx_);
        std::unique_ptr<Chunk> reclaimed;
        {
            std::unique_lock lock(mutex_);
            auto it = chunks_.find(chunkKey(cx, cz));
            if (it == chunks_.end()) return false;
            reclaimed = std::move(it->second);
            chunks_.erase(it);
        }
        recycleChunk(std::move(reclaimed));
        return true;
    }
    // B-07 async I/O: install a fully decoded chunk from ioPool worker (tick thread only)
    void setChunk(std::int32_t cx, std::int32_t cz, Chunk chunk) {
        std::unique_lock lock(mutex_);
        auto key = chunkKey(cx, cz);
        auto ptr = std::make_unique<Chunk>(std::move(chunk));
        // revision bump ensures cache invalidation (ChunkCache uses rev)
        ++ptr->revision;
        chunks_[key] = std::move(ptr);
        lock.unlock();
        fireEdit(cx, cz);
    }
    std::size_t loadedChunkCount() const {
        std::shared_lock lock(mutex_);
        return chunks_.size();
    }
    void addSpawnTicket(std::int32_t cx, std::int32_t cz, std::int64_t tick = 0) {
        std::unique_lock lock(mutex_);
        // W17 strict: SPAWN tickets must NOT pollute ForcedChunks persistence (avoid maxLoadedChunks inflation)
        // Only add to ticketManager, not forcedChunks_ set (Yarn spawn chunk loader is transient)
        ticketManager_.addTicket(cx, cz, TicketType::SPAWN, 31, tick);
    }
    bool isForced(std::int32_t cx, std::int32_t cz) const {
        std::shared_lock lock(mutex_);
        return forcedChunks_.count(chunkKey(cx, cz)) != 0;
    }
    bool isForcedKey(std::int64_t k) const {
        std::shared_lock lock(mutex_);
        return forcedChunks_.count(k) != 0;
    }
    ChunkTicketManager& ticketManager() { return ticketManager_; }
    const ChunkTicketManager& ticketManager() const { return ticketManager_; }
    bool hasTicket(std::int32_t cx, std::int32_t cz, TicketType t) const {
        std::shared_lock lock(mutex_);
        return ticketManager_.hasTicket(cx, cz, t);
    }
    int ticketLevel(std::int32_t cx, std::int32_t cz) const {
        std::shared_lock lock(mutex_);
        return ticketManager_.getMinLevel(cx, cz);
    }
    std::vector<std::int64_t> forcedChunkKeys() const {
        std::shared_lock lock(mutex_);
        std::vector<std::int64_t> out(forcedChunks_.begin(), forcedChunks_.end());
        return out;
    }
    void clearForcedChunks() {
        std::unique_lock lock(mutex_);
        forcedChunks_.clear();
        ticketManager_.clear();
    }
    void restoreForcedChunk(std::int32_t cx, std::int32_t cz) {
        std::unique_lock lock(mutex_);
        forcedChunks_.insert(chunkKey(cx, cz));
        ticketManager_.addTicket(cx, cz, TicketType::FORCED, 31, 0);
    }
    struct ForcedChunkState {
        static inline std::int64_t toLong(int cx,int cz){ return chunkKey(cx,cz); }
    };
    bool setChunkForced(int cx,int cz,bool forced){
        std::int64_t k = ForcedChunkState::toLong(cx,cz);
        std::unique_lock lock(mutex_);
        if(forced){
            if(forcedChunks_.insert(k).second){ ticketManager_.addTicket(cx,cz,TicketType::FORCED,31,0); return true; }
            return false;
        } else {
            if(forcedChunks_.erase(k)){ ticketManager_.removeTicket(cx,cz,TicketType::FORCED); return true; }
            return false;
        }
    }
    std::vector<std::int64_t> getForcedChunks() const {
        std::shared_lock lock(mutex_);
        return std::vector<std::int64_t>(forcedChunks_.begin(), forcedChunks_.end());
    }
    std::vector<std::int64_t> getForcedChunksSnapshot() const { return getForcedChunks(); }
    void truncateForcedChunksIfNeeded(){
        std::unique_lock lock(mutex_);
        if(forcedChunks_.size()<=constants::kMaxForcedChunks) return;
        std::fprintf(stderr,"[World] ForcedChunks %zu >%d, truncating to %d (vanilla limit)\n", forcedChunks_.size(), constants::kMaxForcedChunks, constants::kMaxForcedChunks);
        std::unordered_set<std::int64_t> truncated;
        truncated.reserve(constants::kMaxForcedChunks);
        int c=0;
        for(auto k: forcedChunks_){ if(c++>=constants::kMaxForcedChunks) break; truncated.insert(k); }
        forcedChunks_.swap(truncated);
    }

    const std::string& biomeKey() const { return biome_; }

    // Biome codec wiring: resolve biome key <-> synced registry index. Must be installed before any chunk generation (GameServer::init).
    void setDimensionId(std::int8_t d) { dimensionId_ = d; }
    std::int8_t dimensionId() const { return dimensionId_; }
    void setBiomeCodec(std::function<std::int32_t(const std::string&)> toIndex,
                       std::int32_t defaultIndex) {
        biomeToIndex_ = std::move(toIndex);
        defaultBiomeIndex_ = defaultIndex;
    }
    std::int32_t biomeIndexOf(const std::string& key) const {
        return biomeToIndex_ ? biomeToIndex_(key) : 0;
    }
    std::string dimensionKey() const {
        if(dimensionId_==-1) return "minecraft:the_nether";
        if(dimensionId_==1) return "minecraft:the_end";
        return "minecraft:overworld";
    }
    std::string sampledBiome(int x,int y,int z) const {
        if(biomeSource_) return biomeSource_->sample(x,y,z);
        return biome_;
    }
    bool isEndHighlandsAt(int x,int z) const {
        if(dimensionId_ != 1) return true; // only gate in End
        std::string bio = sampledBiome(x,64,z);
        return bio=="minecraft:end_highlands" || bio=="minecraft:end_midlands";
    }

    // considered in simulation distance per vanilla ChunkTicket SPWAN behavior: they tick even without nearby players.
    bool isPositionInSimulationDistance(std::int32_t x, std::int32_t z) const {
        std::int32_t cx = x >> 4, cz = z >> 4;
        {
            std::shared_lock lock(mutex_);
            if (forcedChunks_.count(chunkKey(cx, cz))) return true;
            if (ticketManager_.getMinLevel(cx, cz) <= 31) return true;
        }
        std::function<bool(std::int32_t, std::int32_t)> simulationCallback;
        {
            std::lock_guard lock(hooksMtx_);
            simulationCallback = simCallback_;
        }
        if (simulationCallback) return simulationCallback(cx, cz);
        // fallback: if we have simulationDistance_ consider spawn distance
        if (simulationDistance_ <= 0) return true;
        // without callback, assume in range (tests without GameServer)
        return true;
    }
    bool isChunkInSimulationDistance(std::int32_t cx, std::int32_t cz) const {
        {
            std::shared_lock lock(mutex_);
            if (forcedChunks_.count(chunkKey(cx, cz))) return true;
            if (ticketManager_.getMinLevel(cx, cz) <= 31) return true;
        }
        std::function<bool(std::int32_t, std::int32_t)> simulationCallback;
        {
            std::lock_guard lock(hooksMtx_);
            simulationCallback = simCallback_;
        }
        if (simulationCallback) return simulationCallback(cx, cz);
        return true;
    }
    void setSimulationDistanceCallback(std::function<bool(std::int32_t,std::int32_t)> cb) {
        std::lock_guard lock(hooksMtx_);
        simCallback_ = std::move(cb);
    }
    void setSimulationDistance(int d) { simulationDistance_ = d; }
    int simulationDistance() const { return simulationDistance_; }

    void setGenerator(std::unique_ptr<worldgen::ChunkGenerator> g) { generator_ = std::move(g); }
    const worldgen::ChunkGenerator* generator() const { return generator_.get(); }

private:
    void fillTerrainV3(Chunk& c, std::int32_t cx, std::int32_t cz) const;
    void fillNether(Chunk& c, std::int32_t cx, std::int32_t cz) const;
    void fillEnd(Chunk& c, std::int32_t cx, std::int32_t cz) const;

public:
    void fillTerrain(Chunk& c, std::int32_t cx, std::int32_t cz) const {
        fillTerrainV3(c, cx, cz);
    }

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
    // Hook configuration is normally installed during server bootstrap, but
    // Persistence clears its callbacks during teardown while worker/session
    // threads may still be finishing a read or edit.  Protect both reads and
    // replacement, and invoke copied callbacks outside this mutex so a hook
    // may safely re-enter World or replace another hook.
    mutable std::mutex hooksMtx_;
    // Serializes the expensive missing-chunk generation path.  Several session
    // threads can request the same edge chunk at once while a player crosses a
    // chunk boundary; without this gate each thread builds a full ~0.6 MiB
    // Chunk before try_emplace discards all but one copy.
    mutable std::mutex generationMtx_;
    mutable std::unordered_map<std::int64_t, std::unique_ptr<Chunk>> chunks_;
    mutable std::vector<std::unique_ptr<Chunk>> recycledChunks_;
    mutable std::unordered_set<std::int64_t> forcedChunks_;
    mutable ChunkTicketManager ticketManager_;
    std::string biome_;
    LevelType level_;
    TerrainGenerator terrain_;
    std::uint64_t srv_seed;
    std::function<std::int32_t(const std::string&)> biomeToIndex_;
    std::int32_t defaultBiomeIndex_ = 40;
    std::int8_t dimensionId_ = 0;
    std::shared_ptr<worldgen::MultiNoiseBiomeSource> biomeSource_;
    std::unique_ptr<worldgen::StructureManager> structureManager_;
    std::unique_ptr<worldgen::ChunkGenerator> generator_;
public:
    worldgen::StructureManager* structureManager() { return structureManager_.get(); }
    const worldgen::StructureManager* structureManager() const { return structureManager_.get(); }
private:
    std::function<bool(std::int32_t,std::int32_t)> simCallback_;
    int simulationDistance_ = 10;
    std::vector<std::pair<const char*, float>> oreTableV3_;   // name, rarity
    void initWorldgen();
    std::function<bool(std::int32_t, std::int32_t, Chunk&)> loader_;
    std::function<void(std::int32_t, std::int32_t)> onEdit_;
    std::function<void(std::int32_t, std::int32_t, std::int32_t,
                       std::uint16_t, std::uint16_t)> onBlockChanged_;
    std::function<void(std::int32_t, std::int32_t, std::int32_t, std::uint16_t, std::uint16_t)> onBlockPlace_;
    std::function<void(std::int32_t, std::int32_t, std::int32_t, std::uint16_t, std::uint16_t)> onBlockBreak_;
    std::function<void(std::int32_t, std::int32_t, std::int32_t, std::uint16_t)> onBlockNeighborChange_;
    std::vector<std::function<void(std::int32_t, std::int32_t, std::int32_t, std::uint16_t, std::uint16_t)>> blockPlaceListeners_;
    std::vector<std::function<void(std::int32_t, std::int32_t, std::int32_t, std::uint16_t, std::uint16_t)>> blockBreakListeners_;
    std::vector<std::function<void(std::int32_t, std::int32_t, std::int32_t, std::uint16_t)>> blockNeighborChangeListeners_;
};

} // namespace cppfm
