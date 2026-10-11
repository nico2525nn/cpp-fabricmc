// Persistence: Anvil region files + background flusher (plan.md Phase 1).
#pragma once
#include "World.hpp"
#include "Anvil.hpp"
#include "WorldDataManager.hpp"
#include "Constants.hpp"
#include "platform/StorageFileOps.hpp"
#include <functional>
#include "RegionFile.hpp"
#include "StagedRegionRecovery.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppfm {

namespace persistence_callback_detail {
struct Frame final {
    const void* state = nullptr;
    bool edit = false;
    bool stopAllowed = true;
    Frame* previous = nullptr;
};

inline thread_local Frame* currentFrame = nullptr;

inline std::size_t activeOnCurrentThread(const void* state,
                                         bool editsOnly) noexcept {
    std::size_t count = 0;
    for (auto* frame = currentFrame; frame; frame = frame->previous) {
        if (frame->state == state && (!editsOnly || frame->edit)) ++count;
    }
    return count;
}

inline bool stopDisallowedOnCurrentThread(const void* state) noexcept {
    for (auto* frame = currentFrame; frame; frame = frame->previous) {
        if (frame->state == state && !frame->stopAllowed) return true;
    }
    return false;
}

class StopDisallowedCallbackScope final {
public:
    StopDisallowedCallbackScope(const void* state,
                                std::atomic<std::size_t>& activeCallbacks) noexcept
        : activeCallbacks_(&activeCallbacks),
          frame_{state, false, false, currentFrame} {
        activeCallbacks_->fetch_add(1, std::memory_order_acq_rel);
        currentFrame = &frame_;
    }

    ~StopDisallowedCallbackScope() {
        currentFrame = frame_.previous;
        activeCallbacks_->fetch_sub(1, std::memory_order_acq_rel);
    }

    StopDisallowedCallbackScope(const StopDisallowedCallbackScope&) = delete;
    StopDisallowedCallbackScope& operator=(
        const StopDisallowedCallbackScope&) = delete;

private:
    std::atomic<std::size_t>* activeCallbacks_;
    Frame frame_;
};
} // namespace persistence_callback_detail

class Persistence {
    using ChunkNbtTemplateMap =
        std::unordered_map<std::int64_t, nbt::Value>;

    enum class CallbackKind { LoaderOrPublication, Edit };

    struct CallbackState {
        // Short-lived lock protects owner admission and in-flight counts only;
        // disk IO, NBT parsing, and user hooks must not serialize unrelated
        // chunk callbacks.
        std::mutex mutex;
        std::condition_variable quiescent;
        Persistence* owner = nullptr;
        bool acceptingCallbacks = true;
        bool acceptingEdits = false;
        std::uint64_t generation = 0;
        std::size_t activeCallbacks = 0;
        std::size_t activeEdits = 0;
        std::atomic<std::size_t> activeSaveCallbacks{0};
#ifdef CPPFM_WORLD_MUTATION_TEST
        std::size_t waitersForTest = 0;
        std::function<void()> beforeEditLeaseForTest;
#endif
    };

    class CallbackLease final {
    public:
        CallbackLease(std::shared_ptr<CallbackState> state, CallbackKind kind,
                      std::optional<std::uint64_t> expectedGeneration =
                          std::nullopt)
            : state_(std::move(state)), kind_(kind) {
            std::lock_guard lock(state_->mutex);
            const bool accepting = kind_ == CallbackKind::Edit
                ? state_->acceptingEdits : state_->acceptingCallbacks;
            if (!state_->owner || !accepting ||
                (expectedGeneration &&
                 state_->generation != *expectedGeneration))
                return;

            owner_ = state_->owner;
            ++state_->activeCallbacks;
            if (kind_ == CallbackKind::Edit) ++state_->activeEdits;
            frame_ = {state_.get(), kind_ == CallbackKind::Edit, true,
                      persistence_callback_detail::currentFrame};
            persistence_callback_detail::currentFrame = &frame_;
        }

        ~CallbackLease() {
            if (!owner_) return;
            persistence_callback_detail::currentFrame = frame_.previous;
            {
                std::lock_guard lock(state_->mutex);
                --state_->activeCallbacks;
                if (kind_ == CallbackKind::Edit) --state_->activeEdits;
            }
            state_->quiescent.notify_all();
        }

        CallbackLease(const CallbackLease&) = delete;
        CallbackLease& operator=(const CallbackLease&) = delete;
        CallbackLease(CallbackLease&&) = delete;
        CallbackLease& operator=(CallbackLease&&) = delete;

        Persistence* owner() const noexcept { return owner_; }

        bool ownerIsCurrent(std::uint64_t expectedGeneration) const {
            if (!owner_) return false;
            std::lock_guard lock(state_->mutex);
            return state_->owner == owner_ &&
                   state_->generation == expectedGeneration;
        }

    private:
        std::shared_ptr<CallbackState> state_;
        CallbackKind kind_;
        Persistence* owner_ = nullptr;
        persistence_callback_detail::Frame frame_;
    };

    class FlushCallLease final {
    public:
        explicit FlushCallLease(Persistence& owner)
            : owner_(&owner), admitted_(owner.enterFlushCall()) {}

        ~FlushCallLease() {
            if (admitted_) owner_->leaveFlushCall();
        }

        FlushCallLease(const FlushCallLease&) = delete;
        FlushCallLease& operator=(const FlushCallLease&) = delete;

        bool admitted() const noexcept { return admitted_; }

    private:
        Persistence* owner_;
        bool admitted_;
    };

public:
    struct ChunkBytesRead final {
        enum class Status { Loaded, Missing, Failed } status = Status::Missing;
        std::vector<std::uint8_t> bytes;
        std::string error;
    };
    enum class StopStatus { StoppedCleanly, UnsavedData, Deferred };
    using StorageRole = WorldDataManager::StorageRole;

    Persistence(World& world, std::string worldDir, std::string biomeKey,
                StorageRole storageRole = StorageRole::WorldRoot)
        : world_(world), dir_(std::move(worldDir)),
          worldDataManager_(dir_, storageRole), biome_(std::move(biomeKey)) {
        std::lock_guard lock(callbackState_->mutex);
        callbackState_->owner = this;
        callbackState_->acceptingCallbacks = true;
    }

    // ---- level.dat ----
    void setLevelStateProvider(
        std::function<void(nbt::Value& data)> provider,
        std::function<void(const nbt::Value& data)> consumer) {
        {
            std::lock_guard lock(configMtx_);
            provideLevelState_ = std::move(provider);
            consumeLevelState_ = std::move(consumer);
        }
        std::function<void(nbt::Value&)> providerCopy;
        std::function<void(const nbt::Value&)> consumerCopy;
        {
            std::lock_guard lock(configMtx_);
            providerCopy = provideLevelState_;
            consumerCopy = consumeLevelState_;
        }
        worldDataManager_.setLevelStateProvider(std::move(providerCopy),
                                                 std::move(consumerCopy));
    }

    // Optional hooks: block-entity + entity NBT attached to saved chunks.
    // For publication, prefer prepareReadFn: it must finish fallible decoding
    // first and return a staged commit closure. The legacy readFn is retained
    // for no-throw/transactional adapters and is called without Persistence's
    // metadata locks so it can safely re-enter lifecycle operations.
    void setChunkExtras(
        std::function<void(std::int32_t, std::int32_t, nbt::Value&)> writeFn,
        std::function<void(const nbt::Value&)> readFn,
        std::function<void(const nbt::Value&)> validateFn = {},
        std::function<std::function<void()>(const nbt::Value&)>
            prepareReadFn = {}) {
        std::lock_guard lock(configMtx_);
        writeExtras_ = std::move(writeFn);
        readExtras_ = std::move(readFn);
        validateExtras_ = std::move(validateFn);
        prepareReadExtras_ = std::move(prepareReadFn);
    }

#ifdef CPPFM_PERSISTENCE_PUBLICATION_TEST_HOOKS
    static void setPublicationPreparedHookForTesting(
        std::function<void()> hook) {
        publicationPreparedHookForTesting() = std::move(hook);
    }

    std::string biomeKeyForTesting() {
        std::lock_guard lock(bioMtx_);
        return biomeOverride_.value_or(biome_);
    }
#endif

    // Runs immediately before a chunk snapshot, including background flushes.
    // Stateful subsystems use this to materialize transient block entities
    // before the serializer intentionally omits them from persistent NBT.
    void setChunkSaveBarrier(std::function<void(std::int32_t, std::int32_t)> barrier) {
        std::lock_guard lock(configMtx_);
        chunkSaveBarrier_ = std::move(barrier);
    }
    // A denied save remains dirty and is retried on a later flush. This is
    // used by world-generation side queues whose state must enter chunk NBT
    // before a snapshot may be committed.
    void setChunkSaveAdmission(
        std::function<bool(std::int32_t, std::int32_t)> admission) {
        std::lock_guard lock(configMtx_);
        chunkSaveAdmission_ = std::move(admission);
    }
    void setChunkSaveComplete(
        std::function<void(std::int32_t, std::int32_t)> callback) {
        std::lock_guard lock(configMtx_);
        chunkSaveComplete_ = std::move(callback);
    }
    void setBiomeCodec(std::unordered_map<std::uint16_t, std::string> idxToKey,
                       std::int32_t defaultIdx) {
        std::lock_guard lock(configMtx_);
        biomeIdxToKey_ = std::move(idxToKey);
        defaultBiomeIndex_ = defaultIdx;
        biomeKeyToIdx_.clear();
        for (auto& [k, v] : biomeIdxToKey_)
            biomeKeyToIdx_[v] = k;
    }

