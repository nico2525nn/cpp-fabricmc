// World: superflat world storage, generation, block get/set.
// Chunk model: 24 sections of 16x16x16 block state ids (flat arrays; simple & fast).
#pragma once
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <functional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>
#include "generated/BlockStates.hpp"
#include "BlockStateTraits.hpp"
#include "TerrainGen.hpp"
#include "../worldgen/MultiNoise.hpp"
#include "../worldgen/ChunkGenerator.hpp"
#include "../worldgen/StructureManager.hpp"
#include "ChunkTicket.hpp"
#include "Constants.hpp"
#include "MutationGateLease.hpp"

namespace cppfm {

constexpr int kSectionsPerChunk = 24;      // 384 / 16
constexpr int kMinY = -64;
constexpr int kMaxY = kMinY + 384;         // 320
constexpr int kSeaLevelFlat = -63;         // observed on reference flat worlds

// Chunks use a fixed Overworld-sized backing allocation, but the legal world
// range is dimension-specific. Keep physical build bounds separate from the
// Nether's smaller logical height (128): Nether chunks still span Y=0..255.
struct DimensionGeometry {
    int minY;
    int height;
    int logicalHeight;
    bool hasSkylight;

    constexpr int maxY() const noexcept { return minY + height; }
    constexpr int logicalMaxY() const noexcept { return minY + logicalHeight; }
    constexpr int minSection() const noexcept { return minY / 16; }
    constexpr int sectionCount() const noexcept { return height / 16; }
    constexpr bool containsY(int y) const noexcept {
        return y >= minY && y < maxY();
    }
    constexpr int storageY(int y) const noexcept { return y - kMinY; }
    constexpr int storageSection(int worldSection) const noexcept {
        return worldSection - (kMinY / 16);
    }
    constexpr int worldSection(int storageSectionIndex) const noexcept {
        return storageSectionIndex + (kMinY / 16);
    }
};

inline constexpr DimensionGeometry kOverworldGeometry{-64, 384, 384, true};
inline constexpr DimensionGeometry kNetherGeometry{0, 256, 128, false};
inline constexpr DimensionGeometry kEndGeometry{0, 256, 256, false};

enum class ChunkLoadStatus { Loaded, Missing, Failed };

inline constexpr DimensionGeometry dimensionGeometry(std::int8_t dimensionId) noexcept {
    if (dimensionId == -1) return kNetherGeometry;
    if (dimensionId == 1) return kEndGeometry;
    return kOverworldGeometry;
}

inline constexpr std::int64_t chunkKey(std::int32_t cx, std::int32_t cz) {
    const auto bits = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32)
                    | static_cast<std::uint32_t>(cz);
    return std::bit_cast<std::int64_t>(bits);
}
inline constexpr std::pair<std::int32_t,std::int32_t> chunkKeyDecode(std::int64_t k) {
    const auto bits = std::bit_cast<std::uint64_t>(k);
    return {std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits >> 32)),
            std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits))};
}

class LightEngine;

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

class ChunkPublishError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class StaleChunkPublishError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline void invokeChunkPublishCallback(std::function<void()>& callback) {
    if (!callback) return;
    try {
        callback();
    } catch (const StaleChunkPublishError&) {
        throw;
    } catch (const std::exception& error) {
        throw ChunkPublishError(error.what());
    } catch (...) {
        throw ChunkPublishError("non-standard exception");
    }
}

struct Chunk {
    // layout: [section][yInSection][z][x]
    using LightArray =
        std::array<std::uint8_t, (kSectionsPerChunk * 4096 + 1) / 2>;
    using HeightmapValues = std::array<std::uint16_t, 16 * 16>;

    // Per-cell biomes: 24 sections × 64 cells (4×4×4), values are the synced biome-registry indices used directly on the wire.
    std::array<std::uint16_t, kSectionsPerChunk * 64> biomes{};
    // Block light, 4 bits per block (same index layout as `blocks`).
    LightArray blockLightNib{};
    std::uint64_t revision = 0;

    Chunk() = default;
    Chunk(const Chunk& other)
        : biomes(other.biomes), blockLightNib(other.blockLightNib),
          revision(other.revision), blocks_(other.blocks_),
          worldSurfaceWg_(other.worldSurfaceWg_),
          oceanFloorWg_(other.oceanFloorWg_),
          hasWorldgenHeightmaps_(other.hasWorldgenHeightmaps_),
          skyLight_(other.skyLight_
                        ? std::make_shared<LightArray>(*other.skyLight_)
                        : nullptr),
          skyLightReady_(other.skyLightReady_),
          incarnation_(other.incarnation_) {}
    Chunk& operator=(const Chunk& other) {
        if (this == &other) return *this;
        auto skyLight = other.skyLight_
                            ? std::make_shared<LightArray>(*other.skyLight_)
                            : nullptr;
        biomes = other.biomes;
        blockLightNib = other.blockLightNib;
        revision = other.revision;
        blocks_ = other.blocks_;
        worldSurfaceWg_ = other.worldSurfaceWg_;
        oceanFloorWg_ = other.oceanFloorWg_;
        hasWorldgenHeightmaps_ = other.hasWorldgenHeightmaps_;
        skyLight_ = std::move(skyLight);
        skyLightReady_ = other.skyLightReady_;
        incarnation_ = other.incarnation_;
        return *this;
    }
    Chunk(Chunk&&) noexcept = default;
    Chunk& operator=(Chunk&&) noexcept = default;

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
    static std::uint8_t getNibble(const LightArray& a,
                                  std::size_t i) {
        return (i & 1) ? static_cast<std::uint8_t>(a[i >> 1] >> 4)
                       : static_cast<std::uint8_t>(a[i >> 1] & 0x0F);
    }
    static void setNibble(LightArray& a,
                          std::size_t i, std::uint8_t v) {
        const std::uint8_t hi = a[i >> 1] & 0xF0;
        const std::uint8_t lo = a[i >> 1] & 0x0F;
        a[i >> 1] = (i & 1) ? static_cast<std::uint8_t>(lo | (v << 4))
                            : static_cast<std::uint8_t>(hi | (v & 0x0F));
    }

    std::uint16_t blockAt(std::size_t index) const noexcept {
        return blocks_[index];
    }
    bool hasSkyLightStorage() const noexcept {
        return static_cast<bool>(skyLight_);
    }
    std::uint8_t skyLightAt(std::size_t index) const noexcept {
        return skyLight_ ? getNibble(*skyLight_, index) : 0;
    }
    bool skyLightReady() const noexcept { return skyLightReady_; }
    std::uint64_t incarnation() const noexcept { return incarnation_; }
    bool hasWorldgenHeightmaps() const noexcept {
        return hasWorldgenHeightmaps_;
    }
    const HeightmapValues& worldSurfaceWg() const noexcept {
        return worldSurfaceWg_;
    }
    const HeightmapValues& oceanFloorWg() const noexcept {
        return oceanFloorWg_;
    }

private:
    // Clear state only when World returns an unloaded allocation to its pool.
    // Keeping this private prevents a read-only chunk view from resetting a
    // live chunk or invalidating its revision/cache invariants.
    void resetForReuse() {
        blocks_.fill(0);
        biomes.fill(0);
        blockLightNib.fill(0);
        skyLight_.reset();
        skyLightReady_ = false;
        revision = 0;
        worldSurfaceWg_.fill(0);
        oceanFloorWg_.fill(0);
        hasWorldgenHeightmaps_ = false;
        incarnation_ = 0;
    }

