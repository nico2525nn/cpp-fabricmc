#pragma once

#include "NativeHandleTable.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace cppfm::jvm {

// Player handles may be created only for admitted live players. Callers retain
// the Player owner for the full lifecycle operation, so the address cannot be
// recycled while an in-flight call is waiting for this mutex. Only live and
// retiring entries are retained; completed lifecycles leave no address history.
class PlayerHandleLifetime {
public:
    explicit PlayerHandleLifetime(NativeHandleTable& handles) noexcept
        : handles_(handles) {}

    template <typename IsLive>
    std::uint64_t activate(void* address,
                           std::atomic<std::uint8_t>& lifecycle,
                           IsLive&& isLive) {
        return activate(address, lifecycle,
                        std::forward<IsLive>(isLive), [] {});
    }

    // The hook overload provides a deterministic point before acquiring the
    // lifetime lock for race regression tests. Production callers should use
    // the two-argument overload.
    template <typename IsLive, typename BeforeLock>
    std::uint64_t activate(void* address,
                           std::atomic<std::uint8_t>& lifecycle,
                           IsLive&& isLive,
                           BeforeLock&& beforeLock) {
        if (!address) return 0;
        std::forward<BeforeLock>(beforeLock)();
        std::lock_guard lock(mutex_);
        if (lifecycle.load(std::memory_order_acquire) != kOpen) return 0;
        const auto found = states_.find(address);
        if (found != states_.end() && found->second != State::Live) return 0;
        // Admission must be revalidated under the same lock that serializes
        // retirement. Otherwise a stale positive check can run after the
        // retirement tombstone has been removed and publish a dangling handle.
        if (!std::forward<IsLive>(isLive)()) return 0;

        auto state = found;
        bool insertedState = false;
        if (state == states_.end()) {
            const auto result = states_.try_emplace(address, State::Live);
            state = result.first;
            insertedState = result.second;
        }

        const auto rollbackState = [&] {
            if (insertedState) states_.erase(state);
        };
        try {
            const auto handle = registerLiveHandle(address);
            if (handle == 0) rollbackState();
            return handle;
        } catch (...) {
            rollbackState();
            throw;
        }
    }

    template <typename IsLive>
    std::uint64_t publish(void* address,
                          const std::atomic<std::uint8_t>& lifecycle,
                          IsLive&& isLive) {
        if (!address || !isLive()) return 0;
        std::lock_guard lock(mutex_);
        if (lifecycle.load(std::memory_order_acquire) != kOpen) return 0;
        const auto found = states_.find(address);
        if (found == states_.end() || found->second != State::Live) return 0;
        return registerLiveHandle(address);
    }

    // Preserve an already-published handle for the quit callback, while
    // preventing subsequent callbacks and lookups from publishing another.
    // A pending marker also closes the activation-vs-retirement race when
    // retirement reaches this lock before the first handle publication.
    std::uint64_t beginRetirement(
        void* address, std::atomic<std::uint8_t>& lifecycle) {
        return beginRetirement(address, lifecycle, [] {});
    }

    // Deterministic pre-lock hook for the delayed-retirement regression test.
    template <typename BeforeLock>
    std::uint64_t beginRetirement(
        void* address, std::atomic<std::uint8_t>& lifecycle,
        BeforeLock&& beforeLock) {
        if (!address) return 0;
        std::forward<BeforeLock>(beforeLock)();
        std::lock_guard lock(mutex_);
        auto phase = lifecycle.load(std::memory_order_acquire);
        if (phase == kFinished) return 0;
        if (phase == kOpen) {
            lifecycle.store(kRetiring, std::memory_order_release);
            phase = kRetiring;
        }
        auto found = states_.find(address);
        if (found == states_.end()) {
            if (phase != kRetiring ||
                lifecycle.load(std::memory_order_acquire) != kRetiring)
                return 0;
            found = states_.try_emplace(address, State::Retiring).first;
        } else if (found->second == State::Live) {
            found->second = State::Retiring;
        }
        return handles_.findHandle(address, HandleKind::Player).value_or(0);
    }