    void setDifficulty(const std::string& d) {
        std::lock_guard lock(configMtx_);
        difficulty_ = d;
    }
    void setWorldBorder(double diameter, double cx=0, double cz=0) {
        std::lock_guard lock(configMtx_);
        worldBorderDiameter_ = diameter; worldBorderCenterX_=cx; worldBorderCenterZ_=cz;
        worldBorderLerpFrom_ = diameter; worldBorderLerpTo_ = diameter; worldBorderLerpMs_ = 0;
        worldBorderLerpRemainingTicks_ = 0; worldBorderLerpTotalTicks_ = 0;
    }
    void setWorldBorderLerp(double from, double to, std::int64_t remainingTicks) {
        std::lock_guard lock(configMtx_);
        worldBorderLerpFrom_ = from; worldBorderLerpTo_ = to;
        worldBorderLerpMs_ = remainingTicks * 50;
        worldBorderLerpRemainingTicks_ = remainingTicks;
        worldBorderLerpTotalTicks_ = remainingTicks;
        worldBorderDiameter_ = from;
        if (remainingTicks <= 0) worldBorderDiameter_ = to;
    }
    // interpolate per tick — Yarn WorldBorder.tick() / interpolateSize
    bool tickWorldBorder() {
        std::lock_guard lock(configMtx_);
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
    std::string difficulty() const {
        std::lock_guard lock(configMtx_);
        return difficulty_;
    }
    double worldBorderDiameter() const {
        std::lock_guard lock(configMtx_);
        return worldBorderDiameter_;
    }
    double worldBorderCenterX() const {
        std::lock_guard lock(configMtx_);
        return worldBorderCenterX_;
    }
    double worldBorderCenterZ() const {
        std::lock_guard lock(configMtx_);
        return worldBorderCenterZ_;
    }
    double worldBorderLerpFrom() const {
        std::lock_guard lock(configMtx_);
        return worldBorderLerpFrom_;
    }
    double worldBorderLerpTo() const {
        std::lock_guard lock(configMtx_);
        return worldBorderLerpTo_;
    }
    std::int64_t worldBorderLerpMs() const {
        std::lock_guard lock(configMtx_);
        return worldBorderLerpMs_;
    }
    std::int64_t worldBorderLerpRemainingTicks() const {
        std::lock_guard lock(configMtx_);
        return worldBorderLerpRemainingTicks_;
    }

    void saveLevelData(std::int64_t worldTicks = 0, std::int64_t dayTime = 0) {
        if (!worldDataManager_.ownsLevelData()) return;
        // Use WorldDataManager for atomic write + version handling
        worldDataManager_.setDirectory(dir_);
        std::function<void(nbt::Value&)> provider;
        std::function<void(const nbt::Value&)> consumer;
        std::string difficulty;
        double diameter = 0.0;
        double centerX = 0.0;
        double centerZ = 0.0;
        double lerpTgt = 0.0;
        std::int64_t lerpMs = 0;
        {
            std::lock_guard lock(configMtx_);
            provider = provideLevelState_;
            consumer = consumeLevelState_;
            difficulty = difficulty_;
            diameter = worldBorderDiameter_;
            centerX = worldBorderCenterX_;
            centerZ = worldBorderCenterZ_;
            lerpTgt = worldBorderLerpRemainingTicks_ > 0
                ? worldBorderLerpTo_ : worldBorderDiameter_;
            lerpMs = worldBorderLerpMs_;
        }
        worldDataManager_.setLevelStateProvider(std::move(provider),
                                                std::move(consumer));
        bool ok = worldDataManager_.saveLevelDataWithProviders(worldTicks, dayTime, world_,
                                                               difficulty, diameter, centerX, centerZ,
                                                               lerpTgt, lerpMs);
        if (!ok)
            std::fprintf(stderr, "[Persistence] level.dat save was rejected by WorldDataManager\n");
    }
    void loadLevelData() {
        if (!worldDataManager_.ownsLevelData()) return;
        worldDataManager_.setDirectory(dir_);
        std::function<void(nbt::Value&)> provider;
        std::function<void(const nbt::Value&)> consumer;
        std::string difficulty;
        double diameter = 0.0;
        double centerX = 0.0;
        double centerZ = 0.0;
        {
            std::lock_guard lock(configMtx_);
            provider = provideLevelState_;
            consumer = consumeLevelState_;
            difficulty = difficulty_;
            diameter = worldBorderDiameter_;
            centerX = worldBorderCenterX_;
            centerZ = worldBorderCenterZ_;
        }
        worldDataManager_.setLevelStateProvider(std::move(provider),
                                                std::move(consumer));
        // try via manager (handles DataFixerUpper version check + atomic read) — include lerp
        double lerpTgt = diameter;
        std::int64_t lerpMs = 0;
        bool ok = worldDataManager_.loadLevelData(
            world_, difficulty, diameter, centerX, centerZ, &lerpTgt, &lerpMs);
        for (const auto& ln : worldDataManager_.lastRecovery().logLines)
            std::fprintf(stderr, "[cppfm]%s\n", ln.c_str());
        if (ok) {
            std::lock_guard lock(configMtx_);
            difficulty_ = std::move(difficulty);
            worldBorderDiameter_ = diameter;
            worldBorderCenterX_ = centerX;
            worldBorderCenterZ_ = centerZ;
            worldBorderLerpFrom_ = worldBorderDiameter_;
            worldBorderLerpTo_ = lerpTgt;
            worldBorderLerpMs_ = lerpMs;
            worldBorderLerpRemainingTicks_ = (lerpMs + 49) / 50;
            worldBorderLerpTotalTicks_ = worldBorderLerpRemainingTicks_;
            if (lerpMs == 0) {
                worldBorderLerpFrom_ = worldBorderDiameter_;
                worldBorderLerpTo_ = worldBorderDiameter_;
            }
            return;
        }
    }

private:
    static void waitForCallbackQuiescence(
        const std::shared_ptr<CallbackState>& state, CallbackKind kind) {
        const bool editsOnly = kind == CallbackKind::Edit;
        const auto ownCallbacks = persistence_callback_detail::
            activeOnCurrentThread(state.get(), editsOnly);
        std::unique_lock lock(state->mutex);
        const auto quiescent = [&] {
            return editsOnly ? state->activeEdits <= ownCallbacks
                             : state->activeCallbacks <= ownCallbacks;
        };
        if (quiescent()) return;
#ifdef CPPFM_WORLD_MUTATION_TEST
        ++state->waitersForTest;
        state->quiescent.notify_all();
#endif
        state->quiescent.wait(lock, quiescent);
#ifdef CPPFM_WORLD_MUTATION_TEST
        --state->waitersForTest;
#endif
        lock.unlock();
        state->quiescent.notify_all();
    }

    friend struct WorldMutationContractTestAccess;

    void setDirtyAdmission(bool accepting) {
        std::unique_lock lock(dirtyAdmissionMtx_, std::defer_lock);
#ifdef CPPFM_WORLD_MUTATION_TEST
        if (!lock.try_lock()) {
            dirtyAdmissionWaitersForTest_.fetch_add(
                1, std::memory_order_acq_rel);
            try {
                lock.lock();
            } catch (...) {
                dirtyAdmissionWaitersForTest_.fetch_sub(
                    1, std::memory_order_acq_rel);
                throw;
            }
            dirtyAdmissionWaitersForTest_.fetch_sub(
                1, std::memory_order_acq_rel);
        }
#else
        lock.lock();
#endif
        acceptingDirty_.store(accepting, std::memory_order_release);
    }

public:
    void start() {
        if (persistence_callback_detail::activeOnCurrentThread(
                callbackState_.get(), false) != 0 ||
            callbackState_->activeSaveCallbacks.load(
                std::memory_order_acquire) != 0 ||
            stopInProgress_.load(std::memory_order_acquire)) {
            std::fprintf(stderr,
                         "[Persistence] start() is unavailable during save callbacks or shutdown\n");
            return;
        }
        std::unique_lock lifecycleLock(lifecycleMtx_, std::try_to_lock);
        if (!lifecycleLock.owns_lock() ||
            stopInProgress_.load(std::memory_order_acquire))
            return;
        const auto regionDirectory = std::filesystem::path(dir_) / "region";
        std::error_code directoryError;
        if (!storage_file_ops::createDirectoriesDurably(
                regionDirectory, directoryError))
            throw std::filesystem::filesystem_error(
                "create durable region directory", regionDirectory,
                directoryError);
        if (running_.load(std::memory_order_acquire) || worker_.joinable()) return;
        reopenFlushCallAdmission();
        std::uint64_t callbackGeneration = 0;
        {
            std::lock_guard callbackLock(callbackState_->mutex);
            if (++callbackState_->generation == 0)
                ++callbackState_->generation;
            callbackGeneration = callbackState_->generation;
            callbackState_->owner = this;
            callbackState_->acceptingCallbacks = true;
            callbackState_->acceptingEdits = true;
        }
        setDirtyAdmission(true);
        const auto callbackState = callbackState_;
        world_.setLoaderWithStatus(
            [callbackState, callbackGeneration](
                std::int32_t cx, std::int32_t cz,
                ChunkBuildWriter& writer) {
                CallbackLease callback(callbackState,
                                       CallbackKind::LoaderOrPublication,
                                       callbackGeneration);
                auto* owner = callback.owner();
                return owner ? owner->loadChunkStatus(
                                   cx, cz, writer, callbackGeneration)
                             : ChunkLoadStatus::Failed;
            });
        world_.setOnEdit([callbackState](std::int32_t cx, std::int32_t cz) {
#ifdef CPPFM_WORLD_MUTATION_TEST
            std::function<void()> beforeLease;
            {
                std::lock_guard lock(callbackState->mutex);
                beforeLease = callbackState->beforeEditLeaseForTest;
            }
            if (beforeLease) beforeLease();
#endif
            CallbackLease callback(callbackState, CallbackKind::Edit);
            if (auto* owner = callback.owner()) owner->markDirty(cx, cz);
        });
        running_.store(true, std::memory_order_release);
        try {
            worker_ = std::thread([this] { loop(); });
        } catch (...) {
            running_.store(false, std::memory_order_release);
            world_.clearOnEditAndWait();
            setDirtyAdmission(false);
            world_.clearLoader();
            {
                std::lock_guard callbackLock(callbackState_->mutex);
                callbackState_->acceptingEdits = false;
                callbackState_->acceptingCallbacks = false;
                callbackState_->owner = nullptr;
            }
            waitForCallbackQuiescence(callbackState_,
                                      CallbackKind::LoaderOrPublication);
            throw;
        }
    }
    // Lifecycle transitions are not reentrant from save hooks. A call made
    // while any save hook is active returns false without beginning teardown;
    // the owning server retries after the hook and admitted synchronous saves
    // have quiesced.
    StopStatus stopWithStatus() noexcept {
        if (persistence_callback_detail::stopDisallowedOnCurrentThread(
                callbackState_.get())) {
            std::fprintf(stderr,
                         "[Persistence] stop() is not allowed from a save callback\n");
            return StopStatus::Deferred;
        }
        bool expectedStopInProgress = false;
        if (!stopInProgress_.compare_exchange_strong(
                expectedStopInProgress, true, std::memory_order_acq_rel,
                std::memory_order_acquire))
            return StopStatus::Deferred;
#ifdef CPPFM_WORLD_MUTATION_TEST
        if (beforeStopAdmissionCheckForTest_) {
            try {
                beforeStopAdmissionCheckForTest_();
            } catch (...) {
                std::fprintf(stderr,
                             "[Persistence] test stop-admission hook threw\n");
            }
        }
#endif
        struct StopFlagReset final {
            std::atomic<bool>& flag;
            ~StopFlagReset() { flag.store(false, std::memory_order_release); }
        } resetStopFlag{stopInProgress_};
        if (callbackState_->activeSaveCallbacks.load(
                std::memory_order_acquire) != 0) {
            // Do not let a save hook (or a helper thread it synchronously
            // waits for) attempt to join the worker currently executing it.
            // The caller can retry once the hook returns; server shutdown
            // does so in its persistence-drain loop.
            return StopStatus::Deferred;
        }
        std::unique_lock lifecycleLock(lifecycleMtx_);
        // Stop the periodic worker but keep edit admission active while final
        // save barriers materialize transient state. A piston barrier can
        // mutate neighboring chunks during this phase, and both its explicit
        // dirty callback and the normal World hook must still be accepted.
        running_.exchange(false, std::memory_order_acq_rel);
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        closeFlushCallAdmissionAndWait();
        const auto flushFinalBatch = [this](bool runSaveBarriers) {
            // Failed writes remain dirty and are retried a bounded number of
            // times so shutdown does not discard a chunk after one transient
            // I/O error.
            constexpr int kMaxFinalFlushPasses = 8;
            for (int pass = 0; pass < kMaxFinalFlushPasses; ++pass) {
                bool pending = false;
                {
                    std::lock_guard lk(dirtyMtx_);
                    pending = !dirty_.empty();
                }
                if (!pending) break;
                try {
                    flushOnce(runSaveBarriers);
                } catch (const std::exception& e) {
                    std::fprintf(stderr,
                                 "[Persistence] final flush failed: %s\n",
                                 e.what());
                } catch (...) {
                    std::fprintf(stderr,
                                 "[Persistence] final flush failed\n");
                }
            }
        };
        flushFinalBatch(true);
        // An edit can commit its block state before it reaches fireEdit().
        // Quiesce the World's mutation order before detaching the hook so
        // stop cannot slip through that gap and miss the edit's dirty mark.
        // Release the gate before the next flush: its save barriers may enter
        // Redstone and must not run under the World gate.
        auto editDrainGate = world_.lockMutationGate();
        world_.clearOnEditAndWait();
        {
            std::lock_guard callbackLock(callbackState_->mutex);
            callbackState_->acceptingEdits = false;
        }
        editDrainGate.unlock();
        waitForCallbackQuiescence(callbackState_, CallbackKind::Edit);
        flushFinalBatch(true);
        // Close dirty admission atomically with markDirty's final insertion.
        // Any call already inside the gate completes before it closes; save
        // that captured generation after closure without invoking barriers
        // that could create new edits whose callbacks are now rejected.
        setDirtyAdmission(false);
        flushFinalBatch(false);
        {
            std::lock_guard callbackLock(callbackState_->mutex);
            callbackState_->acceptingCallbacks = false;
        }
        // A decoded chunk is published under World's mutation gate after its
        // loader callback returns. Serialize teardown with that publication
        // window so stop cannot invalidate the persistence generation after
        // extras were committed but before the matching chunk becomes visible.
        auto publishGate = world_.lockMutationGate();
        world_.clearLoader();
        waitForCallbackQuiescence(callbackState_,
                                  CallbackKind::LoaderOrPublication);
        // Do not leave callbacks containing this object's address installed
        // on a World that outlives Persistence (or is restarted with another
        // persistence owner).
        {
            std::lock_guard callbackLock(callbackState_->mutex);
            if (++callbackState_->generation == 0)
                ++callbackState_->generation;
            callbackState_->owner = nullptr;
        }
        bool savedEveryDirtyChunk = false;
        {
            std::lock_guard lk(dirtyMtx_);
            savedEveryDirtyChunk = dirty_.empty();
            if (!savedEveryDirtyChunk)
                std::fprintf(stderr,
                             "[Persistence] shutdown left %zu chunk(s) dirty after retry limit\n",
                             dirty_.size());
        }
        // Keep the stopped Persistence instance usable for an explicit
        // synchronous retry (for example after a transient I/O failure or a
        // save-admission deferral). enterFlushCall() also checks
        // stopInProgress_, so reopening here cannot admit work until this
        // lifecycle transition has fully returned.
        reopenFlushCallAdmission();
        return savedEveryDirtyChunk ? StopStatus::StoppedCleanly
                                    : StopStatus::UnsavedData;
    }
    bool stop() noexcept {
        return stopWithStatus() == StopStatus::StoppedCleanly;
    }
    // stop() is idempotent and flushes the final dirty batch.
    ~Persistence() { stop(); }

    // Backward-compatible boolean view for callers that only need to know
    // whether a chunk decoded. World generation uses loadChunkStatus so a
    // corrupt persisted chunk is never treated as absent.
    bool loadChunk(std::int32_t cx, std::int32_t cz, ChunkBuildWriter& out) {
        return loadChunkStatus(cx, cz, out) == ChunkLoadStatus::Loaded;
    }

    // Raw-byte callers retain the same region transaction boundary as
    // synchronous decoding. The shared helper below assumes this lock is held.
    ChunkBytesRead loadChunkBytesRecovering(std::int32_t cx,
                                            std::int32_t cz) {
        const std::string primary = regionPath(cx, cz);
        auto regionLock = lockRegionFileTransaction(primary);
        return loadChunkBytesRecoveringLocked(cx, cz, primary);
    }

    ChunkLoadStatus loadChunkStatus(
        std::int32_t cx, std::int32_t cz, ChunkBuildWriter& out,
        std::optional<std::uint64_t> sourceGeneration = std::nullopt) {
        // Capture the callback epoch before region recovery begins. The final
        // selected snapshot must publish through this same epoch even if
        // recovery itself takes long enough for Persistence to be restarted.
        std::uint64_t callbackGeneration = 0;
        if (sourceGeneration) {
            callbackGeneration = *sourceGeneration;
        } else {
            std::lock_guard callbackLock(callbackState_->mutex);
            callbackGeneration = callbackState_->generation;
        }

        const std::string primary = regionPath(cx, cz);
        auto regionLock = lockRegionFileTransaction(primary);
        const auto selected =
            loadChunkBytesRecoveringLocked(cx, cz, primary);
        if (selected.status == ChunkBytesRead::Status::Missing)
            return ChunkLoadStatus::Missing;
        if (selected.status == ChunkBytesRead::Status::Failed) {
            if (!selected.error.empty())
                std::fprintf(stderr,
                             "[Persistence] chunk %d,%d recovery failed: %s\n",
                             cx, cz, selected.error.c_str());
            return ChunkLoadStatus::Failed;
        }

        // Keep caller-visible output untouched until the selected byte
        // snapshot has decoded completely. Chunk extras are committed only by
        // adopt(), after the temporary chunk itself is known to be valid.
        Chunk decoded;
        ChunkBuildWriter decodedWriter(decoded);
        bool unsupportedContent = false;
        try {
            if (!decodeChunkBytes(selected.bytes, decodedWriter, cx, cz,
                                  nullptr, &unsupportedContent,
                                  callbackGeneration)) {
                if (unsupportedContent) {
                    std::fprintf(stderr,
                                 "[Persistence] chunk %d,%d contains block states this build cannot represent; preserving source files\n",
                                 cx, cz);
                } else {
                    std::fprintf(stderr,
                                 "[Persistence] selected chunk %d,%d failed final NBT decode\n",
                                 cx, cz);
                }
                return ChunkLoadStatus::Failed;
            }
        } catch (const std::exception& error) {
            std::fprintf(stderr,
                         "[Persistence] final decode of chunk %d,%d failed: %s\n",
                         cx, cz, error.what());
            return ChunkLoadStatus::Failed;
        } catch (...) {
            std::fprintf(stderr,
                         "[Persistence] final decode of chunk %d,%d failed\n",
                         cx, cz);
            return ChunkLoadStatus::Failed;
        }

        auto publishCallback = decodedWriter.takePublishCallback();
        out.adopt(std::move(decoded), std::move(publishCallback));
        return ChunkLoadStatus::Loaded;
    }

    // Save a prepared chunk snapshot through the same staged region transaction
    // used by the background writer. Callers must still retain their dirty
    // generation until this returns successfully.
#ifdef CPPFM_WORLD_MUTATION_TEST
    bool storeChunkBytes(std::int32_t cx, std::int32_t cz,
                         const std::vector<std::uint8_t>& bytes) {
        FlushCallLease flushCall(*this);
        if (!flushCall.admitted()) {
            std::fprintf(stderr,
                         "[Persistence] raw chunk store rejected during shutdown\n");
            return false;
        }
        storeChunkAtomically(cx, cz, bytes);
        return true;
    }
#endif

    // Persist this snapshot only if its dirty generation is still current at
    // the point where the region-file transaction lock is acquired. Checking
    // before taking that lock allows an older background snapshot to overwrite
    // a newer async save that completed while it waited for region I/O.
    bool storeChunkBytesIfDirtyVersion(
        std::int32_t cx, std::int32_t cz, std::uint64_t expectedVersion,
        const std::vector<std::uint8_t>& bytes) {
        FlushCallLease flushCall(*this);
        if (!flushCall.admitted()) return false;
        return storeChunkBytesIfDirtyVersionInternal(
            cx, cz, expectedVersion, bytes);
    }

private:
    // Internal save paths are already admitted by either the worker lifecycle
    // or an outer FlushCallLease. Keeping the admission gate at public write
    // boundaries prevents a late raw writer from overtaking shutdown's final
    // transaction without blocking stop's own final flush.
    bool storeChunkBytesIfDirtyVersionInternal(
        std::int32_t cx, std::int32_t cz, std::uint64_t expectedVersion,
        const std::vector<std::uint8_t>& bytes) {
        if (expectedVersion == 0) return false;
        const std::string path = regionPath(cx, cz);
        auto regionLock = lockRegionFileTransaction(path);
        if (dirtyVersion(cx, cz) != expectedVersion) return false;
        storeChunkAtomicallyLocked(path, cx, cz, bytes);
        return true;
    }

public:
    // Decode bytes obtained by an asynchronous RegionFile read through the
    // same biome resolver and chunk-extras callbacks as the synchronous
    // loader.  Keeping one decode path avoids async loads silently dropping
    // block entities or turning unknown biome names into uint16(-1).
    bool decodeChunkBytes(const std::vector<std::uint8_t>& bytes,
                          ChunkBuildWriter& out,
                          std::int32_t cx, std::int32_t cz,
                          bool* payloadInvalid = nullptr,
                          bool* unsupportedContent = nullptr,
                          std::optional<std::uint64_t>
                              sourceGeneration = std::nullopt) {
        if (payloadInvalid) *payloadInvalid = false;
        if (unsupportedContent) *unsupportedContent = false;
        if (bytes.empty()) return false;
        std::uint64_t publicationGeneration = 0;
        if (sourceGeneration) {
            publicationGeneration = *sourceGeneration;
        } else {
            std::lock_guard callbackLock(callbackState_->mutex);
            publicationGeneration = callbackState_->generation;
        }
        ReadBuffer in(bytes);
        nbt::Parser parser(in);
        nbt::Value root;
        try {
            root = parser.readFileRoot();
        } catch (const std::bad_alloc&) {
            throw;
        } catch (...) {
            if (payloadInvalid) *payloadInvalid = true;
            return false;
        }
        std::string bio;
        std::unordered_map<std::string, std::uint16_t> biomeKeyToIdx;
        std::int32_t defaultBiomeIndex = 0;
        std::function<void(const nbt::Value&)> readExtras;
        std::function<void(const nbt::Value&)> validateExtras;
        std::function<std::function<void()>(const nbt::Value&)>
            prepareReadExtras;
        {
            std::lock_guard lock(configMtx_);
            biomeKeyToIdx = biomeKeyToIdx_;
            defaultBiomeIndex = defaultBiomeIndex_;
            readExtras = readExtras_;
            validateExtras = validateExtras_;
            prepareReadExtras = prepareReadExtras_;
        }
        bool decoded = false;
        bool unsupportedBlockState = false;
        try {
            decoded = chunkFromNBT(
                root, out, {}, bio,
                [biomeKeyToIdx = std::move(biomeKeyToIdx),
                 defaultBiomeIndex](const std::string& key) -> std::int32_t {
                    auto it = biomeKeyToIdx.find(key);
                    return it != biomeKeyToIdx.end()
                               ? static_cast<std::int32_t>(it->second)
                               : defaultBiomeIndex;
                },
                world_.verticalGeometry(), std::pair{cx, cz},
                &unsupportedBlockState);
        } catch (const std::bad_alloc&) {
            throw;
        } catch (...) {
            if (payloadInvalid) *payloadInvalid = true;
            return false;
        }
        if (!decoded) {
            if (unsupportedContent)
                *unsupportedContent = unsupportedBlockState;
            if (payloadInvalid)
                *payloadInvalid = !unsupportedBlockState;
            return false;
        }
        if (validateExtras) {
            try {
                validateExtras(root);
            } catch (const std::bad_alloc&) {
                throw;
            } catch (...) {
                if (payloadInvalid) *payloadInvalid = true;
                return false;
            }
        }
        const auto callbackState = callbackState_;
        auto publishedRoot = std::make_shared<const nbt::Value>(std::move(root));
        out.setPublishCallback(
            [callbackState, publicationGeneration,
             readExtras = std::move(readExtras),
             prepareReadExtras = std::move(prepareReadExtras),
             publishedRoot, cx, cz,
             bio = std::move(bio)] {
                CallbackLease callback(callbackState,
                                       CallbackKind::LoaderOrPublication,
                                       publicationGeneration);
                Persistence* owner = callback.owner();
                if (!owner)
                    throw StaleChunkPublishError(
                        "persistence session ended before chunk publication");
                std::optional<std::string> preparedBiome;
                // Finish all parsing and allocation before publishing any
                // side state. In particular, opaque NBT projection can copy
                // large extension trees and must not run after block entities
                // have been committed.
                if (!bio.empty()) preparedBiome.emplace(bio);
                std::function<void()> commitExtras;
                if (prepareReadExtras)
                    commitExtras = prepareReadExtras(*publishedRoot);
                auto opaque = owner->prepareOpaqueChunkRootNbt(*publishedRoot);
                ChunkNbtTemplateMap stagedTemplates;
                if (!opaque.comp.empty())
                    stagedTemplates.emplace(chunkKey(cx, cz),
                                            std::move(opaque));
                auto stagedTemplate = stagedTemplates.extract(chunkKey(cx, cz));
                if (!callback.ownerIsCurrent(publicationGeneration))
                    throw StaleChunkPublishError(
                        "persistence owner changed during chunk publication");
                const auto key = chunkKey(cx, cz);
                if (!commitExtras && readExtras) {
                    // Legacy readers may synchronously call stop(), which can
                    // flush chunks and re-enter opaque-NBT projection. Reserve
                    // the only map capacity they could otherwise force us to
                    // allocate later, then invoke them without Persistence's
                    // metadata locks held. World mutation order serializes
                    // competing chunk publications across this gap.
                    if (!stagedTemplate.empty()) {
                        std::unique_lock reserveLock(
                            owner->chunkNbtTemplatesMtx_);
                        if (!owner->chunkNbtTemplates_.contains(key))
                            owner->chunkNbtTemplates_.reserve(
                                owner->chunkNbtTemplates_.size() + 1);
                    }
                    readExtras(*publishedRoot);
                    if (!callback.ownerIsCurrent(publicationGeneration))
                        throw StaleChunkPublishError(
                            "persistence owner changed during legacy extras publication");
                }
                std::unique_lock opaqueLock(owner->chunkNbtTemplatesMtx_);
                if (!stagedTemplate.empty() &&
                    !owner->chunkNbtTemplates_.contains(key))
                    owner->chunkNbtTemplates_.reserve(
                        owner->chunkNbtTemplates_.size() + 1);
                std::unique_lock biomeLock(owner->bioMtx_);

                // Publish staged persistence metadata while retaining its old
                // value in the detached node/optional. The only remaining
                // potentially throwing operation is the prepared extras
                // commit; if it fails, these swaps/node changes are undone
                // before World discards the chunk candidate.
                bool opaqueInserted = false;
                bool opaqueReplaced = false;
                std::optional<std::string> displacedBiome;
                const bool publishBiome = preparedBiome.has_value();
                if (!stagedTemplate.empty()) {
                    const auto existing = owner->chunkNbtTemplates_.find(key);
                    if (existing != owner->chunkNbtTemplates_.end()) {
                        using std::swap;
                        static_assert(std::is_nothrow_swappable_v<nbt::Value>);
                        swap(existing->second, stagedTemplate.mapped());
                        opaqueReplaced = true;
                    } else {
                        const auto insertion = owner->chunkNbtTemplates_.insert(
                            std::move(stagedTemplate));
                        if (!insertion.inserted)
                            throw std::logic_error(
                                "opaque chunk template appeared during serialized publication");
                        opaqueInserted = true;
                    }
                }
                if (publishBiome) {
                    static_assert(
                        std::is_nothrow_swappable_v<
                            std::optional<std::string>>);
                    owner->biomeOverride_.swap(preparedBiome);
                    displacedBiome.swap(preparedBiome);
                }

                const auto rollback = [&]() noexcept {
                    if (publishBiome)
                        owner->biomeOverride_.swap(displacedBiome);
                    if (opaqueInserted)
                        owner->chunkNbtTemplates_.erase(key);
                    else if (opaqueReplaced) {
                        const auto current =
                            owner->chunkNbtTemplates_.find(key);
                        if (current != owner->chunkNbtTemplates_.end()) {
                            using std::swap;
                            swap(current->second, stagedTemplate.mapped());
                        }
                    }
                };
                try {
#ifdef CPPFM_PERSISTENCE_PUBLICATION_TEST_HOOKS
                    runPublicationPreparedHookForTesting();
#endif
                    if (commitExtras) commitExtras();
                } catch (...) {
                    rollback();
                    throw;
                }

                // A successful chunk with no retained opaque fields replaces
                // any older template for this key. Erasure is non-allocating
                // and happens only after every fallible publication step.
                if (stagedTemplate.empty() && !opaqueInserted &&
                    !opaqueReplaced)
                    owner->chunkNbtTemplates_.erase(key);
            });
        return true;
    }

    // A staged region replaces the whole `.mca`, not just the requested
    // slot. Before promoting it, validate every occupied slot through the
    // normal chunk decoder so a compressed but malformed sibling cannot
    // replace a usable primary chunk.
    bool validateRegionChunkNbt(const std::string& path,
                                std::int32_t requestedCx,
                                std::int32_t requestedCz,
                                std::string* error = nullptr,
                                bool* unsupportedContent = nullptr) {
        if (unsupportedContent) *unsupportedContent = false;
        const auto originX =
            static_cast<std::int64_t>(requestedCx >> 5) * 32;
        const auto originZ =
            static_cast<std::int64_t>(requestedCz >> 5) * 32;
        try {
            RegionFile region(path);
            for (std::int32_t lz = 0; lz < 32; ++lz) {
                for (std::int32_t lx = 0; lx < 32; ++lx) {
                    const auto bytes = region.load(lx, lz);
                    if (bytes.empty()) continue;
                    const auto cx = static_cast<std::int32_t>(originX + lx);
                    const auto cz = static_cast<std::int32_t>(originZ + lz);
                    Chunk decoded;
                    ChunkBuildWriter writer(decoded);
                    bool unsupportedChunk = false;
                    if (!decodeChunkBytes(bytes, writer, cx, cz, nullptr,
                                          &unsupportedChunk)) {
                        if (unsupportedChunk && unsupportedContent)
                            *unsupportedContent = true;
                        if (error)
                            *error = (unsupportedChunk
                                          ? "unsupported block state at "
                                          : "invalid chunk NBT at ") +
                                     std::to_string(cx) + "," +
                                     std::to_string(cz);
                        return false;
                    }
                }
            }
        } catch (const std::exception& exception) {
            if (error) *error = exception.what();
            return false;
        } catch (...) {
            if (error) *error = "unknown region chunk validation error";
            return false;
        }
        if (error) error->clear();
        return true;
    }

private:
    // Caller must hold the transaction lock for this region through the
    // complete recovery decision and, for sync loads, final selected decode.
    ChunkBytesRead loadChunkBytesRecoveringLocked(
        std::int32_t cx, std::int32_t cz, const std::string& primary) {
        std::string failure;

        const auto tryRead = [&](const std::string& path,
                                 std::vector<std::uint8_t>& bytes,
                                 bool* payloadInvalid,
                                 bool* unsupportedContent,
                                 std::optional<RegionLoadFailureKind>*
                                     regionFailure) {
            if (regionFailure) regionFailure->reset();
            try {
                RegionFile region(path);
                bytes = region.load(cx & 31, cz & 31);
                if (bytes.empty()) return ChunkLoadStatus::Missing;
                Chunk validation;
                ChunkBuildWriter writer(validation);
                if (!decodeChunkBytes(bytes, writer, cx, cz,
                                      payloadInvalid, unsupportedContent)) {
                    throw std::runtime_error(
                        "chunk NBT failed validation or coordinate check");
                }
                return ChunkLoadStatus::Loaded;
            } catch (const RegionLoadError& e) {
                if (regionFailure) *regionFailure = e.kind();
                if (!failure.empty()) failure += "; ";
                failure += path + ": " + e.what();
                bytes.clear();
                return ChunkLoadStatus::Failed;
            } catch (const std::exception& e) {
                if (!failure.empty()) failure += "; ";
                failure += path + ": " + e.what();
                bytes.clear();
                return ChunkLoadStatus::Failed;
            } catch (...) {
                if (!failure.empty()) failure += "; ";
                failure += path + ": unknown read/decode failure";
                bytes.clear();
                return ChunkLoadStatus::Failed;
            }
        };

        ChunkBytesRead result;
        bool primaryPayloadInvalid = false;
        bool primaryUnsupportedContent = false;
        std::optional<RegionLoadFailureKind> primaryRegionFailure;
        auto primaryStatus =
            tryRead(primary, result.bytes, &primaryPayloadInvalid,
                    &primaryUnsupportedContent, &primaryRegionFailure);
        if (primaryUnsupportedContent) {
            result.status = ChunkBytesRead::Status::Failed;
            result.error = primary +
                ": chunk contains block states this build cannot represent; source preserved";
            result.bytes.clear();
            return result;
        }
        const std::string staged = primary + ".new";
        std::error_code existsError;
        const bool stagedExists = std::filesystem::exists(staged, existsError);
        if (existsError) {
            if (primaryStatus == ChunkLoadStatus::Loaded) {
                result.status = ChunkBytesRead::Status::Loaded;
                return result;
            }
            result.status = ChunkBytesRead::Status::Failed;
            result.error = staged + ": cannot inspect staged region: " +
                           existsError.message();
            return result;
        }
        if (stagedExists) {
            std::string validationError;
            bool unsupportedStagedContent = false;
            bool stagedValid = validateRegionChunkNbt(
                staged, cx, cz, &validationError,
                &unsupportedStagedContent);
            if (!stagedValid && !unsupportedStagedContent) {
                const auto recovery = recoverInvalidStagedRegion(
                    primary, staged, cx, cz);
                if (recovery.status == StagedRegionRecovery::Status::Recovered ||
                    recovery.primaryRepaired) {
                    result.bytes.clear();
                    primaryPayloadInvalid = false;
                    primaryUnsupportedContent = false;
                    primaryStatus = tryRead(
                        primary, result.bytes, &primaryPayloadInvalid,
                        &primaryUnsupportedContent, &primaryRegionFailure);
                }
                if (recovery.status == StagedRegionRecovery::Status::Recovered) {
                    stagedValid = validateRegionChunkNbt(
                        staged, cx, cz, &validationError,
                        &unsupportedStagedContent);
                } else if (validationError.empty()) {
                    validationError = recovery.error;
                }
            }
            if (!stagedValid) {
                if (primaryStatus == ChunkLoadStatus::Loaded) {
                    result.status = ChunkBytesRead::Status::Loaded;
                    return result;
                }
                result.status = ChunkBytesRead::Status::Failed;
                result.error = staged + ": staged region failed validation: " +
                               validationError;
                return result;
            }
        }

        // RegionFile can distinguish an on-disk structural/codec corruption
        // from an I/O or unsupported-format failure. When the primary is
        // confirmed corrupt and the staged candidate passed validation,
        // rebuild both snapshots from their independently readable chunks.
        // Do not let a generic read failure authorize replacing the primary.
        if (stagedExists && primaryStatus == ChunkLoadStatus::Failed &&
            primaryRegionFailure == RegionLoadFailureKind::Corruption) {
            const auto recovery = recoverInvalidStagedRegion(
                primary, staged, cx, cz);
            if (recovery.status != StagedRegionRecovery::Status::Recovered) {
                result.status = ChunkBytesRead::Status::Failed;
                result.error = primary +
                    ": confirmed region corruption could not be recovered: " +
                    recovery.error;
                return result;
            }
            result.bytes.clear();
            primaryPayloadInvalid = false;
            primaryUnsupportedContent = false;
            primaryRegionFailure.reset();
            primaryStatus = tryRead(
                primary, result.bytes, &primaryPayloadInvalid,
                &primaryUnsupportedContent, &primaryRegionFailure);
            if (primaryStatus == ChunkLoadStatus::Failed ||
                primaryUnsupportedContent) {
                result.status = ChunkBytesRead::Status::Failed;
                result.error = primary +
                    ": repaired region could not be read safely";
                return result;
            }
        }

        std::vector<std::uint8_t> stagedBytes;
        const auto stagedStatus = stagedExists
            ? tryRead(staged, stagedBytes, nullptr, nullptr, nullptr)
            : ChunkLoadStatus::Missing;
        if (stagedStatus == ChunkLoadStatus::Loaded) {
            bool mayPromote = true;
            if (primaryStatus == ChunkLoadStatus::Failed) {
                existsError.clear();
                const bool primaryExists =
                    std::filesystem::exists(primary, existsError);
                if (existsError) {
                    mayPromote = false;
                } else if (primaryExists && !primaryPayloadInvalid &&
                           primaryRegionFailure !=
                               RegionLoadFailureKind::Corruption) {
                    // Do not replace a primary region after an I/O failure:
                    // it may still contain the only readable copy.
                    mayPromote = false;
                } else if (primaryExists) {
                    try {
                        RegionFile stagedRegion(staged);
                        RegionFile primaryRegion(primary);
                        std::string mergeError;
                        if (!mergeValidatedMissingChunks(
                                stagedRegion, primaryRegion, cx, cz,
                                &mergeError) ||
                            !persistence_detail::syncFile(staged)) {
                            std::fprintf(stderr,
                                         "[Persistence] could not preserve valid primary siblings from %s: %s\n",
                                         primary.c_str(), mergeError.c_str());
                            mayPromote = false;
                        }
                    } catch (const std::exception& error) {
                        std::fprintf(stderr,
                                     "[Persistence] could not preserve primary siblings from %s: %s\n",
                                     primary.c_str(), error.what());
                        mayPromote = false;
                    }
                }
                if (mayPromote && primaryExists && primaryPayloadInvalid) {
                    const auto quarantined = quarantineRegion(primary);
                    if (quarantined.empty()) {
                        std::fprintf(stderr,
                                     "[Persistence] could not quarantine invalid region %s\n",
                                     primary.c_str());
                        existsError.clear();
                        if (std::filesystem::exists(primary, existsError) ||
                            existsError)
                            mayPromote = false;
                    }
                }
            } else {
                existsError.clear();
                const bool primaryExists =
                    std::filesystem::exists(primary, existsError);
                if (existsError) {
                    mayPromote = false;
                } else if (primaryExists) {
                    try {
                        RegionFile stagedRegion(staged);
                        RegionFile primaryRegion(primary);
                        std::string mergeError;
                        if (!mergeValidatedMissingChunks(
                                stagedRegion, primaryRegion, cx, cz,
                                &mergeError) ||
                            !persistence_detail::syncFile(staged)) {
                            std::fprintf(stderr,
                                         "[Persistence] could not merge primary-only chunks into staged region %s: %s\n",
                                         staged.c_str(), mergeError.c_str());
                            mayPromote = false;
                        }
                    } catch (const std::exception& error) {
                        std::fprintf(stderr,
                                     "[Persistence] could not merge primary-only chunks into staged region %s: %s\n",
                                     staged.c_str(), error.what());
                        mayPromote = false;
                    }
                }
            }
            std::error_code promoteError;
            if (mayPromote) {
                if (persistence_detail::replaceFile(staged, primary,
                                                    promoteError)) {
                    if (!persistence_detail::syncDirectory(
                            std::filesystem::path(primary).parent_path()))
                        std::fprintf(stderr,
                                     "[Persistence] recovered chunk %d,%d but could not sync region directory\n",
                                     cx, cz);
                } else {
                    std::fprintf(stderr,
                                 "[Persistence] recovered chunk %d,%d from staged region; promotion failed: %s\n",
                                 cx, cz, promoteError.message().c_str());
                }
            }
            result.status = ChunkBytesRead::Status::Loaded;
            result.bytes = std::move(stagedBytes);
            return result;
        }

        if (primaryStatus == ChunkLoadStatus::Loaded) {
            if (stagedExists &&
                (stagedStatus == ChunkLoadStatus::Missing ||
                 stagedStatus == ChunkLoadStatus::Failed)) {
                // Preserve staged-only siblings, merge every missing primary
                // slot, and repair an invalid staged target from the
                // independently decoded primary copy before promotion.
                try {
                    RegionFile stagedRegion(staged);
                    if (stagedStatus == ChunkLoadStatus::Missing) {
                        RegionFile primaryRegion(primary);
                        std::string mergeError;
                        if (!mergeValidatedMissingChunks(
                                stagedRegion, primaryRegion, cx, cz,
                                &mergeError))
                            throw std::runtime_error(mergeError);
                    } else {
                        stagedRegion.store(cx & 31, cz & 31, result.bytes);
                    }
                    std::error_code promoteError;
                    if (!persistence_detail::syncFile(staged) ||
                        !persistence_detail::replaceFile(staged, primary,
                                                         promoteError) ||
                        !persistence_detail::syncDirectory(
                            std::filesystem::path(primary).parent_path()))
                        std::fprintf(stderr,
                                     "[Persistence] could not merge staged sibling region %s: %s\n",
                                     staged.c_str(), promoteError.message().c_str());
                } catch (const std::exception& error) {
                    std::fprintf(stderr,
                                 "[Persistence] could not merge staged sibling region %s: %s\n",
                                 staged.c_str(), error.what());
                }
            }
            result.status = ChunkBytesRead::Status::Loaded;
            return result;
        }

        // Even when the requested slot is absent, a staged region can be the
        // only surviving copy of its sibling chunks. Preserve it before a
        // later generated-chunk save creates a new primary file.
        if (stagedStatus == ChunkLoadStatus::Missing && stagedExists) {
            existsError.clear();
            const bool primaryExists = std::filesystem::exists(primary, existsError);
            if (existsError) {
                result.status = ChunkBytesRead::Status::Failed;
                result.error = primary + ": cannot inspect primary region: " +
                               existsError.message();
                return result;
            }

            if (primaryExists) {
                if (primaryStatus == ChunkLoadStatus::Failed &&
                    !primaryPayloadInvalid) {
                    result.status = ChunkBytesRead::Status::Failed;
                    result.error = primary +
                                   ": refusing staged promotion after primary I/O failure";
                    return result;
                }
                try {
                    RegionFile stagedRegion(staged);
                    RegionFile primaryRegion(primary);
                    std::string mergeError;
                    if (!mergeValidatedMissingChunks(
                            stagedRegion, primaryRegion, cx, cz,
                            &mergeError,
                            primaryPayloadInvalid) ||
                        !persistence_detail::syncFile(staged)) {
                        result.status = ChunkBytesRead::Status::Failed;
                        result.error =
                            "could not merge primary sibling chunks into staged region " +
                            staged + ": " + mergeError;
                        return result;
                    }
                } catch (const std::exception& error) {
                    result.status = ChunkBytesRead::Status::Failed;
                    result.error =
                        "could not merge primary sibling chunks into staged region " +
                        staged + ": " + error.what();
                    return result;
                }
            }

            if (primaryStatus == ChunkLoadStatus::Failed &&
                primaryPayloadInvalid) {
                const auto quarantined = quarantineRegion(primary);
                if (quarantined.empty()) {
                    result.status = ChunkBytesRead::Status::Failed;
                    result.error = "could not quarantine invalid primary region " +
                                   primary;
                    return result;
                }
            }
            std::error_code promoteError;
            if (!persistence_detail::replaceFile(staged, primary,
                                                 promoteError)) {
                result.status = ChunkBytesRead::Status::Failed;
                result.error = "could not promote sibling-only staged region " +
                               staged + ": " + promoteError.message();
                return result;
            }
            if (!persistence_detail::syncDirectory(
                    std::filesystem::path(primary).parent_path()))
                std::fprintf(stderr,
                             "[Persistence] promoted staged region %s but could not sync its directory\n",
                             primary.c_str());
            result.status = ChunkBytesRead::Status::Missing;
            return result;
        }

        result.status = failure.empty() ? ChunkBytesRead::Status::Missing
                                        : ChunkBytesRead::Status::Failed;
        result.error = std::move(failure);
        return result;
    }

public:

    void mergeOpaqueChunkRootNbt(std::int32_t cx, std::int32_t cz,
                                 nbt::Value& destination) const {
        nbt::Value saved = nbt::Value::makeCompound();
        {
            std::lock_guard lock(chunkNbtTemplatesMtx_);
            const auto it = chunkNbtTemplates_.find(chunkKey(cx, cz));
            if (it == chunkNbtTemplates_.end()) return;
            saved = it->second;
        }
        for (const auto& [name, value] : saved.comp) {
            if (name == "sections") {
                mergeOpaqueSections(value, destination,
                                     world_.verticalGeometry());
                continue;
            }
            auto* current = destination.get(name);
            if (!current) {
                destination.set(name, value);
            } else if (name == "Heightmaps") {
                mergeMissingCompoundFields(*current, value);
                if (current->tag == nbt::Compound &&
                    value.tag == nbt::Compound) {
                    // *_WG maps describe the pre-feature generation surface,
                    // so recomputing them from final block states is lossy.
                    // Use the validated persisted originals when available.
                    for (const char* worldgenMap : {
                             "WORLD_SURFACE_WG", "OCEAN_FLOOR_WG"}) {
                        const auto* savedMap = value.get(worldgenMap);
                        if (savedMap && savedMap->tag == nbt::LongArray &&
                            savedMap->longArray.size() ==
                                kAnvilHeightmapLongCount)
                            current->set(worldgenMap, *savedMap);
                    }
                }
            }
        }
    }

    void forgetOpaqueChunkRootNbt(std::int32_t cx, std::int32_t cz) {
        std::lock_guard lock(chunkNbtTemplatesMtx_);
        chunkNbtTemplates_.erase(chunkKey(cx, cz));
    }

    void markDirty(std::int32_t cx, std::int32_t cz) {
        std::unique_lock admissionLock(dirtyAdmissionMtx_);
        if (!acceptingDirty_.load(std::memory_order_acquire)) return;
#ifdef CPPFM_WORLD_MUTATION_TEST
        if (beforeDirtyCommitForTest_) beforeDirtyCommitForTest_();
#endif
        {
            // Serialize the state change with the wait predicate under the
            // same mutex; otherwise notify can race between predicate-check
            // and sleeping and leave dirty data waiting for the periodic poll.
            std::lock_guard waitLock(cvMtx_);
            std::lock_guard dirtyLock(dirtyMtx_);
            const auto key = chunkKey(cx, cz);
            // Store dirtiness and its generation in one node. If allocating a
            // new map entry fails, no dirty state is published without a
            // generation; replacing an existing generation is non-allocating.
            dirty_.insert_or_assign(key, nextDirtyVersion_);
            ++nextDirtyVersion_;
            if (nextDirtyVersion_ == 0) ++nextDirtyVersion_;
            ++dirtyWakeGeneration_;
            if (dirtyWakeGeneration_ == 0) ++dirtyWakeGeneration_;
        }
        admissionLock.unlock();
        cv_.notify_all();
    }

private:
    void flushOnce(bool runSaveBarrier = true) {
        std::vector<std::pair<std::int64_t, std::uint64_t>> batch;
        {
            std::lock_guard lk(dirtyMtx_);
            batch.reserve(dirty_.size());
            for (const auto& [key, version] : dirty_)
                batch.emplace_back(key, version);
        }
        std::unordered_map<std::uint16_t, std::string> biomeIdxToKey;
        std::function<void(std::int32_t, std::int32_t, nbt::Value&)> writeExtras;
        std::function<bool(std::int32_t, std::int32_t)> chunkSaveAdmission;
        std::function<void(std::int32_t, std::int32_t)> chunkSaveBarrier;
        std::function<void(std::int32_t, std::int32_t)> chunkSaveComplete;
        {
            std::lock_guard lock(configMtx_);
            biomeIdxToKey = biomeIdxToKey_;
            writeExtras = writeExtras_;
            chunkSaveAdmission = chunkSaveAdmission_;
            chunkSaveBarrier = chunkSaveBarrier_;
            chunkSaveComplete = chunkSaveComplete_;
        }
        for (const auto& [k, dirtyVersion] : batch) {
            auto [cx, cz] = chunkKeyDecode(k);
            const std::string bio = [this] {
                std::lock_guard lk(bioMtx_);
                return biomeOverride_.value_or(biome_);
            }();
            bool saveFailed = false;
            bool found = false;
            bool persisted = false;
            try {
                if (chunkSaveAdmission &&
                    !invokeSaveCallback(chunkSaveAdmission, cx, cz))
                    continue;
                if (runSaveBarrier && chunkSaveBarrier)
                    invokeSaveCallback(chunkSaveBarrier, cx, cz);
                nbt::Value root;
                {
                    // A dirty notification can arrive just after the chunk
                    // was evicted (notably from separately owned block
                    // entities). Retry disk/generation recovery here, and
                    // hold the same gate as eviction through the value
                    // snapshot so the save worker never silently drops that
                    // dirty generation for lack of a resident chunk.
                    auto mutationGate = world_.lockMutationGate();
                    if (!world_.hasChunk(cx, cz) &&
                        !world_.generateChunkIfMissing(cx, cz)) {
                        saveFailed = true;
                        std::fprintf(stderr,
                                     "[Persistence] dirty chunk %d,%d is unavailable; retaining it for retry\n",
                                     cx, cz);
                        continue;
                    }
                    // Generating an evicted/missing dirty chunk can publish
                    // deferred side effects (for example structure loot)
                    // after the initial admission check above. Recheck while
                    // the World mutation gate is held so the new side effect
                    // cannot be omitted from this snapshot and lost at stop.
                    if (chunkSaveAdmission &&
                        !invokeSaveCallback(chunkSaveAdmission, cx, cz))
                        continue;
                    found = snapshotChunkNbt(cx, cz, bio, biomeIdxToKey, root);
                }
                if (found) {
                    // Do not hold the World read lock while extra serializers
                    // acquire block-entity locks: gameplay ticks may hold an
                    // entity lock while mutating World, so nesting the two
                    // locks here would invert their order and deadlock.
                    try {
                        std::vector<std::uint8_t> bytes;
                        encodeChunkNbt(cx, cz, root, writeExtras, bytes);
                        persisted = storeChunkBytesIfDirtyVersionInternal(
                            cx, cz, dirtyVersion, bytes);
                        if (persisted)
                            std::fprintf(stderr,
                                         "[cppfm] saved r.%d.%d mca (%zu bytes nbt)\n",
                                         cx >> 5, cz >> 5, bytes.size());
                    } catch (const std::exception& e) {
                        saveFailed = true;
                        std::fprintf(stderr,
                                     "[cppfm] SAVE ERROR chunk %d,%d: %s\n",
                                     cx, cz, e.what());
                    } catch (...) {
                        saveFailed = true;
                        std::fprintf(stderr,
                                     "[cppfm] SAVE ERROR chunk %d,%d: unknown error\n",
                                     cx, cz);
                    }
                }
            } catch (const std::exception& e) {
                saveFailed = true;
                std::fprintf(stderr, "[cppfm] SAVE ERROR chunk %d,%d: %s\n", cx, cz, e.what());
            } catch (...) {
                saveFailed = true;
                std::fprintf(stderr, "[cppfm] SAVE ERROR chunk %d,%d: unknown error\n", cx, cz);
            }
            if (found && !saveFailed && persisted &&
                markCleanIfVersion(cx, cz, dirtyVersion) && chunkSaveComplete) {
                try {
                    invokeSaveCallback(chunkSaveComplete, cx, cz);
                } catch (const std::exception& error) {
                    std::fprintf(stderr,
                                 "[Persistence] post-save callback %d,%d failed: %s\n",
                                 cx, cz, error.what());
                } catch (...) {
                    std::fprintf(stderr,
                                 "[Persistence] post-save callback %d,%d failed\n",
                                 cx, cz);
                }
            }
        }
    }

public:
    bool chunkFitsAnvilEntry(
        std::int32_t cx, std::int32_t cz,
        const std::function<void(std::int32_t, std::int32_t, nbt::Value&)>&
            projectedExtras = {}) {
        FlushCallLease flushCall(*this);
        if (!flushCall.admitted()) return false;
        try {
            const std::string bio = [this] {
                std::lock_guard lock(bioMtx_);
                return biomeOverride_.value_or(biome_);
            }();
            std::unordered_map<std::uint16_t, std::string> biomeIdxToKey;
            std::function<void(std::int32_t, std::int32_t, nbt::Value&)> writeExtras;
            {
                std::lock_guard lock(configMtx_);
                biomeIdxToKey = biomeIdxToKey_;
                writeExtras = writeExtras_;
            }
            if (projectedExtras) writeExtras = projectedExtras;
            nbt::Value root;
            if (!snapshotChunkNbt(cx, cz, bio, biomeIdxToKey, root)) return false;
            std::vector<std::uint8_t> bytes;
            encodeChunkNbt(cx, cz, root, writeExtras, bytes);
            return RegionFile::canStoreBytes(bytes);
        } catch (...) {
            return false;
        }
    }
    bool isDirty(std::int32_t cx, std::int32_t cz) {
        std::lock_guard lk(dirtyMtx_);
        return dirty_.find(chunkKey(cx, cz)) != dirty_.end();
    }
    // The caller's World eviction gate must already be held. Keep this lock
    // alive through removal so a dirty callback racing the final clean check
    // is published only after eviction, where flushOnce can reload the chunk.
    std::unique_lock<std::mutex> lockChunkIfCleanForEviction(
        std::int32_t cx, std::int32_t cz) {
        std::unique_lock lock(dirtyMtx_);
        if (dirty_.find(chunkKey(cx, cz)) != dirty_.end()) return {};
        return lock;
    }
    std::uint64_t dirtyVersion(std::int32_t cx, std::int32_t cz) {
        std::lock_guard lk(dirtyMtx_);
        const auto it = dirty_.find(chunkKey(cx, cz));
        return it == dirty_.end() ? 0 : it->second;
    }
    bool markCleanIfVersion(std::int32_t cx, std::int32_t cz,
                            std::uint64_t expectedVersion) {
        if (expectedVersion == 0) return false;
        std::lock_guard lk(dirtyMtx_);
        const auto key = chunkKey(cx, cz);
        const auto it = dirty_.find(key);
        if (it == dirty_.end() || it->second != expectedVersion)
            return false;
        dirty_.erase(it);
        return true;
    }
    bool flushChunk(std::int32_t cx, std::int32_t cz) {
        FlushCallLease flushCall(*this);
        if (!flushCall.admitted()) return false;
        const auto dirtyVersion = this->dirtyVersion(cx, cz);
        const std::string bio = [this] {
            std::lock_guard lk(bioMtx_);
            return biomeOverride_.value_or(biome_);
        }();
        std::unordered_map<std::uint16_t, std::string> biomeIdxToKey;
        std::function<void(std::int32_t, std::int32_t, nbt::Value&)> writeExtras;
        std::function<bool(std::int32_t, std::int32_t)> chunkSaveAdmission;
        std::function<void(std::int32_t, std::int32_t)> chunkSaveBarrier;
        std::function<void(std::int32_t, std::int32_t)> chunkSaveComplete;
        {
            std::lock_guard lock(configMtx_);
            biomeIdxToKey = biomeIdxToKey_;
            writeExtras = writeExtras_;
            chunkSaveAdmission = chunkSaveAdmission_;
            chunkSaveBarrier = chunkSaveBarrier_;
            chunkSaveComplete = chunkSaveComplete_;
        }
        try {
            if (chunkSaveAdmission &&
                !invokeSaveCallback(chunkSaveAdmission, cx, cz))
                return false;
        } catch (const std::exception& error) {
            std::fprintf(stderr,
                         "[Persistence] save admission %d,%d failed: %s\n",
                         cx, cz, error.what());
            return false;
        } catch (...) {
            std::fprintf(stderr,
                         "[Persistence] save admission %d,%d failed\n",
                         cx, cz);
            return false;
        }
        try {
            if (chunkSaveBarrier)
                invokeSaveCallback(chunkSaveBarrier, cx, cz);
        } catch (const std::exception& error) {
            std::fprintf(stderr,
                         "[Persistence] save barrier %d,%d failed: %s\n",
                         cx, cz, error.what());
            return false;
        } catch (...) {
            std::fprintf(stderr,
                         "[Persistence] save barrier %d,%d failed\n",
                         cx, cz);
            return false;
        }
        bool ok = false;
        bool saveFailed = false;
        bool found = false;
        try {
            nbt::Value root;
            {
                // Barriers can materialize deferred worldgen state just like
                // the asynchronous path. Keep admission, any newly published
                // side effects, and the snapshot in the same mutation epoch.
                auto mutationGate = world_.lockMutationGate();
                try {
                    if (chunkSaveAdmission &&
                        !invokeSaveCallback(chunkSaveAdmission, cx, cz))
                        return false;
                } catch (const std::exception& error) {
                    std::fprintf(stderr,
                                 "[Persistence] save admission %d,%d failed: %s\n",
                                 cx, cz, error.what());
                    return false;
                } catch (...) {
                    std::fprintf(stderr,
                                 "[Persistence] save admission %d,%d failed\n",
                                 cx, cz);
                    return false;
                }
                found = snapshotChunkNbt(cx, cz, bio, biomeIdxToKey, root);
            }
            if (found) {
                // Serialize extras after releasing the World lock. Block
                // entity ticks can hold their state lock while changing the
                // world, so nesting those locks in the opposite order here
                // would allow a save/tick deadlock.
                try {
                    std::vector<std::uint8_t> bytes;
                    encodeChunkNbt(cx, cz, root, writeExtras, bytes);
                    ok = storeChunkBytesIfDirtyVersionInternal(
                        cx, cz, dirtyVersion, bytes);
                    if (ok)
                        std::fprintf(stderr, "[cppfm] flushChunk %d,%d (%zu bytes)\n",
                                     cx, cz, bytes.size());
                } catch (const std::exception& e) {
                    saveFailed = true;
                    std::fprintf(stderr, "[cppfm] FLUSH CHUNK ERROR %d,%d: %s\n", cx, cz, e.what());
                } catch (...) {
                    saveFailed = true;
                    std::fprintf(stderr, "[cppfm] FLUSH CHUNK ERROR %d,%d\n", cx, cz);
                }
            }
        } catch (const std::exception& e) {
            saveFailed = true;
            std::fprintf(stderr, "[cppfm] FLUSH CHUNK ERROR %d,%d: %s\n", cx, cz, e.what());
        } catch (...) {
            saveFailed = true;
            std::fprintf(stderr, "[cppfm] FLUSH CHUNK ERROR %d,%d\n", cx, cz);
        }
        if (found && !saveFailed && ok &&
            markCleanIfVersion(cx, cz, dirtyVersion) && chunkSaveComplete) {
            try {
                invokeSaveCallback(chunkSaveComplete, cx, cz);
            } catch (const std::exception& error) {
                std::fprintf(stderr,
                             "[Persistence] post-save callback %d,%d failed: %s\n",
                             cx, cz, error.what());
            } catch (...) {
                std::fprintf(stderr,
                             "[Persistence] post-save callback %d,%d failed\n",
                             cx, cz);
            }
        }
        return ok;
    }

private:
#ifdef CPPFM_PERSISTENCE_PUBLICATION_TEST_HOOKS
    static std::function<void()>& publicationPreparedHookForTesting() {
        static thread_local std::function<void()> hook;
        return hook;
    }

    static void runPublicationPreparedHookForTesting() {
        auto hook = std::move(publicationPreparedHookForTesting());
        publicationPreparedHookForTesting() = {};
        if (hook) hook();
    }
#endif

    StagedRegionRecovery::Result recoverInvalidStagedRegion(
        const std::string& primary, const std::string& staged,
        std::int32_t cx, std::int32_t cz) {
        const auto validate = [this](const std::vector<std::uint8_t>& bytes,
                                     std::int32_t chunkX,
                                     std::int32_t chunkZ) {
            Chunk candidate;
            ChunkBuildWriter writer(candidate);
            bool unsupported = false;
            if (decodeChunkBytes(bytes, writer, chunkX, chunkZ, nullptr,
                                 &unsupported))
                return StagedRegionRecovery::ChunkState::Valid;
            return unsupported
                ? StagedRegionRecovery::ChunkState::Unsupported
                : StagedRegionRecovery::ChunkState::Invalid;
        };
        return StagedRegionRecovery::recover(
            primary, staged, cx, cz, validate);
    }

    bool snapshotChunkNbt(
        std::int32_t cx, std::int32_t cz, const std::string& biome,
        const std::unordered_map<std::uint16_t, std::string>& biomeIdxToKey,
        nbt::Value& root) {
        return world_.withChunk(cx, cz, [&](const Chunk& chunk) {
            root = chunkToNBT(cx, cz, chunk, biome, &biomeIdxToKey,
                              world_.verticalGeometry());
        });
    }

    void encodeChunkNbt(
        std::int32_t cx, std::int32_t cz, nbt::Value& root,
        const std::function<void(std::int32_t, std::int32_t, nbt::Value&)>&
            writeExtras,
        std::vector<std::uint8_t>& bytes) {
        mergeOpaqueChunkRootNbt(cx, cz, root);
        if (writeExtras) invokeSaveCallback(writeExtras, cx, cz, root);
        WriteBuffer output;
        nbt::writeFileRoot(output, root);
        bytes = std::move(output.data);
    }

    template <typename Callback, typename... Args>
    std::invoke_result_t<Callback&&, Args&&...> invokeSaveCallback(
        Callback&& callback, Args&&... args) {
        persistence_callback_detail::StopDisallowedCallbackScope scope(
            callbackState_.get(), callbackState_->activeSaveCallbacks);
        return std::invoke(std::forward<Callback>(callback),
                           std::forward<Args>(args)...);
    }

    bool enterFlushCall() {
        if (persistence_callback_detail::activeOnCurrentThread(
                callbackState_.get(), false) != 0)
            return false;
        if (stopInProgress_.load(std::memory_order_acquire)) return false;
        std::lock_guard lock(flushCallMtx_);
        if (!acceptingFlushCalls_ ||
            stopInProgress_.load(std::memory_order_acquire))
            return false;
        ++activeFlushCalls_;
        return true;
    }

    void leaveFlushCall() noexcept {
#ifdef CPPFM_WORLD_MUTATION_TEST
        if (beforeFlushCallLeaveForTest_) {
            try {
                beforeFlushCallLeaveForTest_();
            } catch (...) {
                std::fprintf(stderr,
                             "[Persistence] test flush-call release hook threw\n");
            }
        }
#endif
        {
            std::lock_guard lock(flushCallMtx_);
            if (activeFlushCalls_ > 0) --activeFlushCalls_;
        }
        flushCallQuiescent_.notify_all();
    }

    void reopenFlushCallAdmission() {
        std::lock_guard lock(flushCallMtx_);
        acceptingFlushCalls_ = true;
    }

    void closeFlushCallAdmissionAndWait() {
        std::unique_lock lock(flushCallMtx_);
        acceptingFlushCalls_ = false;
        flushCallQuiescent_.wait(lock,
                                 [this] { return activeFlushCalls_ == 0; });
    }

    // A .mca merge is a whole-region replacement. Copying compressed chunk
    // payloads without decoding lets one malformed sibling make recovery
    // destructive later. Validate every primary-only chunk first and mutate
    // the staged snapshot only after the complete merge set is known safe.
    bool mergeValidatedMissingChunks(RegionFile& destination,
                                     RegionFile& source,
                                     std::int32_t requestedCx,
                                     std::int32_t requestedCz,
                                     std::string* error,
                                     bool skipInvalidRequestedChunk = false) {
        if (&destination == &source) return true;
        struct PendingChunk final {
            std::int32_t localX;
            std::int32_t localZ;
            std::vector<std::uint8_t> bytes;
        };
        std::vector<PendingChunk> pending;
        const auto originX = static_cast<std::int64_t>(requestedCx & ~31);
        const auto originZ = static_cast<std::int64_t>(requestedCz & ~31);
        try {
            for (std::int32_t localZ = 0; localZ < 32; ++localZ) {
                for (std::int32_t localX = 0; localX < 32; ++localX) {
                    if (!destination.load(localX, localZ).empty()) continue;
                    if (skipInvalidRequestedChunk &&
                        localX == (requestedCx & 31) &&
                        localZ == (requestedCz & 31))
                        continue;
                    const auto bytes = source.load(localX, localZ);
                    if (bytes.empty()) continue;
                    const auto globalX = originX + localX;
                    const auto globalZ = originZ + localZ;
                    if (globalX < std::numeric_limits<std::int32_t>::min() ||
                        globalX > std::numeric_limits<std::int32_t>::max() ||
                        globalZ < std::numeric_limits<std::int32_t>::min() ||
                        globalZ > std::numeric_limits<std::int32_t>::max())
                        continue;
                    Chunk decoded;
                    ChunkBuildWriter writer(decoded);
                    if (!decodeChunkBytes(
                            bytes, writer, static_cast<std::int32_t>(globalX),
                            static_cast<std::int32_t>(globalZ))) {
                        if (error)
                            *error = "invalid primary-only chunk at " +
                                     std::to_string(globalX) + "," +
                                     std::to_string(globalZ);
                        return false;
                    }
                    pending.push_back(
                        {localX, localZ, std::move(bytes)});
                }
            }
            for (auto& chunk : pending)
                destination.store(chunk.localX, chunk.localZ, chunk.bytes);
        } catch (const std::exception& exception) {
            if (error) *error = exception.what();
            return false;
        } catch (...) {
            if (error) *error = "unknown validated region merge error";
            return false;
        }
        if (error) error->clear();
        return true;
    }

    static bool isRegeneratedChunkRootField(std::string_view name) {
        return name == "DataVersion" || name == "xPos" || name == "zPos" ||
               name == "yPos" || name == "Status" || name == "LastUpdate" ||
               name == "Heightmaps" || name == "sections" ||
               name == "isLightOn" ||
               name == "block_entities" || name == "TileEntities";
    }

    static std::optional<std::int32_t> sectionY(const nbt::Value& section) {
        const auto* y = section.get("Y");
        if (!y) return std::nullopt;
        if (y->tag == nbt::Byte) return y->b;
        if (y->tag == nbt::Short) return y->s;
        if (y->tag == nbt::Int) return y->i;
        return std::nullopt;
    }

    static void mergeMissingCompoundFields(nbt::Value& destination,
                                           const nbt::Value& saved) {
        if (destination.tag != nbt::Compound || saved.tag != nbt::Compound)
            return;
        for (const auto& [name, value] : saved.comp) {
            auto* current = destination.get(name);
            if (!current) {
                destination.set(name, value);
            } else if (current->tag == nbt::Compound &&
                       value.tag == nbt::Compound) {
                mergeMissingCompoundFields(*current, value);
            }
        }
    }

    static bool sectionInGeometry(std::int32_t sectionY,
                                  DimensionGeometry geometry) {
        return sectionY >= geometry.minSection() &&
               sectionY < geometry.minSection() + geometry.sectionCount();
    }

    static void mergeOpaqueSections(const nbt::Value& savedSections,
                                    nbt::Value& destinationRoot,
                                    DimensionGeometry geometry) {
        if (savedSections.tag != nbt::List) return;
        auto* currentSections = destinationRoot.get("sections");
        // The generated section list is authoritative. A malformed or absent
        // destination must never be replaced by the incomplete opaque template.
        if (!currentSections || currentSections->tag != nbt::List) return;

        for (const auto& savedSection : savedSections.list) {
            const auto y = sectionY(savedSection);
            auto current = currentSections->list.end();
            if (y) {
                current = std::find_if(currentSections->list.begin(),
                    currentSections->list.end(), [&](const nbt::Value& section) {
                        return sectionY(section) == y;
                    });
            }
            if (current == currentSections->list.end() &&
                (!y || !sectionInGeometry(*y, geometry)))
                currentSections->list.push_back(savedSection);
            else if (current != currentSections->list.end())
            {
                // Biomes currently have no gameplay mutation API. Preserve
                // the source palette verbatim so an unknown registry key
                // mapped to the runtime fallback is not rewritten as that
                // fallback when unrelated blocks are edited and saved.
                if (const auto* savedBiomes = savedSection.get("biomes"))
                    current->set("biomes", *savedBiomes);
                mergeMissingCompoundFields(*current, savedSection);
            }
        }
    }

    static nbt::Value opaqueSectionTemplate(const nbt::Value& section,
                                            DimensionGeometry geometry) {
        const auto y = sectionY(section);
        if (!y || !sectionInGeometry(*y, geometry)) {
            if (section.tag != nbt::Compound) return section;
            nbt::Value opaque = section;
            opaque.comp.erase(std::remove_if(opaque.comp.begin(),
                opaque.comp.end(), [](const auto& field) {
                    return field.first == "BlockLight" ||
                           field.first == "SkyLight";
                }), opaque.comp.end());
            return opaque;
        }

        nbt::Value opaque = nbt::Value::makeCompound();
        if (const auto* yTag = section.get("Y")) opaque.set("Y", *yTag);
        for (const auto& [name, value] : section.comp) {
            if (name != "Y" && name != "block_states" && name != "biomes" &&
                name != "BlockLight" && name != "SkyLight")
                opaque.set(name, value);
        }
        if (const auto* represented = section.get("block_states");
            represented && represented->tag == nbt::Compound) {
            nbt::Value extras = nbt::Value::makeCompound();
            for (const auto& [name, value] : represented->comp)
                if (name != "palette" && name != "data")
                    extras.set(name, value);
            if (!extras.comp.empty())
                opaque.set("block_states", std::move(extras));
        }
        // Preserve the original data-backed biome palette. Resolution into the
        // fixed runtime registry may use a fallback for an unknown key; that
        // fallback must not replace the persisted source on the next save.
        if (const auto* biomes = section.get("biomes"))
            opaque.set("biomes", *biomes);
        return opaque;
    }

    nbt::Value prepareOpaqueChunkRootNbt(const nbt::Value& root) const {
        nbt::Value opaque = nbt::Value::makeCompound();
        for (const auto& [name, value] : root.comp) {
            if (!isRegeneratedChunkRootField(name)) {
                opaque.set(name, value);
                continue;
            }
            if (name == "Heightmaps" && value.tag == nbt::Compound) {
                nbt::Value heightmapExtras = nbt::Value::makeCompound();
                for (const auto& [heightmap, data] : value.comp) {
                    const bool vanillaDerived =
                        heightmap == "WORLD_SURFACE_WG" ||
                        heightmap == "OCEAN_FLOOR_WG" ||
                        heightmap == "WORLD_SURFACE" ||
                        heightmap == "OCEAN_FLOOR" ||
                        heightmap == "MOTION_BLOCKING" ||
                        heightmap == "MOTION_BLOCKING_NO_LEAVES";
                    const bool worldgenMap =
                        heightmap == "WORLD_SURFACE_WG" ||
                        heightmap == "OCEAN_FLOOR_WG";
                    const bool validWorldgenMap = worldgenMap &&
                        data.tag == nbt::LongArray &&
                        data.longArray.size() == kAnvilHeightmapLongCount;
                    if (!vanillaDerived || validWorldgenMap)
                        heightmapExtras.set(heightmap, data);
                }
                if (!heightmapExtras.comp.empty())
                    opaque.set("Heightmaps", std::move(heightmapExtras));
            } else if (name == "sections" && value.tag == nbt::List) {
                nbt::Value sectionExtras = nbt::Value::makeList(nbt::Compound);
                for (const auto& section : value.list) {
                    nbt::Value savedSection = opaqueSectionTemplate(
                        section, world_.verticalGeometry());
                    if (savedSection.tag == nbt::Compound &&
                        savedSection.comp.size() > 1)
                        sectionExtras.list.push_back(std::move(savedSection));
                }
                if (!sectionExtras.list.empty())
                    opaque.set("sections", std::move(sectionExtras));
            }
        }
        return opaque;
    }

    void loop() {
        std::unique_lock lk(cvMtx_);
        std::uint64_t observedWakeGeneration = 0;
        while (running_.load(std::memory_order_acquire)) {
            cv_.wait_for(lk, std::chrono::seconds(3), [this,
                                                       &observedWakeGeneration] {
                return !running_.load(std::memory_order_acquire) ||
                       dirtyWakeGeneration_ != observedWakeGeneration;
            });
            if (!running_.load(std::memory_order_acquire)) break;
            observedWakeGeneration = dirtyWakeGeneration_;
            lk.unlock();
            try {
                flushOnce();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[Persistence] background flush failed: %s\n", e.what());
            } catch (...) {
                std::fprintf(stderr, "[Persistence] background flush failed\n");
            }
            lk.lock();
        }
    }
    std::string regionPath(std::int32_t cx, std::int32_t cz) const {
        return dir_ + "/region/r." + std::to_string(cx >> 5) + "." +
               std::to_string(cz >> 5) + ".mca";
    }

public:
    class RegionFileTransaction {
    public:
        explicit RegionFileTransaction(
            std::vector<std::shared_ptr<std::mutex>> mutexes)
            : mutexes_(std::move(mutexes)) {
            locks_.reserve(mutexes_.size());
            for (const auto& mutex : mutexes_)
                locks_.emplace_back(*mutex);
        }
        RegionFileTransaction(
            std::vector<std::shared_ptr<std::mutex>> mutexes,
            std::try_to_lock_t)
            : mutexes_(std::move(mutexes)) {
            locks_.reserve(mutexes_.size());
            for (const auto& mutex : mutexes_) {
                locks_.emplace_back(*mutex, std::try_to_lock);
                if (!locks_.back().owns_lock()) {
                    ownsAll_ = false;
                    return;
                }
            }
        }
        RegionFileTransaction(RegionFileTransaction&&) noexcept = default;
        RegionFileTransaction& operator=(RegionFileTransaction&&) noexcept = delete;
        RegionFileTransaction(const RegionFileTransaction&) = delete;
        RegionFileTransaction& operator=(const RegionFileTransaction&) = delete;
        bool ownsAllMutexes() const noexcept { return ownsAll_; }

    private:
        // Keep the mutex objects alive until after every lock is released.
        std::vector<std::shared_ptr<std::mutex>> mutexes_;
        std::vector<std::unique_lock<std::mutex>> locks_;
        bool ownsAll_ = true;
    };

    // Serialize whole-file copy/merge/replace transactions per primary region.
    // The canonical path lock remains stable across atomic replacement; the
    // current file-identity lock also serializes aliases. Both are held for
    // the entire copy/merge/replace transaction.
    static RegionFileTransaction lockRegionFileTransaction(
        const std::string& path) {
        return RegionFileTransaction(regionFileMutexesFor(path));
    }
    static std::optional<RegionFileTransaction>
    tryLockRegionFileTransaction(const std::string& path) {
        RegionFileTransaction transaction(regionFileMutexesFor(path),
                                          std::try_to_lock);
        if (!transaction.ownsAllMutexes()) return std::nullopt;
        return std::optional<RegionFileTransaction>{std::move(transaction)};
    }

private:
    static std::mutex& regionFileRegistryMutex() {
        static std::mutex mutex;
        return mutex;
    }

    static std::unordered_map<std::string, std::weak_ptr<std::mutex>>&
    regionFileMutexes() {
        static std::unordered_map<std::string, std::weak_ptr<std::mutex>> mutexes;
        return mutexes;
    }

    static std::vector<std::shared_ptr<std::mutex>> regionFileMutexesFor(
        const std::string& path) {
        const auto lockKeys = RegionFile::transactionLockKeys(path);
        std::vector<std::string> keys(lockKeys.begin(), lockKeys.end());
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

        std::lock_guard registryLock(regionFileRegistryMutex());
        auto& registry = regionFileMutexes();
        if (registry.size() > 256) {
            for (auto it = registry.begin(); it != registry.end();) {
                if (it->second.expired()) it = registry.erase(it);
                else ++it;
            }
        }
        std::vector<std::shared_ptr<std::mutex>> mutexes;
        mutexes.reserve(keys.size());
        for (const auto& key : keys) {
            auto& weakMutex = registry[key];
            auto mutex = weakMutex.lock();
            if (!mutex) {
                mutex = std::make_shared<std::mutex>();
                weakMutex = mutex;
            }
            mutexes.push_back(std::move(mutex));
        }
        return mutexes;
    }

    static std::string quarantineRegion(const std::string& path) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec) return {};
        for (std::size_t suffix = 0; suffix < 10000; ++suffix) {
            const std::string candidate = path + ".corrupt" +
                (suffix == 0 ? std::string{} : "." + std::to_string(suffix));
            ec.clear();
            if (storage_file_ops::moveFileWithoutReplace(path, candidate, ec))
                return candidate;
            if (ec == std::errc::file_exists) continue;
            return {};
        }
        return {};
    }

    void storeChunkAtomically(std::int32_t cx, std::int32_t cz,
                              const std::vector<std::uint8_t>& bytes) {
        const std::string path = regionPath(cx, cz);
        auto regionLock = lockRegionFileTransaction(path);
        storeChunkAtomicallyLocked(path, cx, cz, bytes);
    }

    void storeChunkAtomicallyLocked(const std::string& path,
                                    std::int32_t cx, std::int32_t cz,
                                    const std::vector<std::uint8_t>& bytes) {
        const std::string staged = path + ".new";
        const auto parent = std::filesystem::path(path).parent_path();
        // Only remove a stage created by this transaction. A pre-existing
        // `.new` may be the sole completed sibling-chunk snapshot.
        bool stagedOwned = false;
        try {
            if (!parent.empty()) {
                std::error_code directoryError;
                if (!storage_file_ops::createDirectoriesDurably(
                        parent, directoryError))
                    throw std::filesystem::filesystem_error(
                        "create durable region directory", parent,
                        directoryError);
            }
            const auto inspectRegularFile = [](const std::string& filePath,
                                               const char* label) {
                std::error_code inspectError;
                const auto status = std::filesystem::symlink_status(
                    filePath, inspectError);
                if (inspectError == std::errc::no_such_file_or_directory)
                    return false;
                if (inspectError)
                    throw std::filesystem::filesystem_error(
                        std::string("inspect ") + label, filePath,
                        inspectError);
                if (status.type() == std::filesystem::file_type::not_found)
                    return false;
                if (status.type() != std::filesystem::file_type::regular)
                    throw std::runtime_error(
                        std::string("refusing non-regular ") + label +
                        ": " + filePath);
                return true;
            };
            const bool primaryExists =
                inspectRegularFile(path, "primary region");
            const bool stagedExists =
                inspectRegularFile(staged, "staged region");

            bool havePrimary = primaryExists;
            if (stagedExists) {
                std::string validationError;
                bool unsupportedStagedContent = false;
                bool stagedValid = validateRegionChunkNbt(
                    staged, cx, cz, &validationError,
                    &unsupportedStagedContent);
                if (!stagedValid) {
                    if (unsupportedStagedContent)
                        throw std::runtime_error(
                            "cannot replace a staged region containing unsupported block states; preserving source: " +
                            validationError);
                    const auto recovery = recoverInvalidStagedRegion(
                        path, staged, cx, cz);
                    if (recovery.status !=
                            StagedRegionRecovery::Status::Recovered) {
                        throw std::runtime_error(
                            "cannot safely recover invalid staged region: " +
                            (recovery.error.empty() ? validationError
                                                    : recovery.error));
                    }
                    stagedValid = validateRegionChunkNbt(
                        staged, cx, cz, &validationError,
                        &unsupportedStagedContent);
                    if (!stagedValid)
                        throw std::runtime_error(
                            "recovered staged region failed validation: " +
                            validationError);
                }

                // A crash may leave a newer, fully written staged snapshot
                // beside a readable but stale primary. Promote it before
                // beginning another transaction.
                if (havePrimary) {
                    RegionFile stagedRegion(staged);
                    RegionFile primaryRegion(path);
                    std::string mergeError;
                    if (!mergeValidatedMissingChunks(
                            stagedRegion, primaryRegion, cx, cz,
                            &mergeError) ||
                        !persistence_detail::syncFile(staged))
                        throw std::runtime_error(
                            "cannot merge primary-only region chunks: " +
                            mergeError);
                }
                std::error_code promoteError;
                if (!persistence_detail::replaceFile(staged, path,
                                                     promoteError))
                    throw std::system_error(promoteError,
                                            "promote staged region before save");
                havePrimary = true;
                if (!persistence_detail::syncDirectory(parent))
                    throw std::runtime_error(
                        "cannot sync promoted region directory");
            }

            if (havePrimary) {
                std::error_code copyError;
                if (!storage_file_ops::copyFileExclusively(
                        path, staged, copyError, &stagedOwned, 0666))
                    throw std::system_error(copyError, "stage region file");
            } else {
                const std::array<std::uint8_t, 8192> emptyRegion{};
                std::error_code createError;
                if (!storage_file_ops::writeFileExclusively(
                        staged, emptyRegion.data(), emptyRegion.size(),
                        createError, &stagedOwned, 0666))
                    throw std::system_error(createError,
                                            "create staged region file");
            }

            RegionFile stagedRegion(staged);
            stagedRegion.store(cx & 31, cz & 31, bytes);
            if (!persistence_detail::syncFile(staged))
                throw std::runtime_error("cannot sync staged region file");
            std::error_code replaceError;
            if (!persistence_detail::replaceFile(staged, path, replaceError))
                throw std::system_error(replaceError, "replace region file");
            stagedOwned = false;
            if (!persistence_detail::syncDirectory(parent))
                throw std::runtime_error("cannot sync region directory");
        } catch (...) {
            if (stagedOwned) {
                std::error_code cleanupError;
                std::filesystem::remove(staged, cleanupError);
            }
            throw;
        }
    }

    mutable std::mutex configMtx_;
    mutable std::mutex lifecycleMtx_;
    std::atomic<bool> stopInProgress_{false};
    std::shared_ptr<CallbackState> callbackState_ =
        std::make_shared<CallbackState>();
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
    std::function<void(const nbt::Value&)> validateExtras_;
    std::function<std::function<void()>(const nbt::Value&)>
        prepareReadExtras_;
    std::function<bool(std::int32_t, std::int32_t)> chunkSaveAdmission_;
    std::function<void(std::int32_t, std::int32_t)> chunkSaveBarrier_;
    std::function<void(std::int32_t, std::int32_t)> chunkSaveComplete_;
    mutable std::mutex chunkNbtTemplatesMtx_;
    ChunkNbtTemplateMap chunkNbtTemplates_;
