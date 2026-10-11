#pragma once

#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cppfm {

inline std::optional<bool> waitForPlayerHandoffCleanup(
    const std::shared_future<bool>& completion,
    std::chrono::steady_clock::time_point deadline) {
    if (!completion.valid() ||
        completion.wait_until(deadline) != std::future_status::ready)
        return std::nullopt;
    return completion.get();
}

enum class HandoffCleanupPolicy {
    BestEffort,
    RequiredForReplacement,
};

class PlayerHandoffReadiness final {
public:
    void recordFailure(HandoffCleanupPolicy policy) noexcept {
        if (policy == HandoffCleanupPolicy::RequiredForReplacement)
            ready_ = false;
    }
    bool ready() const noexcept { return ready_; }

private:
    bool ready_ = true;
};

template <typename Step, typename FailureReporter>
void runPlayerHandoffCleanupStep(std::string_view name,
                                 HandoffCleanupPolicy policy,
                                 PlayerHandoffReadiness& readiness,
                                 Step&& step,
                                 FailureReporter&& reportFailure) {
    try {
        std::forward<Step>(step)();
    } catch (const std::exception& error) {
        readiness.recordFailure(policy);
        std::forward<FailureReporter>(reportFailure)(name, &error);
    } catch (...) {
        readiness.recordFailure(policy);
        std::forward<FailureReporter>(reportFailure)(name, nullptr);
    }
}

// Serializes transitions only for the same authenticated player identity.
// Incoming sessions hold this lease while retiring the prior session, waiting
// for its cleanup, loading durable state, and publishing their roster entry.
class PlayerLoginHandoffCoordinator final {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::seconds kCleanupTimeout{30};

private:
    struct IdentityGate final {
        std::timed_mutex mutex;
    };

public:
    class Lease final {
    public:
        Lease(Lease&& other) noexcept
            : gate_(std::move(other.gate_)), lock_(std::move(other.lock_)),
              waited_(other.waited_) {}
        Lease& operator=(Lease&&) = delete;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        bool waited() const noexcept { return waited_; }

    private:
        friend class PlayerLoginHandoffCoordinator;
        explicit Lease(std::shared_ptr<IdentityGate> gate)
            : gate_(std::move(gate)), lock_(gate_->mutex, std::defer_lock) {}

        // Declared before lock_ so reverse destruction unlocks before the
        // shared owner can destroy the gate's mutex.
        std::shared_ptr<IdentityGate> gate_;
        std::unique_lock<std::timed_mutex> lock_;
        bool waited_ = false;
    };

    std::optional<Lease> enterUntil(
        const std::string& uuidHex, Clock::time_point deadline) {
        if (Clock::now() >= deadline) return std::nullopt;
        std::shared_ptr<IdentityGate> gate;
        {
            std::lock_guard registryLock(mutex_);
            for (auto it = gates_.begin(); it != gates_.end();) {
                if (it->second.expired()) it = gates_.erase(it);
                else ++it;
            }
            auto& weakGate = gates_[uuidHex];
            gate = weakGate.lock();
            if (!gate) {
                gate = std::make_shared<IdentityGate>();
                weakGate = gate;
            }
        }
        Lease lease(std::move(gate));
        if (lease.lock_.try_lock()) {
            if (Clock::now() >= deadline) return std::nullopt;
            return std::optional<Lease>(std::move(lease));
        }
        lease.waited_ = true;
        if (!lease.lock_.try_lock_until(deadline)) return std::nullopt;
        if (Clock::now() >= deadline) return std::nullopt;
        return std::optional<Lease>(std::move(lease));
    }

    bool blocked(const std::string& uuidHex) const {
        std::lock_guard registryLock(mutex_);
        return blockedIdentities_.contains(uuidHex);
    }

    void block(const std::string& uuidHex) {
        std::lock_guard registryLock(mutex_);
        blockedIdentities_.insert(uuidHex);
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::weak_ptr<IdentityGate>> gates_;
    // A failed retirement is sticky for this server lifetime: admitting a
    // later session could silently load stale player data.
    std::unordered_set<std::string> blockedIdentities_;
};

} // namespace cppfm