    // Claim the single Fabric quit callback for this retiring player. The
    // claim remains latched through callback completion and handle invalidation,
    // so a racing Session cleanup and duplicate-login takeover cannot dispatch
    // LEAVE/DISCONNECT twice.
    std::uint64_t claimQuitCallback(
        void* address, std::atomic<std::uint8_t>& lifecycle) {
        if (!address) return 0;
        std::lock_guard lock(mutex_);
        auto phase = lifecycle.load(std::memory_order_acquire);
        if (phase == kFinished) return 0;
        if (phase == kOpen) {
            lifecycle.store(kRetiring, std::memory_order_release);
            phase = kRetiring;
        }
        auto found = states_.find(address);
        if (found == states_.end() && phase == kRetiring) {
            found = states_.try_emplace(address, State::Retiring).first;
        } else if (found != states_.end() && found->second == State::Live) {
            found->second = State::Retiring;
        }
        if (found == states_.end() || found->second != State::Retiring)
            return 0;
        const auto handle = handles_.findHandle(address, HandleKind::Player);
        if (!handle) return 0;
        found->second = State::QuitCallbackInProgress;
        return *handle;
    }

    bool completeQuitCallback(
        void* address, std::atomic<std::uint8_t>& lifecycle) {
        if (!address) return false;
        std::lock_guard lock(mutex_);
        if (lifecycle.load(std::memory_order_acquire) == kFinished)
            return false;
        const auto found = states_.find(address);
        if (found == states_.end())
            return false;
        if (found->second == State::QuitCallbackInProgress) {
            found->second = State::QuitCallbackCompleted;
        } else if (found->second ==
                   State::QuitCallbackInProgressFinalizeRequested) {
            found->second = State::QuitCallbackCompletedFinalizeRequested;
        } else {
            return false;
        }
        return true;
    }

    // A directory finalizer can race the callback after it has returned but
    // before its still-valid handle is invalidated. The callback owner checks
    // this latch after invalidation and completes any deferred finalization.
    bool hasPendingRetirementFinalization(void* address) const {
        if (!address) return false;
        std::lock_guard lock(mutex_);
        const auto found = states_.find(address);
        return found != states_.end() &&
            found->second == State::QuitCallbackCompletedFinalizeRequested;
    }

    // A native operation that already resolved a Player handle must release
    // the handle-table borrow before calling this method. Requiring the exact
    // live handle prevents a disconnect racing completed retirement from
    // creating a new tombstone for an object no longer in the directory.
    bool beginRetirementForHandle(void* address,
                                  std::uint64_t expectedHandle,
                                  std::atomic<std::uint8_t>& lifecycle) {
        if (!address || !expectedHandle) return false;
        std::lock_guard lock(mutex_);
        const auto found = states_.find(address);
        if (found == states_.end() || found->second != State::Live)
            return false;
        if (lifecycle.load(std::memory_order_acquire) != kOpen) return false;
        const auto current = handles_.findHandle(address, HandleKind::Player);
        if (!current || *current != expectedHandle) return false;
        lifecycle.store(kRetiring, std::memory_order_release);
        found->second = State::Retiring;
        return true;
    }

    // A Java wrapper may be created only for an admitted, non-retiring Player.
    // This lock order matches registration and retirement: lifetime -> table.
    bool isLiveHandle(std::uint64_t handle) const {
        if (!handle) return false;
        std::lock_guard lock(mutex_);
        const auto association = addressesByHandle_.find(handle);
        if (association == addressesByHandle_.end()) return false;
        const auto found = states_.find(association->second);
        if (found == states_.end() || found->second != State::Live)
            return false;
        return handles_.valid(handle, HandleKind::Player);
    }