    std::array<std::uint16_t, kSectionsPerChunk * 4096> blocks_{};
    HeightmapValues worldSurfaceWg_{};
    HeightmapValues oceanFloorWg_{};
    bool hasWorldgenHeightmaps_ = false;
    // Allocation is not equivalent to a valid light calculation. Keep the
    // cache private so const chunk views cannot mutate or retain its storage.
    std::shared_ptr<LightArray> skyLight_;
    bool skyLightReady_ = false;
    std::uint64_t incarnation_ = 0;
    friend class ChunkBuildWriter;
    friend class World;
};

// A narrow adapter for detached chunk construction. Block and biome access is
// value-only; publication side effects are held until World accepts the chunk.
template <typename T, std::size_t Size>
class ChunkArrayWriter final {
public:
    explicit ChunkArrayWriter(std::array<T, Size>& storage) noexcept
        : storage_(storage) {}
    ChunkArrayWriter(const ChunkArrayWriter&) = delete;
    ChunkArrayWriter& operator=(const ChunkArrayWriter&) = delete;
    ChunkArrayWriter(ChunkArrayWriter&&) = delete;
    ChunkArrayWriter& operator=(ChunkArrayWriter&&) = delete;

    T get(std::size_t index) const { return storage_.at(index); }
    void set(std::size_t index, T value) { storage_.at(index) = value; }
    void fill(T value) noexcept { storage_.fill(value); }

private:
    std::array<T, Size>& storage_;
};

class ChunkBuildWriter final {
public:
    using BlockStates =
        ChunkArrayWriter<std::uint16_t, kSectionsPerChunk * 4096>;
    using BiomeStates = ChunkArrayWriter<std::uint16_t,
                                         kSectionsPerChunk * 64>;

    explicit ChunkBuildWriter(Chunk& detached) noexcept
        : blocks(detached.blocks_), biomes(detached.biomes),
          detached_(detached) {}
    ChunkBuildWriter(const ChunkBuildWriter&) = delete;
    ChunkBuildWriter& operator=(const ChunkBuildWriter&) = delete;
    ChunkBuildWriter(ChunkBuildWriter&&) = delete;
    ChunkBuildWriter& operator=(ChunkBuildWriter&&) = delete;

    void setPublishCallback(std::function<void()> callback) {
        publishCallback_ = std::move(callback);
    }
    void addPublishCallback(std::function<void()> callback) {
        if (callback) additionalPublishCallbacks_.push_back(std::move(callback));
    }
    void setWorldgenHeightmaps(const Chunk::HeightmapValues& worldSurface,
                               const Chunk::HeightmapValues& oceanFloor) noexcept {
        detached_.worldSurfaceWg_ = worldSurface;
        detached_.oceanFloorWg_ = oceanFloor;
        detached_.hasWorldgenHeightmaps_ = true;
    }
    void adopt(Chunk&& candidate,
               std::function<void()> publishCallback) noexcept {
        detached_ = std::move(candidate);
        publishCallback_ = std::move(publishCallback);
    }
    std::function<void()> takePublishCallback() {
        auto primary = std::move(publishCallback_);
        auto additional = std::move(additionalPublishCallbacks_);
        if (additional.empty()) return primary;
        return [primary = std::move(primary),
                additional = std::move(additional)]() mutable {
            if (primary) primary();
            for (auto& callback : additional)
                if (callback) callback();
        };
    }

    BlockStates blocks;
    BiomeStates biomes;

private:
    Chunk& detached_;
    std::function<void()> publishCallback_;
    std::vector<std::function<void()>> additionalPublishCallbacks_;
};

enum class LevelType { Flat, Normal, Nether, End };

class World;
namespace world_edit_hook_detail {
struct Frame {
    const World* owner = nullptr;
    Frame* previous = nullptr;
};
inline thread_local Frame* currentFrame = nullptr;

inline std::size_t activeOnCurrentThread(const World* owner) noexcept {
    std::size_t count = 0;
    for (auto* frame = currentFrame; frame; frame = frame->previous)
        if (frame->owner == owner) ++count;
    return count;
}
} // namespace world_edit_hook_detail

namespace world_chunk_borrow_detail {
struct Frame {
    const World* owner = nullptr;
    Frame* previous = nullptr;
};
inline thread_local Frame* currentFrame = nullptr;

inline bool activeOnCurrentThread(const World* owner) noexcept {
    for (auto* frame = currentFrame; frame; frame = frame->previous)
        if (frame->owner == owner) return true;
    return false;
}
} // namespace world_chunk_borrow_detail

class World {
#ifdef CPPFM_WORLD_MUTATION_TEST
    friend struct WorldMutationContractTestAccess;
#endif
    friend class LightEngine;
    friend class worldgen::ChunkGenerator;
    friend class worldgen::FlatLevelSource;
    friend class worldgen::NormalLevelSource;
    friend class worldgen::NetherLevelSource;
    friend class worldgen::EndLevelSource;
private:
    struct PendingLoadedChunk {
        std::unique_ptr<Chunk> chunk;
        std::function<void()> publishCallback;
    };
    enum class PendingLoadedChunkResult {
        NotFound,
        Published,
        Resident,
        Stale,
    };

    class EditHookInvocation final {
    public:
        explicit EditHookInvocation(const World& owner) : owner_(owner) {
            frame_ = {&owner_, world_edit_hook_detail::currentFrame};
            world_edit_hook_detail::currentFrame = &frame_;
        }
        ~EditHookInvocation() {
            world_edit_hook_detail::currentFrame = frame_.previous;
            owner_.finishEditHookInvocation();
        }
        EditHookInvocation(const EditHookInvocation&) = delete;
        EditHookInvocation& operator=(const EditHookInvocation&) = delete;

    private:
        const World& owner_;
        world_edit_hook_detail::Frame frame_;
    };

    class ChunkBorrowInvocation final {
    public:
        explicit ChunkBorrowInvocation(const World& owner) : owner_(owner) {
            if (world_chunk_borrow_detail::activeOnCurrentThread(&owner_))
                throw std::logic_error(
                    "nested World::withChunk callback is not supported");
            frame_ = {&owner_, world_chunk_borrow_detail::currentFrame};
            world_chunk_borrow_detail::currentFrame = &frame_;
        }
        ~ChunkBorrowInvocation() {
            world_chunk_borrow_detail::currentFrame = frame_.previous;
        }
        ChunkBorrowInvocation(const ChunkBorrowInvocation&) = delete;
        ChunkBorrowInvocation& operator=(const ChunkBorrowInvocation&) = delete;

    private:
        const World& owner_;
        world_chunk_borrow_detail::Frame frame_;
    };

    void assertNotInChunkBorrow() const {
        if (world_chunk_borrow_detail::activeOnCurrentThread(this))
            throw std::logic_error(
                "World access from withChunk callback is forbidden; "
                "use the borrowed Chunk argument");
    }

    std::shared_lock<std::shared_mutex> lockShared() const {
        assertNotInChunkBorrow();
        return std::shared_lock<std::shared_mutex>(mutex_);
    }

    std::unique_lock<std::shared_mutex> lockUnique() const {
        assertNotInChunkBorrow();
        return std::unique_lock<std::shared_mutex>(mutex_);
    }

    std::shared_ptr<PendingLoadedChunk> findPendingLoadedChunk(
        std::int64_t key) const;
    bool retainPendingLoadedChunk(std::int64_t key,
                                  std::unique_ptr<Chunk>& chunk,
                                  std::function<void()>& publishCallback) const;
    PendingLoadedChunkResult publishPendingLoadedChunkLocked(
        std::int64_t key) const;
    void discardPendingLoadedChunk(
        std::int64_t key,
        const std::shared_ptr<PendingLoadedChunk>& pending) const;
    void discardPendingLoadedChunkIfAny(std::int64_t key) const;

public:
    struct ChunkVersion {
        std::uint64_t revision = 0;
        std::uint64_t incarnation = 0;
        bool operator==(const ChunkVersion&) const = default;
    };
    using ChunkRevisionSnapshot =
        std::unordered_map<std::int64_t,
                           std::optional<ChunkVersion>>;

