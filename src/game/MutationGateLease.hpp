#pragma once

#include <algorithm>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace cppfm {

// A mutation-gate lock carries the identity of the World whose coordinated
// mutation boundary it protects. The identity is also registered per-thread
// so separately-owned stores can fail closed when callers omit that boundary.
// Like the recursive mutex it owns, a live lease is confined to its acquiring
// thread; violating this contract terminates rather than unlocking illegally.
class MutationGateLease {
public:
    MutationGateLease() = default;

    MutationGateLease(const void* owner,
                      std::unique_lock<std::recursive_mutex>&& lock)
        : owner_(owner), lock_(std::move(lock)), registered_(
              owner_ != nullptr && lock_.owns_lock()),
          ownerThread_(lock_.owns_lock() ? std::this_thread::get_id()
                                         : std::thread::id{}) {
        if (lock_.owns_lock() && !owner_)
            throw std::invalid_argument(
                "an owned mutation-gate lease requires a World identity");
        if (registered_) heldOwners().push_back(owner_);
    }

    MutationGateLease(const MutationGateLease&) = delete;
    MutationGateLease& operator=(const MutationGateLease&) = delete;

    MutationGateLease(MutationGateLease&& other) noexcept
        : owner_(std::exchange(other.owner_, nullptr)),
          lock_(std::move(other.lock_)),
          registered_(std::exchange(other.registered_, false)),
          ownerThread_(other.ownerThread_) {
        requireCurrentThread(ownerThread_);
        other.ownerThread_ = {};
    }

    MutationGateLease& operator=(MutationGateLease&& other) noexcept {
        if (this == &other) return *this;
        requireCurrentThread(ownerThread_);
        requireCurrentThread(other.ownerThread_);
        unregister();
        lock_ = std::move(other.lock_);
        owner_ = std::exchange(other.owner_, nullptr);
        registered_ = std::exchange(other.registered_, false);
        ownerThread_ = std::exchange(other.ownerThread_, std::thread::id{});
        return *this;
    }

    ~MutationGateLease() {
        requireCurrentThread(ownerThread_);
        unregister();
    }

    bool ownsLock() const noexcept {
        requireCurrentThread(ownerThread_);
        return lock_.owns_lock();
    }
    bool owns_lock() const noexcept { return ownsLock(); }
    bool ownedByCurrentThread() const noexcept {
        return lock_.owns_lock() &&
               ownerThread_ == std::this_thread::get_id();
    }
    const void* owner() const noexcept { return owner_; }

    void unlock() {
        if (!lock_.owns_lock()) return;
        requireCurrentThread(ownerThread_);
        unregister();
        lock_.unlock();
        ownerThread_ = {};
    }

    static bool heldByCurrentThread(const void* owner) {
        if (!owner) return false;
        const auto& owners = heldOwners();
        return std::find(owners.begin(), owners.end(), owner) != owners.end();
    }

private:
    static void requireCurrentThread(std::thread::id ownerThread) noexcept {
        if (ownerThread != std::thread::id{} &&
            ownerThread != std::this_thread::get_id())
            std::terminate();
    }

    static std::vector<const void*>& heldOwners() {
        static thread_local std::vector<const void*> owners;
        return owners;
    }

    void unregister() noexcept {
        if (!registered_) return;
        requireCurrentThread(ownerThread_);
        auto& owners = heldOwners();
        const auto it = std::find(owners.rbegin(), owners.rend(), owner_);
        if (it != owners.rend()) owners.erase(std::next(it).base());
        registered_ = false;
    }

    const void* owner_ = nullptr;
    std::unique_lock<std::recursive_mutex> lock_;
    bool registered_ = false;
    std::thread::id ownerThread_{};
};

}  // namespace cppfm
