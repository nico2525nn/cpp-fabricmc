#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cppfm {

// Serializes transitions only for the same authenticated player identity.
// Incoming sessions hold this lease while retiring the prior session, waiting
// for its cleanup, loading durable state, and publishing their roster entry.
class PlayerLoginHandoffCoordinator final {
    struct IdentityGate final {
        std::mutex mutex;
    };

public:
    class Lease final {
    public:
        Lease(Lease&& other) noexcept
            : gate_(std::move(other.gate_)), lock_(std::move(other.lock_)) {}
        Lease& operator=(Lease&&) = delete;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

    private:
        friend class PlayerLoginHandoffCoordinator;
        explicit Lease(std::shared_ptr<IdentityGate> gate)
            : gate_(std::move(gate)), lock_(gate_->mutex) {}

        // Declared before lock_ so reverse destruction unlocks before the
        // shared owner can destroy the gate's mutex.
        std::shared_ptr<IdentityGate> gate_;
        std::unique_lock<std::mutex> lock_;
    };

    Lease enter(const std::string& uuidHex) {
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
    // A failed retirement is sticky for this server lifetime: admitting a
    // later session could silently load stale player data.
    std::unordered_set<std::string> blockedIdentities_;
};

} // namespace cppfm
