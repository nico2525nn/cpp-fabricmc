#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cppfm {

// Serializes only transitions for the same authenticated player identity.
// The gate is held by an incoming session while it retires an older session,
// waits for that session's cleanup, loads durable state, and publishes its own
// roster entry. Cleanup never acquires this gate, so waiting for cleanup cannot
// form a cycle with the incoming transition.
class PlayerLoginHandoffCoordinator final {
    struct IdentityGate {
        std::mutex mutex;
    };

public:
    class Lease final {
    public:
        Lease(Lease&&) noexcept = default;
        Lease& operator=(Lease&&) noexcept = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

    private:
        friend class PlayerLoginHandoffCoordinator;
        explicit Lease(std::shared_ptr<IdentityGate> gate)
            : gate_(std::move(gate)), lock_(gate_->mutex) {}

        std::shared_ptr<IdentityGate> gate_;
        std::unique_lock<std::mutex> lock_;
    };

    Lease enter(const std::string& uuidHex) {
        std::shared_ptr<IdentityGate> gate;
        {
            std::lock_guard registryLock(mutex_);
            for (auto it = gates_.begin(); it != gates_.end();) {
                if (it->second.expired())
                    it = gates_.erase(it);
                else
                    ++it;
            }
            auto& weakGate = gates_[uuidHex];
            gate = weakGate.lock();
            if (!gate) {
                gate = std::make_shared<IdentityGate>();
                weakGate = gate;
            }
        }
        return Lease(std::move(gate));
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
    // A failed retirement is sticky for this server lifetime. Allowing a later
    // attempt to load after the old state could not be durably saved would
    // silently admit stale player data.
    std::unordered_set<std::string> blockedIdentities_;
};

} // namespace cppfm