    World(std::string biomeKey, LevelType level, std::uint64_t seed)
        : biome_(std::move(biomeKey)), level_(level), terrain_(seed),
          srv_seed(seed),
          dimensionId_(level == LevelType::Nether ? -1
                       : level == LevelType::End ? 1 : 0) {
        if (level == LevelType::Nether) spawnPt = {0, 128, 0, 0.0f};
        else if (level == LevelType::End) spawnPt = {0, 65, 0, 0.0f};
        initWorldgen();
    }
    ~World();

    LevelType levelType() const { return level_; }
    DimensionGeometry verticalGeometry() const noexcept {
        return dimensionGeometry(dimensionId_);
    }
    int minY() const noexcept { return verticalGeometry().minY; }
    int maxY() const noexcept { return verticalGeometry().maxY(); }
    std::uint64_t seed() const noexcept {
        return srv_seed.load(std::memory_order_acquire);
    }
    // Seal the seed before an asynchronous persistence read starts. The
    // generation mutex makes this linearizable with setSeed(): either the
    // override wins before the read begins, or the active read prevents it.
    void markChunkLoadStarted() {
        std::lock_guard generationLock(generationMtx_);
        chunkLoadStarted_ = true;
    }
    // Level metadata is loaded before the first chunk is generated. Refuse a
    // seed change after chunks exist; changing it then would create terrain
    // seams in an already authoritative world.
    bool setSeed(std::uint64_t seed) {
        // Match chunk publication's mutation-order -> generation -> storage
        // lock order. Every first load marks chunkLoadStarted_ before disk I/O
        // or generation, so an in-flight detached build cannot observe a
        // mixture of old and new seed state.
        auto mutationOrder = lockMutationOrder();
        std::lock_guard generationLock(generationMtx_);
        if (chunkLoadStarted_) return false;
        {
            auto lock = lockShared();
            if (!chunks_.empty()) return false;
        }
        terrain_ = TerrainGenerator(seed);
        srv_seed.store(seed, std::memory_order_release);
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
    int seaLevel() const {
        switch (dimensionId_) {
        case -1: return 32;
        case 1: return 0;
        default: return level_ == LevelType::Flat ? kSeaLevelFlat : 63;
        }
    }
    bool isFlat() const { return level_ == LevelType::Flat; }

    // Feet Y for a fresh spawn at this column (after ensuring the chunk exists).
    int surfaceFeetY(std::int32_t wx, std::int32_t wz) {
        generateChunkIfMissing(wx >> 4, wz >> 4);
        const auto geometry = verticalGeometry();
        int surface = geometry.minY + 4;
        withChunk(wx >> 4, wz >> 4, [&](const Chunk& c) {
            for (int y = geometry.maxY() - 1; y >= geometry.minY; --y)
                if (c.blockAt(Chunk::index((y - kMinY) >> 4,
                                           (y - kMinY) & 15,
                                           wz & 15, wx & 15)) != 0) {
                    surface = y + 1;
                    break;
                }
        });
        return surface;
    }

    // Read the surface only when the chunk is already loaded.  Commands such
    // as /locate must not synchronously create and serialize an arbitrary
    // structure chunk just to choose a display Y coordinate.
    std::optional<int> surfaceFeetYIfLoaded(std::int32_t wx,
                                             std::int32_t wz) const {
        const auto geometry = verticalGeometry();
        int surface = geometry.minY + 4;
        if (!withChunk(wx >> 4, wz >> 4, [&](const Chunk& c) {
                for (int y = geometry.maxY() - 1; y >= geometry.minY; --y) {
                    if (c.blockAt(Chunk::index((y - kMinY) >> 4,
                                               (y - kMinY) & 15,
                                               wz & 15, wx & 15)) != 0) {
                        surface = y + 1;
                        break;
                    }
                }
            }))
            return std::nullopt;
        return surface;
    }

    // Loader hook for detached chunk construction (for example, region-file
    // decoding). It must only populate the supplied writer and return whether
    // it produced a valid chunk. It is called without the storage or generation
    // mutexes held, but must not call back into this World; in particular, a
    // recursive request for the same missing key would recurse indefinitely.
    void setLoader(std::function<bool(std::int32_t, std::int32_t,
                                      ChunkBuildWriter&)> l) {
        setLoaderWithStatus(
            [loader = std::move(l)](std::int32_t cx, std::int32_t cz,
                                    ChunkBuildWriter& writer) {
                return loader && loader(cx, cz, writer)
                           ? ChunkLoadStatus::Loaded
                           : ChunkLoadStatus::Missing;
            });
    }
    void setLoaderWithStatus(
        std::function<ChunkLoadStatus(std::int32_t, std::int32_t,
                                      ChunkBuildWriter&)> loader) {
        std::lock_guard lock(hooksMtx_);
        loader_ = std::move(loader);
    }
    void clearLoader() {
        std::lock_guard lock(hooksMtx_);
        loader_ = {};
    }
    void setOnEdit(std::function<void(std::int32_t, std::int32_t)> cb) {
        std::lock_guard lock(hooksMtx_);
        onEdit_ = std::move(cb);
    }
    // Remove the persistence edit hook and wait for every invocation that
    // already copied it. A callback may re-enter this method on its own thread;
    // in that case wait only for other invocations, not for itself.
    void clearOnEditAndWait() {
        const auto ownInvocations =
            world_edit_hook_detail::activeOnCurrentThread(this);
        std::unique_lock lock(hooksMtx_);
        onEdit_ = {};
        const auto quiescent = [&] {
            return activeEditHooks_ <= ownInvocations;
        };
        if (!quiescent()) {
#ifdef CPPFM_WORLD_MUTATION_TEST
            editHookWaitersForTest_.fetch_add(1, std::memory_order_acq_rel);
            editHookQuiescent_.notify_all();
#endif
            editHookQuiescent_.wait(lock, quiescent);
#ifdef CPPFM_WORLD_MUTATION_TEST
            editHookWaitersForTest_.fetch_sub(1, std::memory_order_acq_rel);
#endif
        }
        onEdit_ = {};
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
        if (!verticalGeometry().containsY(y)) return 0;
        auto lock = lockShared();
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end()) return 0;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        const std::size_t i = Chunk::index(wy >> 4, wy & 15, lz, lx);
        return Chunk::getNibble(it->second->blockLightNib, i);
    }
    void setBlockLightRaw(std::int32_t x, std::int32_t y, std::int32_t z,
                          std::uint8_t v) {
        if (!verticalGeometry().containsY(y)) return;
        generateChunkIfMissing(x >> 4, z >> 4);
        auto lock = lockUnique();
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end()) return;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        const std::size_t i = Chunk::index(wy >> 4, wy & 15, lz, lx);
        Chunk::setNibble(it->second->blockLightNib, i, v & 0x0F);
    }
    // Sky light cache accessors (zero when absent or not yet valid).
    std::uint8_t getSkyLight(std::int32_t x, std::int32_t y,
                             std::int32_t z) const {
        if (!verticalGeometry().containsY(y)) return 0;
        auto lock = lockShared();
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end() || !it->second->skyLight_ ||
            !it->second->skyLightReady_) return 0;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        const std::size_t i = Chunk::index(wy >> 4, wy & 15, lz, lx);
        return Chunk::getNibble(*it->second->skyLight_, i);
    }
    bool hasSkyLightCache(std::int32_t cx, std::int32_t cz) const {
        auto lock = lockShared();
        auto it = chunks_.find(chunkKey(cx, cz));
        return it != chunks_.end() && it->second->skyLightReady_ &&
               static_cast<bool>(it->second->skyLight_);
    }
    void ensureSkyStorage(std::int32_t cx, std::int32_t cz) {
        auto lock = lockUnique();
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it == chunks_.end()) return;
        if (!it->second->skyLight_)
            it->second->skyLight_ = std::make_shared<Chunk::LightArray>();
    }
    void invalidateSkyLight(std::int32_t cx, std::int32_t cz) {
        auto lock = lockUnique();
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it != chunks_.end()) it->second->skyLightReady_ = false;
    }
