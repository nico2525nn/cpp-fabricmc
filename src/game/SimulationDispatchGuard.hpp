#pragma once

#include "../net/Connection.hpp"

#include <algorithm>
#include <iterator>
#include <mutex>
#include <utility>
#include <vector>

namespace cppfm {

// The simulation gate is recursive because callbacks can re-enter the server.
// Each owned guard contributes to activeSimulationDispatchDepth, which
// Connection::sendFramed uses to enqueue writes instead of performing socket
// I/O while a simulation transition owns mutable server state.
class SimulationDispatchGuard {
public:
    explicit SimulationDispatchGuard(std::recursive_mutex& mutex)
        : mutex_(&mutex), lock_(mutex) {
        registerOwnedGuard();
    }
    SimulationDispatchGuard(const SimulationDispatchGuard&) = delete;
    SimulationDispatchGuard& operator=(const SimulationDispatchGuard&) = delete;

    void unlock() {
        if (!lock_.owns_lock()) return;
        unregisterOwnedGuard();
        lock_.unlock();
    }
    void lock() {
        if (lock_.owns_lock()) return;
        lock_.lock();
        try {
            registerOwnedGuard();
        } catch (...) {
            lock_.unlock();
            throw;
        }
    }

    // Execute extension/user code without holding the global simulation gate,
    // then restore the caller's serialization boundary even if it throws.
    // This is required when a callback synchronously queues work for the tick
    // thread: that thread must be able to enter the gate and drain its queue.
    // Release every recursive acquisition of this mutex on the current thread;
    // releasing only this guard would leave an outer nested guard blocking the
    // callback's queued server-thread work.
    template <typename Callback>
    decltype(auto) withLockReleased(Callback&& callback) {
        if (!owns_lock()) return std::forward<Callback>(callback)();

        std::vector<SimulationDispatchGuard*> released;
        for (auto* guard : activeGuards_) {
            if (guard && guard->mutex_ == mutex_ && guard->owns_lock())
                released.push_back(guard);
        }
        for (auto it = released.rbegin(); it != released.rend(); ++it)
            (*it)->unlock();

        struct Relock {
            std::vector<SimulationDispatchGuard*>& guards;
            ~Relock() {
                for (auto* guard : guards) {
                    if (guard && !guard->owns_lock()) guard->lock();
                }
            }
        } restore{released};
        return std::forward<Callback>(callback)();
    }

    // Use from code that may run under a simulation dispatch guard but does
    // not receive that guard explicitly (notably tick/AI damage callbacks).
    // With no active guard this simply invokes the callback.
    template <typename Callback>
    static decltype(auto) withCurrentLockReleased(Callback&& callback) {
        for (auto it = activeGuards_.rbegin(); it != activeGuards_.rend(); ++it) {
            if (*it && (*it)->owns_lock())
                return (*it)->withLockReleased(
                    std::forward<Callback>(callback));
        }
        return std::forward<Callback>(callback)();
    }

    static bool currentThreadOwns(
        const std::recursive_mutex* mutex) noexcept {
        return mutex && std::any_of(
            activeGuards_.begin(), activeGuards_.end(),
            [mutex](const SimulationDispatchGuard* guard) {
                return guard && guard->mutex_ == mutex && guard->owns_lock();
            });
    }

    bool owns_lock() const noexcept { return lock_.owns_lock(); }

    ~SimulationDispatchGuard() {
        if (lock_.owns_lock()) {
            unregisterOwnedGuard();
            lock_.unlock();
        }
    }

private:
    void registerOwnedGuard() {
        activeGuards_.push_back(this);
        ++activeSimulationDispatchDepth;
    }

    void unregisterOwnedGuard() noexcept {
        const auto it = std::find(activeGuards_.rbegin(), activeGuards_.rend(), this);
        if (it != activeGuards_.rend()) {
            activeGuards_.erase(std::next(it).base());
            --activeSimulationDispatchDepth;
        }
    }

    std::recursive_mutex* mutex_;
    std::unique_lock<std::recursive_mutex> lock_;
    inline static thread_local std::vector<SimulationDispatchGuard*> activeGuards_;
};

// Command dispatch and simulation state have a strict lock order: acquire the
// command mutex first, then restore the simulation gate. Keeping that order in
// one RAII type prevents command ingress from waiting for the command mutex
// while retaining the gate needed by its current owner.
class CommandDispatchGuard {
public:
    CommandDispatchGuard(SimulationDispatchGuard& simulationLock,
                         std::recursive_mutex& commandMutex)
        : lock_(commandMutex, std::defer_lock) {
        simulationLock.withLockReleased([&] { lock_.lock(); });
    }

    CommandDispatchGuard(const CommandDispatchGuard&) = delete;
    CommandDispatchGuard& operator=(const CommandDispatchGuard&) = delete;

private:
    std::unique_lock<std::recursive_mutex> lock_;
};

} // namespace cppfm