    // Invalidate the old handle after the quit callback. Keep the tombstone
    // until directory removal so stale membership cannot reactivate the same
    // Player object in the intervening window.
    std::optional<std::uint64_t> retire(
        void* address, std::atomic<std::uint8_t>& lifecycle) {
        if (!address) return std::nullopt;
        std::lock_guard lock(mutex_);
        auto phase = lifecycle.load(std::memory_order_acquire);
        if (phase == kFinished) return std::nullopt;
        if (phase == kOpen) {
            lifecycle.store(kRetiring, std::memory_order_release);
            phase = kRetiring;
        }
        auto state = states_.find(address);
        if (state == states_.end()) {
            if (phase != kRetiring) return std::nullopt;
            state = states_.try_emplace(address, State::Retiring).first;
        } else if (state->second == State::QuitCallbackInProgress ||
                   state->second ==
                       State::QuitCallbackInProgressFinalizeRequested) {
            return std::nullopt;
        } else if (state->second == State::Live) {
            state->second = State::Retiring;
        }
        const auto handle = handles_.findHandle(address, HandleKind::Player);
        if (handle) handles_.invalidateHandle(*handle);
        eraseMappingsFor(address);
        return handle;
    }

    bool finishRetirement(void* address,
                          std::atomic<std::uint8_t>& lifecycle) {
        if (!address) return false;
        std::lock_guard lock(mutex_);
        auto found = states_.find(address);
        if (found == states_.end()) return false;
        if (found->second == State::QuitCallbackInProgress) {
            found->second = State::QuitCallbackInProgressFinalizeRequested;
            return false;
        }
        if (found->second == State::QuitCallbackInProgressFinalizeRequested)
            return false;
        const bool handleIsLive =
            handles_.findHandle(address, HandleKind::Player).has_value();
        if (found->second == State::QuitCallbackCompleted && handleIsLive) {
            found->second = State::QuitCallbackCompletedFinalizeRequested;
            return false;
        }
        if ((found->second != State::Retiring &&
             found->second != State::QuitCallbackCompleted &&
             found->second !=
                 State::QuitCallbackCompletedFinalizeRequested) ||
            handleIsLive)
            return false;
        lifecycle.store(kFinished, std::memory_order_release);
        eraseMappingsFor(address);
        states_.erase(found);
        return true;
    }

    void clear() {
        std::lock_guard lock(mutex_);
        states_.clear();
        addressesByHandle_.clear();
    }

private:
    static constexpr std::uint8_t kOpen = 0;
    static constexpr std::uint8_t kRetiring = 1;
    static constexpr std::uint8_t kFinished = 2;
    enum class State : std::uint8_t {
        Live,
        Retiring,
        QuitCallbackInProgress,
        QuitCallbackInProgressFinalizeRequested,
        QuitCallbackCompleted,
        QuitCallbackCompletedFinalizeRequested,
    };

    std::uint64_t registerLiveHandle(void* address) {
        const auto existing = handles_.findHandle(address, HandleKind::Player);
        const auto handle = handles_.registerObject(address, HandleKind::Player);
        if (!handle) return 0;
        try {
            const auto [association, inserted] =
                addressesByHandle_.try_emplace(handle, address);
            if (!inserted && association->second != address) {
                if (!existing) handles_.invalidateHandle(handle);
                return 0;
            }
        } catch (...) {
            if (!existing) handles_.invalidateHandle(handle);
            throw;
        }
        return handle;
    }

    void eraseMappingsFor(void* address) {
        for (auto it = addressesByHandle_.begin();
             it != addressesByHandle_.end();) {
            if (it->second == address)
                it = addressesByHandle_.erase(it);
            else
                ++it;
        }
    }

    NativeHandleTable& handles_;
    mutable std::mutex mutex_;
    std::unordered_map<void*, State> states_;
    std::unordered_map<std::uint64_t, void*> addressesByHandle_;
};

} // namespace cppfm::jvm