private:
    // Only LightEngine may publish: the source version set is valid only when
    // it was captured while reading the exact block columns used by the build.
    bool publishSkyLightCache(std::int32_t cx, std::int32_t cz,
                              const ChunkRevisionSnapshot& expectedChunks,
                              const Chunk::LightArray& sky) {
        auto lock = lockUnique();
        // Validate only the chunks actually sampled by LightEngine. An
        // unrelated distant load should not discard this work, while any
        // mutation/eviction/publication in the source set must reject it.
        for (const auto& [key, expectedRevision] : expectedChunks) {
            const auto current = chunks_.find(key);
            const bool expectedLoaded = expectedRevision.has_value();
            if ((current != chunks_.end()) != expectedLoaded) return false;
            if (expectedLoaded &&
                ChunkVersion{current->second->revision,
                             current->second->incarnation_} != *expectedRevision)
                return false;
        }
        const auto targetKey = chunkKey(cx, cz);
        const auto expectedTarget = expectedChunks.find(targetKey);
        if (expectedTarget == expectedChunks.end() ||
            !expectedTarget->second)
            return false;
        auto it = chunks_.find(targetKey);
        if (it == chunks_.end()) return false;
        auto& chunk = *it->second;
        if (!chunk.skyLight_)
            chunk.skyLight_ = std::make_shared<Chunk::LightArray>();
        *chunk.skyLight_ = sky;
        chunk.skyLightReady_ = true;
        return true;
    }
public:

    // Block update: check if block above needs to fall (sand/gravel)
    void scheduleNeighborUpdates(std::int32_t x, std::int32_t y, std::int32_t z) {
        const auto& names = gen::blockNameToState();
        static const uint16_t sand  = (uint16_t)names.at("minecraft:sand");
        static const uint16_t gravel= (uint16_t)names.at("minecraft:gravel");
        static const uint16_t torch = (uint16_t)names.at("minecraft:torch");
        const auto initialAbove = getBlock(x, y + 1, z);
        if (initialAbove != sand && initialAbove != gravel &&
            initialAbove != torch)
            return;

        // Chunk loading can call an I/O-backed detached loader. Perform it
        // before entering the block/callback ordering critical section, then
        // re-read the support block after acquiring that section.
        generateChunkIfMissing(x >> 4, z >> 4);
        auto mutationOrder = lockMutationOrder();
        const auto above = getBlock(x, y + 1, z);
        if (above == sand || above == gravel) {
            setBlockInternal(x, y+1, z, 0);
            int fallY = y;
            while (fallY > minY() && getBlock(x, fallY-1, z) == 0) --fallY;
            setBlockInternal(x, fallY, z, above);
            fireEdit(x >> 4, z >> 4);
        }
        // Torch/support blocks pop off if support removed
        if (above == torch) {
            setBlockInternal(x, y+1, z, 0);
            fireEdit(x >> 4, z >> 4);
        }
    }