public:
    std::function<void(nbt::Value&)> provideLevelState_;
    std::function<void(const nbt::Value&)> consumeLevelState_;
private:

    std::mutex dirtyMtx_;
    // Serializes the acceptingDirty_ check with insertion, allowing stop() to
    // close admission and know that every previously accepted registration is
    // already represented in dirty_.
    std::mutex dirtyAdmissionMtx_;
    mutable std::mutex flushCallMtx_;
    std::condition_variable flushCallQuiescent_;
    bool acceptingFlushCalls_ = true;
    std::size_t activeFlushCalls_ = 0;
#ifdef CPPFM_WORLD_MUTATION_TEST
    std::atomic<std::uint32_t> dirtyAdmissionWaitersForTest_{0};
    std::function<void()> beforeDirtyCommitForTest_;
    std::function<void()> beforeFlushCallLeaveForTest_;
    std::function<void()> beforeStopAdmissionCheckForTest_;
#endif
    // A chunk can never be dirty without its current, nonzero save generation.
    // std::map retains the prior deterministic chunk-key flush order.
    std::map<std::int64_t, std::uint64_t> dirty_;
    std::uint64_t nextDirtyVersion_ = 1;
    std::thread worker_;
    std::condition_variable cv_;
    std::mutex cvMtx_;
    std::uint64_t dirtyWakeGeneration_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<bool> acceptingDirty_{false};
};

} // namespace cppfm