private:
    static constexpr std::size_t kMaxRecycledChunks = 128;
    static constexpr std::size_t kChunkBuildLockCount = 64;

    static std::uint64_t allocateChunkIncarnation() noexcept {
        static std::atomic<std::uint64_t> next{1};
        auto value = next.fetch_add(1, std::memory_order_relaxed);
        if (value == 0)
            value = next.fetch_add(1, std::memory_order_relaxed);
        return value;
    }

    static std::size_t chunkBuildStripeIndex(std::int64_t key) noexcept {
        const auto bits = std::bit_cast<std::uint64_t>(key);
        const auto cx = bits >> 32;
        const auto cz = bits & 0xFFFFFFFFULL;
        std::uint64_t hash =
            cx * 0x9E3779B185EBCA87ULL ^ cz * 0xC2B2AE3D27D4EB4FULL;
        hash ^= hash >> 30;
        hash *= 0xBF58476D1CE4E5B9ULL;
        hash ^= hash >> 27;
        hash *= 0x94D049BB133111EBULL;
        hash ^= hash >> 31;
        return static_cast<std::size_t>(hash % kChunkBuildLockCount);
    }

    std::unique_lock<std::mutex> lockChunkBuild(std::int64_t key) const {
        const auto stripe = chunkBuildStripeIndex(key);
#ifdef CPPFM_WORLD_MUTATION_TEST
        chunkBuildWaiters_.fetch_add(1, std::memory_order_acq_rel);
        try {
            std::unique_lock lock(chunkBuildMtx_[stripe]);
            chunkBuildWaiters_.fetch_sub(1, std::memory_order_acq_rel);
            return lock;
        } catch (...) {
            chunkBuildWaiters_.fetch_sub(1, std::memory_order_acq_rel);
            throw;
        }
#else
        return std::unique_lock<std::mutex>(chunkBuildMtx_[stripe]);
#endif
    }

    MutationGateLease lockMutationOrder() const {
        assertNotInChunkBorrow();
#ifdef CPPFM_WORLD_MUTATION_TEST
        std::unique_lock lock(blockMutationOrderMtx_, std::defer_lock);
        if (lock.try_lock())
            return MutationGateLease{this, std::move(lock)};
        mutationOrderWaiters_.fetch_add(1, std::memory_order_acq_rel);
        try {
            lock.lock();
            mutationOrderWaiters_.fetch_sub(1, std::memory_order_acq_rel);
            return MutationGateLease{this, std::move(lock)};
        } catch (...) {
            mutationOrderWaiters_.fetch_sub(1, std::memory_order_acq_rel);
            throw;
        }
#else
        return MutationGateLease{
            this, std::unique_lock<std::recursive_mutex>(
                      blockMutationOrderMtx_)};
#endif
    }

    void fireEdit(std::int32_t cx, std::int32_t cz) const {
        std::function<void(std::int32_t, std::int32_t)> hook;
        {
            std::lock_guard lock(hooksMtx_);
            hook = onEdit_;
            if (!hook) return;
            ++activeEditHooks_;
        }
        EditHookInvocation invocation(*this);
        invokeWorldHook(hook, "edit hook", cx, cz);
    }

    void finishEditHookInvocation() const {
        {
            std::lock_guard lock(hooksMtx_);
            --activeEditHooks_;
        }
        editHookQuiescent_.notify_all();
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

    // Publish a detached chunk without committing its side effects before the
    // map node and bucket capacity are ready. The caller holds mutation order,
    // which excludes other chunk-map writers while the callback runs.
    bool publishChunkCandidateLocked(
        std::int64_t key, std::unique_ptr<Chunk>& candidate,
        std::function<void()>& publishCallback) const {
        if (!candidate) return false;
        {
            std::lock_guard generationLock(generationMtx_);
            chunkLoadStarted_ = true;
        }

        std::unordered_map<std::int64_t, std::unique_ptr<Chunk>> staged;
        staged.reserve(1);
        staged.emplace(key, std::move(candidate));
        auto node = staged.extract(key);

        {
            auto lock = lockUnique();
            if (chunks_.contains(key)) {
                candidate = std::move(node.mapped());
                return false;
            }
            try {
                // Node-handle insertion below reuses its allocation. Reserve
                // the destination buckets now, before irreversible side data
                // (structures, opaque NBT, or biome overrides) is published.
                // The caller's mutation-order lease excludes other chunk-map
                // writers until insertion, so callback completion is followed
                // only by transfer of this already-allocated node. Keep every
                // allocation-capable map operation above the callback.
                chunks_.reserve(chunks_.size() + 1);
            } catch (...) {
                candidate = std::move(node.mapped());
                throw;
            }
        }

        node.mapped()->incarnation_ = allocateChunkIncarnation();
        try {
            invokeChunkPublishCallback(publishCallback);
        } catch (...) {
            candidate = std::move(node.mapped());
            throw;
        }

        auto lock = lockUnique();
        auto insertion = chunks_.insert(std::move(node));
        if (insertion.inserted) return true;
        candidate = std::move(insertion.node.mapped());
        return false;
    }

    std::optional<std::uint16_t> storeBlockState(
        std::int32_t x, std::int32_t y, std::int32_t z,
        std::uint16_t state, bool skipIdentical) {
        auto lock = lockUnique();
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end()) return std::nullopt;

        auto& chunk = *it->second;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        auto& block = chunk.blocks_[Chunk::index(wy >> 4, wy & 15, lz, lx)];
        const auto oldState = block;
        if (skipIdentical && oldState == state) return oldState;
        block = state;
        ++chunk.revision;
        return oldState;
    }

    enum class ConditionalBlockWriteStatus : std::uint8_t {
        MissingChunk,
        StateMismatch,
        Written
    };
    struct ConditionalBlockWriteResult {
        ConditionalBlockWriteStatus status;
        std::uint16_t oldState;
    };

    template <typename Matches>
    ConditionalBlockWriteResult storeBlockStateIf(
        std::int32_t x, std::int32_t y, std::int32_t z,
        std::uint16_t state, Matches&& matches) {
        auto lock = lockUnique();
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end())
            return {ConditionalBlockWriteStatus::MissingChunk, 0};

        auto& chunk = *it->second;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        auto& block = chunk.blocks_[Chunk::index(wy >> 4, wy & 15, lz, lx)];
        const auto oldState = block;
        if (!matches(oldState))
            return {ConditionalBlockWriteStatus::StateMismatch, oldState};
        if (oldState != state) {
            block = state;
            ++chunk.revision;
        }
        return {ConditionalBlockWriteStatus::Written, oldState};
    }

    template <typename Matches>
    bool setBlockIfMatching(std::int32_t x, std::int32_t y, std::int32_t z,
                            std::uint16_t state, Matches&& matches) {
        if (!verticalGeometry().containsY(y) ||
            gen::blockByState(state) == nullptr)
            return false;

        MutationGateLease mutationOrder;
        std::uint16_t oldState = 0;
        for (;;) {
            if (!generateChunkIfMissing(x >> 4, z >> 4)) return false;
            mutationOrder = lockMutationOrder();
            const auto result = storeBlockStateIf(
                x, y, z, state, matches);
            if (result.status == ConditionalBlockWriteStatus::MissingChunk) {
                mutationOrder.unlock();
                continue;
            }
            if (result.status == ConditionalBlockWriteStatus::StateMismatch)
                return false;
            oldState = result.oldState;
            break;
        }
        notifyBlockStateChanged(x, y, z, oldState, state);
        return true;
    }

    void notifyBlockStateChanged(std::int32_t x, std::int32_t y,
                                 std::int32_t z, std::uint16_t oldState,
                                 std::uint16_t state) {
        if (oldState == state) return;
        std::function<void(std::int32_t, std::int32_t, std::int32_t,
                           std::uint16_t, std::uint16_t)> blockChanged;
        {
            std::lock_guard lock(hooksMtx_);
            blockChanged = onBlockChanged_;
        }
        invokeWorldHook(blockChanged, "block-change hook",
                        x, y, z, oldState, state);
        const bool wasAir = isAirBlockState(oldState);
        const bool isAir = isAirBlockState(state);
        if (wasAir && !isAir) fireBlockPlace(x, y, z, oldState, state);
        else if (!wasAir && isAir)
            fireBlockBreak(x, y, z, oldState, state);
        else
            fireBlockNeighborChange(x, y, z, state);

        static constexpr int DX[6] = {1, -1, 0, 0, 0, 0};
        static constexpr int DY[6] = {0, 0, 1, -1, 0, 0};
        static constexpr int DZ[6] = {0, 0, 0, 0, 1, -1};
        for (int direction = 0; direction < 6; ++direction)
            fireBlockNeighborChange(x + DX[direction], y + DY[direction],
                                    z + DZ[direction], state);
        fireEdit(x >> 4, z >> 4);
    }

    void setBlockInternal(std::int32_t x, std::int32_t y, std::int32_t z, std::uint16_t state) {
        if (!verticalGeometry().containsY(y)) return;
        if (gen::blockByState(state) == nullptr) return;
        // Support updates pre-generate their chunk before taking the mutation
        // ordering lock; do not re-enter the potentially blocking loader here.
        (void)storeBlockState(x, y, z, state, false);
    }

public:

    // Double-checked, atomic lazy generation (safe under concurrent joins).
    bool generateChunkIfMissing(std::int32_t cx, std::int32_t cz) const;
    // Inline delegation kept for header-only builds; actual impl in WorldGen.cpp
private:
    void generateChunkFallback(ChunkBuildWriter& writer, std::int32_t cx,
                               std::int32_t cz) const {
        if (level_ == LevelType::Flat) fillFlat(writer);
        else fillTerrainV3(writer, cx, cz);
    }
public:
    // The const Chunk reference is borrowed only during fn. Callers must not
    // retain it or pointers/references to its members; use tryGetCopy() when
    // data must outlive this read-lock scope. The callback must also avoid
    // lock-taking APIs on this World and must not synchronously wait for work
    // that might call them: another thread cannot see this thread-local borrow
    // marker and could otherwise wait behind this shared lock.
    template <typename Fn>
        requires std::is_invocable_v<Fn, const Chunk&>
    bool withChunk(std::int32_t cx, std::int32_t cz, Fn&& fn) const {
        auto lock = lockShared();
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it == chunks_.end()) return false;
        ChunkBorrowInvocation invocation(*this);
        std::forward<Fn>(fn)(static_cast<const Chunk&>(*it->second));
        return true;
    }
    bool hasChunk(std::int32_t cx, std::int32_t cz) const {
        auto lock = lockShared();
        return chunks_.count(chunkKey(cx, cz)) != 0;
    }

    std::uint16_t getBlock(std::int32_t x, std::int32_t y, std::int32_t z) const {
        if (!verticalGeometry().containsY(y)) return 0;
        auto lock = lockShared();
        auto it = chunks_.find(chunkKey(x >> 4, z >> 4));
        if (it == chunks_.end()) return 0;
        const int lx = x & 15, lz = z & 15, wy = y - kMinY;
        return it->second->blocks_[Chunk::index(wy >> 4, wy & 15, lz, lx)];
    }
    struct BlockUpdate {
        std::int32_t x;
        std::int32_t y;
        std::int32_t z;
        std::uint16_t state;
    };
    struct BlockChange {
        std::int32_t x;
        std::int32_t y;
        std::int32_t z;
        std::uint16_t oldState;
        std::uint16_t newState;
    };
    struct BlockStateCondition {
        std::int32_t x;
        std::int32_t y;
        std::int32_t z;
        std::uint16_t state;
    };

    // Validate and prepare every affected chunk before committing any edit.
    // Repeated coordinates use last-update-wins semantics, while callbacks
    // retain the first-occurrence order and fire at most once per changed
    // coordinate. The mutation-order gate excludes concurrent mutations and
    // eviction; one World write lock publishes the complete state set to all
    // readers before any callback runs.
    bool setBlockBatch(std::span<const BlockUpdate> updates) {
        std::vector<BlockChange> changes;
        return setBlockBatchImpl(
            updates, changes,
            [this](std::span<const BlockChange> committed) {
                publishBlockBatch(committed);
            }, std::span<const BlockStateCondition>{});
    }

    // Commit a multi-block edit only if every listed position still has the
    // expected state. Conditions are checked under the same mutation gate and
    // World write lock as the edit itself, so validation cannot race another
    // writer between a separate getBlock() and setBlockBatch() call.
    bool setBlockBatchIfStates(
        std::span<const BlockUpdate> updates,
        std::span<const BlockStateCondition> conditions) {
        std::vector<BlockChange> changes;
        return setBlockBatchImpl(
            updates, changes,
            [this](std::span<const BlockChange> committed) {
                publishBlockBatch(committed);
            }, conditions);
    }

    // Publish block bytes without observer callbacks. The caller must hold
    // this World's mutation gate until it either calls publishBlockBatch or
    // restores its companion state and silently applies the prior block
    // states. Compound block/block-entity transactions use this to keep a
    // failed forward attempt invisible to simulation listeners.
    bool setBlockBatchDeferred(std::span<const BlockUpdate> updates,
                               std::vector<BlockChange>& changes) {
        if (!MutationGateLease::heldByCurrentThread(this))
            throw std::logic_error(
                "deferred block batch requires the World mutation gate");
        return setBlockBatchImpl(
            updates, changes,
            [](std::span<const BlockChange>) noexcept {},
            std::span<const BlockStateCondition>{});
    }

private:
    template <typename AfterCommit>
    bool setBlockBatchImpl(std::span<const BlockUpdate> updates,
                           std::vector<BlockChange>& changes,
                           AfterCommit&& afterCommit,
                           std::span<const BlockStateCondition> conditions) {
        changes.clear();
        if (updates.empty()) return true;
        for (const auto& update : updates) {
            if (!verticalGeometry().containsY(update.y) ||
                gen::blockByState(update.state) == nullptr)
                return false;
        }
        for (const auto& condition : conditions) {
            if (!verticalGeometry().containsY(condition.y) ||
                gen::blockByState(condition.state) == nullptr)
                return false;
        }

        struct Coordinate final {
            std::int32_t x;
            std::int32_t y;
            std::int32_t z;
            bool operator==(const Coordinate&) const noexcept = default;
        };
        struct CoordinateHash final {
            std::size_t operator()(const Coordinate& coordinate) const noexcept {
                std::size_t seed = std::hash<std::int32_t>{}(coordinate.x);
                const auto combine = [&seed](std::int32_t value) {
                    seed ^= std::hash<std::int32_t>{}(value) +
                            static_cast<std::size_t>(0x9e3779b9U) +
                            (seed << 6) + (seed >> 2);
                };
                combine(coordinate.y);
                combine(coordinate.z);
                return seed;
            }
        };
        // Collapse duplicate positions before loading chunks or committing.
        // Updating the existing entry preserves first-occurrence callback
        // order while making the final supplied state authoritative.
        std::vector<BlockUpdate> uniqueUpdates;
        uniqueUpdates.reserve(updates.size());
        std::unordered_map<Coordinate, std::size_t, CoordinateHash> updateIndex;
        updateIndex.reserve(updates.size());
        for (const auto& update : updates) {
            const Coordinate coordinate{update.x, update.y, update.z};
            const auto [entry, inserted] =
                updateIndex.emplace(coordinate, uniqueUpdates.size());
            if (inserted) uniqueUpdates.push_back(update);
            else uniqueUpdates[entry->second].state = update.state;
        }
        // Reserve the notification snapshot before publishing any bytes.
        // Appending these fixed-size records in the commit section then
        // cannot allocate or throw after a state has changed.
        changes.reserve(uniqueUpdates.size());

        for (;;) {
            for (const auto& update : uniqueUpdates) {
                if (!generateChunkIfMissing(update.x >> 4, update.z >> 4))
                    return false;
            }
            for (const auto& condition : conditions) {
                if (!generateChunkIfMissing(condition.x >> 4,
                                            condition.z >> 4))
                    return false;
            }
            auto mutationOrder = lockMutationOrder();
            auto lock = lockUnique();
            bool allChunksLoaded = true;
            for (const auto& update : uniqueUpdates) {
                if (!chunks_.count(chunkKey(update.x >> 4, update.z >> 4))) {
                    allChunksLoaded = false;
                    break;
                }
            }
            if (allChunksLoaded) {
                for (const auto& condition : conditions) {
                    if (!chunks_.count(
                            chunkKey(condition.x >> 4, condition.z >> 4))) {
                        allChunksLoaded = false;
                        break;
                    }
                }
            }
            if (!allChunksLoaded) {
                lock.unlock();
                mutationOrder.unlock();
                continue;
            }

            for (const auto& condition : conditions) {
                const auto& chunk = *chunks_.find(
                    chunkKey(condition.x >> 4, condition.z >> 4))->second;
                const int storageY = condition.y - kMinY;
                const auto current = chunk.blocks_[Chunk::index(
                    storageY >> 4, storageY & 15, condition.z & 15,
                    condition.x & 15)];
                if (current != condition.state) return false;
            }

            // No generation, callbacks, or allocating work occurs inside this
            // commit. getBlock()/withChunk readers therefore see either the
            // complete pre-batch state or the complete post-batch state.
            for (const auto& update : uniqueUpdates) {
                auto& chunk = *chunks_.find(
                    chunkKey(update.x >> 4, update.z >> 4))->second;
                const int storageY = update.y - kMinY;
                auto& block = chunk.blocks_[Chunk::index(
                    storageY >> 4, storageY & 15, update.z & 15,
                    update.x & 15)];
                const auto oldState = block;
                if (oldState == update.state) continue;
                block = update.state;
                ++chunk.revision;
                changes.push_back({update.x, update.y, update.z,
                                   oldState, update.state});
            }

            lock.unlock();
            std::invoke(std::forward<AfterCommit>(afterCommit),
                        std::span<const BlockChange>(changes));
            return true;
        }
    }

public:
    // Dispatch captured observer events after a compound state change has
    // committed. The mutation gate must remain held from the deferred write
    // through this call so competing writers cannot overtake these callbacks.
    bool publishBlockBatch(std::span<const BlockChange> changes) {
        if (!MutationGateLease::heldByCurrentThread(this))
            throw std::logic_error(
                "block batch publication requires the World mutation gate");
        bool complete = true;
        for (const auto& change : changes) {
            try {
                notifyBlockStateChanged(change.x, change.y, change.z,
                                        change.oldState, change.newState);
            } catch (...) {
                // The block batch is already committed. Continue dispatching
                // later positions, and report the incomplete observer pass
                // without making callers mistake committed state for rollback.
                complete = false;
            }
        }
        return complete;
    }

    bool setBlock(std::int32_t x, std::int32_t y, std::int32_t z,
                  std::uint16_t state) {
        if (!verticalGeometry().containsY(y)) return false;
        if (gen::blockByState(state) == nullptr) return false;
        std::uint16_t old = 0;
        MutationGateLease mutationOrder;
        for (;;) {
            // Do slow missing-chunk work before acquiring the callback-order
            // lock. If eviction wins the small race between these operations,
            // retry after releasing the lock.
            if (!generateChunkIfMissing(x >> 4, z >> 4)) return false;
            mutationOrder = lockMutationOrder();
            const auto previous = storeBlockState(x, y, z, state, true);
            if (!previous) {
                mutationOrder.unlock();
                continue;
            }
            old = *previous;
            if (old == state) return true;
            break;
        }
        // A state-identical write is not a block update in vanilla. The early
        // return above prevents simulation hooks from reprocessing stable state.
        notifyBlockStateChanged(x, y, z, old, state);
        return true;
    }

    // Atomically replace a particular state after loading its persisted chunk.
    // This is used when stale unloaded-chunk reads must not turn a saved block
    // into air and then overwrite it during a later lazy load.
    bool setBlockIfState(std::int32_t x, std::int32_t y, std::int32_t z,
                         std::uint16_t expectedState, std::uint16_t state) {
        if (gen::blockByState(expectedState) == nullptr) return false;
        return setBlockIfMatching(
            x, y, z, state,
            [expectedState](std::uint16_t currentState) {
                return currentState == expectedState;
            });
    }

    bool setBlockIfAir(std::int32_t x, std::int32_t y, std::int32_t z,
                       std::uint16_t state) {
        return setBlockIfMatching(
            x, y, z, state,
            [](std::uint16_t currentState) {
                return isAirBlockState(currentState);
            });
    }
    // Block state updates loop over six neighbors and notify subscribers.
    bool updateBlockState(std::int32_t x, std::int32_t y, std::int32_t z,
                          std::uint16_t newState) {
        MutationGateLease mutationOrder;
        if (verticalGeometry().containsY(y)) {
            for (;;) {
                if (!generateChunkIfMissing(x >> 4, z >> 4)) return false;
                mutationOrder = lockMutationOrder();
                bool loaded = false;
                {
                    auto lock = lockShared();
                    loaded = chunks_.count(chunkKey(x >> 4, z >> 4)) != 0;
                }
                if (!loaded) {
                    mutationOrder.unlock();
                    continue;
                }
                // While the ordering lock is held, setBlock's generation
                // check is a read-only fast path; eviction cannot win here.
                if (!setBlock(x, y, z, newState)) return false;
                break;
            }
        } else {
            return false;
        }
        static constexpr int DX[6] = {1,-1,0,0,0,0};
        static constexpr int DY[6] = {0,0,1,-1,0,0};
        static constexpr int DZ[6] = {0,0,0,0,1,-1};
        for (int d = 0; d < 6; ++d) {
            onBlockNeighborChange(x + DX[d], y + DY[d], z + DZ[d], newState);
        }
        return true;
    }
    struct BlockPosI { std::int32_t x=0, y=0, z=0; };
    bool updateBlockState(BlockPosI pos, std::uint16_t newState) {
        return updateBlockState(pos.x, pos.y, pos.z, newState);
    }
    // Return an independently owned snapshot. Chunk's normal copy operation
    // detaches its optional skylight cache as well as value-copying block data.
    std::optional<Chunk> tryGetCopy(std::int32_t cx, std::int32_t cz) const {
        auto lock = lockShared();
        auto it = chunks_.find(chunkKey(cx, cz));
        if (it == chunks_.end()) return std::nullopt;
        return *it->second;
    }
    std::vector<std::int64_t> allChunkKeys() const {
        auto lock = lockShared();
        std::vector<std::int64_t> out;
        out.reserve(chunks_.size());
        for (auto& kv : chunks_) out.push_back(kv.first);
        return out;
    }
    // Sum of revisions of the chunk (cheap "did anything change" probe).
    std::uint64_t revisionAt(std::int32_t cx, std::int32_t cz) const {
        auto lock = lockShared();
        auto it = chunks_.find(chunkKey(cx, cz));
        return it == chunks_.end() ? 0 : it->second->revision;
    }
    bool eraseChunk(std::int32_t cx, std::int32_t cz) {
        return eraseChunkIf(cx, cz, [] { return true; });
    }

    // Serialize the caller's final eligibility check with block mutations and
    // their on-edit callbacks. Persistence-backed eviction uses this to avoid
    // deleting a chunk that became dirty after an earlier unlocked probe.
    template <typename Predicate>
    bool eraseChunkIf(std::int32_t cx, std::int32_t cz,
                      Predicate&& mayErase) {
        auto mutationOrder = lockMutationOrder();
        if (!mayErase()) return false;
        // Keep generation and eviction in the same order so a concurrent
        // border crossing cannot race the bounded allocation pool.
        std::lock_guard generationLock(generationMtx_);
        std::unique_ptr<Chunk> reclaimed;
        {
            auto lock = lockUnique();
            auto it = chunks_.find(chunkKey(cx, cz));
            if (it == chunks_.end()) return false;
            reclaimed = std::move(it->second);
            chunks_.erase(it);
        }
        recycleChunk(std::move(reclaimed));
        return true;
    }
    // Publish a fully decoded persisted chunk without per-block gameplay hooks.
    // A resident chunk is authoritative even at revision zero: it may already
    // have run world-generation side effects that are not safe to duplicate or
    // roll back when an async disk read arrives late. Returns true if a
    // resident chunk is available afterward, false when the detached load was
    // rejected as stale.
    bool installLoadedChunk(std::int32_t cx, std::int32_t cz, Chunk chunk,
                            std::function<void()> publishCallback = {}) {
        // Coordinate disk publication with generation for this key. Without
        // this, generation can run structure placement (and queue side effects)
        // while a concurrently completed disk load wins the final try_emplace.
        auto key = chunkKey(cx, cz);
        // Match generateChunkIfMissing's publication order. Holding the build
        // stripe while waiting for callback order can deadlock with a block
        // hook that owns callback order and needs this chunk.
        auto mutationOrder = lockMutationOrder();
        auto buildLock = lockChunkBuild(key);
        {
            auto lock = lockShared();
            if (chunks_.count(key) != 0) {
                lock.unlock();
                discardPendingLoadedChunkIfAny(key);
                return true;
            }
        }
        const auto pendingResult = publishPendingLoadedChunkLocked(key);
        if (pendingResult != PendingLoadedChunkResult::NotFound)
            return pendingResult != PendingLoadedChunkResult::Stale;
        auto candidate = std::make_unique<Chunk>(std::move(chunk));
        ++candidate->revision;
        // Chunk extras are transactional sidecar data. Prepare the map node
        // and capacity before publication so an allocation failure cannot
        // leave extras committed without their corresponding chunk.
        try {
            (void)publishChunkCandidateLocked(key, candidate,
                                              publishCallback);
        } catch (const StaleChunkPublishError&) {
            // The loader owner changed while this detached candidate was in
            // flight. Its side data is no longer safe to publish; discard the
            // candidate so a later request can load it under the new owner.
            return false;
        }
        return hasChunk(cx, cz);
    }
    std::size_t loadedChunkCount() const {
        auto lock = lockShared();
        return chunks_.size();
    }
    // Coordinate mutations in separately owned per-chunk systems (such as
    // block entities) with final chunk eviction. The mutex is recursive so a
    // dirty callback triggered by a World edit can safely re-enter this gate.
    MutationGateLease lockMutationGate() const {
        return lockMutationOrder();
    }
    // Load outside the mutation gate, then retry if an eviction won the gap.
    // On success the returned lease keeps this chunk resident until the caller
    // completes its compound world/block-entity mutation.
    std::optional<MutationGateLease>
    lockLoadedChunkMutationGate(std::int32_t cx, std::int32_t cz) const {
        for (;;) {
            if (!generateChunkIfMissing(cx, cz)) return std::nullopt;
            auto mutationOrder = lockMutationOrder();
            if (!hasChunk(cx, cz)) continue;
            return std::optional<MutationGateLease>{std::move(mutationOrder)};
        }
    }
    const void* mutationGateIdentity() const noexcept { return this; }
    void addSpawnTicket(std::int32_t cx, std::int32_t cz, std::int64_t tick = 0) {
        auto mutationOrder = lockMutationOrder();
        auto lock = lockUnique();
        // W17 strict: SPAWN tickets must NOT pollute ForcedChunks persistence (avoid maxLoadedChunks inflation)
        // Only add to ticketManager, not forcedChunks_ set (Yarn spawn chunk loader is transient)
        ticketManager_.addTicket(cx, cz, TicketType::SPAWN, 31, tick);
    }
    bool isForced(std::int32_t cx, std::int32_t cz) const {
        auto lock = lockShared();
        return forcedChunks_.count(chunkKey(cx, cz)) != 0;
    }
    bool isForcedKey(std::int64_t k) const {
        auto lock = lockShared();
        return forcedChunks_.count(k) != 0;
    }
    bool hasTicket(std::int32_t cx, std::int32_t cz, TicketType t) const {
        auto lock = lockShared();
        return ticketManager_.hasTicket(cx, cz, t);
    }
    int ticketLevel(std::int32_t cx, std::int32_t cz) const {
        auto lock = lockShared();
        return ticketManager_.getMinLevel(cx, cz);
    }
    std::vector<std::int64_t> forcedChunkKeys() const {
        auto lock = lockShared();
        std::vector<std::int64_t> out(forcedChunks_.begin(), forcedChunks_.end());
        return out;
    }
    void clearForcedChunks() {
        auto mutationOrder = lockMutationOrder();
        auto lock = lockUnique();
        forcedChunks_.clear();
        ticketManager_.clear();
    }
    void restoreForcedChunk(std::int32_t cx, std::int32_t cz) {
        auto mutationOrder = lockMutationOrder();
        auto lock = lockUnique();
        forcedChunks_.insert(chunkKey(cx, cz));
        ticketManager_.addTicket(cx, cz, TicketType::FORCED, 31, 0);
    }
    struct ForcedChunkState {
        static inline std::int64_t toLong(int cx,int cz){ return chunkKey(cx,cz); }
    };
    bool setChunkForced(int cx,int cz,bool forced){
        auto mutationOrder = lockMutationOrder();
        std::int64_t k = ForcedChunkState::toLong(cx,cz);
        auto lock = lockUnique();
        if(forced){
            if(forcedChunks_.insert(k).second){ ticketManager_.addTicket(cx,cz,TicketType::FORCED,31,0); return true; }
            return false;
        } else {
            if(forcedChunks_.erase(k)){ ticketManager_.removeTicket(cx,cz,TicketType::FORCED); return true; }
            return false;
        }
    }
    std::vector<std::int64_t> getForcedChunks() const {
        auto lock = lockShared();
        return std::vector<std::int64_t>(forcedChunks_.begin(), forcedChunks_.end());
    }
    std::vector<std::int64_t> getForcedChunksSnapshot() const { return getForcedChunks(); }
    void truncateForcedChunksIfNeeded(){
        auto mutationOrder = lockMutationOrder();
        auto lock = lockUnique();
        if(forcedChunks_.size()<=constants::kMaxForcedChunks) return;
        std::fprintf(stderr,"[World] ForcedChunks %zu >%d, truncating to %d (vanilla limit)\n", forcedChunks_.size(), constants::kMaxForcedChunks, constants::kMaxForcedChunks);
        std::unordered_set<std::int64_t> truncated;
        truncated.reserve(constants::kMaxForcedChunks);
        int c=0;
        for(auto k: forcedChunks_){ if(c++>=constants::kMaxForcedChunks) break; truncated.insert(k); }
        for (const auto key : forcedChunks_) {
            if (truncated.contains(key)) continue;
            const auto [cx, cz] = chunkKeyDecode(key);
            ticketManager_.removeTicket(cx, cz, TicketType::FORCED);
        }
        forcedChunks_.swap(truncated);
    }

    const std::string& biomeKey() const { return biome_; }

    // Biome codec wiring: resolve biome key <-> synced registry index. Must be installed before any chunk generation (GameServer::init).
    // Dimension identity is fixed from LevelType in the constructor. Keeping
    // this compatibility check write-free makes detached chunk generation and
    // concurrent persistence readers observe one geometry for World lifetime.
    bool setDimensionId(std::int8_t d) const noexcept {
        return d == dimensionId_;
    }
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
            auto lock = lockShared();
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
            auto lock = lockShared();
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
    void fillTerrainV3(ChunkBuildWriter& writer, std::int32_t cx,
                       std::int32_t cz) const;
    void fillNether(ChunkBuildWriter& writer, std::int32_t cx,
                    std::int32_t cz) const;
    void fillEnd(ChunkBuildWriter& writer, std::int32_t cx,
                 std::int32_t cz) const;

public:
    void fillTerrain(ChunkBuildWriter& writer, std::int32_t cx,
                     std::int32_t cz) const {
        fillTerrainV3(writer, cx, cz);
    }

    void fillFlat(ChunkBuildWriter& writer) const {
        const auto& map = gen::blockNameToState();
        const std::uint16_t bedrock = static_cast<std::uint16_t>(map.at("minecraft:bedrock"));
        const std::uint16_t dirt    = static_cast<std::uint16_t>(map.at("minecraft:dirt"));
        const std::uint16_t grass   = static_cast<std::uint16_t>(map.at("minecraft:grass_block"));
        for (int y = kMinY; y <= -61; ++y) {
            const std::uint16_t st = (y == -64) ? bedrock : (y == -61 ? grass : dirt);
            const int sec = ((y - kMinY) >> 4), yi = (y - kMinY) & 15;
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x)
                    writer.blocks.set(Chunk::index(sec, yi, z, x), st);
        }
    }

private:
    mutable std::shared_mutex mutex_;
    // Serializes block commits and callback sequences across writer threads.
    // It is recursive because a hook may synchronously mutate World again;
    // such nested notifications are intentionally depth-first.
    mutable std::recursive_mutex blockMutationOrderMtx_;
    // Hook configuration is normally installed during server bootstrap, but
    // Persistence clears its callbacks during teardown while worker/session
    // threads may still be finishing a read or edit.  Protect both reads and
    // replacement, and invoke copied callbacks outside this mutex so a hook
    // may safely re-enter World or replace another hook.
    mutable std::mutex hooksMtx_;
    mutable std::condition_variable editHookQuiescent_;
    mutable std::size_t activeEditHooks_ = 0;
    // Protects the recycled-allocation pool and serializes worldgen fallback
    // (whose generator-owned caches are not independently synchronized). The
    // I/O loader runs outside this gate; per-key build stripes coordinate
    // duplicate callers without serializing unrelated disk reads.
    mutable std::mutex generationMtx_;
    mutable bool chunkLoadStarted_ = false;
    mutable std::array<std::mutex, kChunkBuildLockCount> chunkBuildMtx_{};
    // Successfully decoded synchronous loads wait here while changing from
    // build-stripe order to mutation-order-first publication. Readers only
    // hold pendingLoadedMtx_ for the map operation; never across a wait or a
    // callback.
    mutable std::mutex pendingLoadedMtx_;
    mutable std::unordered_map<std::int64_t,
                               std::shared_ptr<PendingLoadedChunk>>
        pendingLoadedChunks_;
#ifdef CPPFM_WORLD_MUTATION_TEST
    mutable std::atomic<std::uint32_t> chunkBuildWaiters_{0};
    mutable std::atomic<std::uint32_t> mutationOrderWaiters_{0};
    mutable std::atomic<std::uint32_t> editHookWaitersForTest_{0};
#endif
    mutable std::unordered_map<std::int64_t, std::unique_ptr<Chunk>> chunks_;
    mutable std::vector<std::unique_ptr<Chunk>> recycledChunks_;
    mutable std::unordered_set<std::int64_t> forcedChunks_;
    mutable ChunkTicketManager ticketManager_;
    std::string biome_;
    LevelType level_;
    TerrainGenerator terrain_;
    std::atomic<std::uint64_t> srv_seed;
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
    std::function<ChunkLoadStatus(std::int32_t, std::int32_t,
                                  ChunkBuildWriter&)> loader_;
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
