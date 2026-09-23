// EventBus: typed publish/subscribe events with priority + cancellation.
//
// "Modding API基盤の設計"): server internals fire strongly-typed events at
// well-defined points and listeners (built-in systems or future "mods") may
// observe or cancel them. Handlers run on the firing thread; ordering is by
// ascending priority value (lower = earlier), then registration order.
#pragma once
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace cppfm::api {

// Derive (or embed) to make an event cancellable.
struct Cancelable {
    bool cancelled = false;
    void cancel() { cancelled = true; }
};

class Subscription {
public:
    Subscription() = default;
    explicit Subscription(std::function<void()> release)
        : release_(std::move(release)) {}
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept
        : release_(std::move(other.release_)) {
        other.release_ = {};
    }
    Subscription& operator=(Subscription&& other) noexcept {
        if (this == &other) return *this;
        reset();
        release_ = std::move(other.release_);
        other.release_ = {};
        return *this;
    }
    ~Subscription() { reset(); }

    void reset() {
        if (!release_) return;
        auto release = std::move(release_);
        release();
    }

private:
    std::function<void()> release_;
};

namespace detail {
class BusBase {
public:
    using ErasedFn = std::function<void(void*)>;
    struct Entry {
        int priority = 0;
        std::uint64_t seq = 0;
        ErasedFn fn;
        std::mutex mutex;
        std::condition_variable drained;
        bool active = true;
        std::size_t inFlight = 0;
        std::size_t drainWaiters = 0;
    };
    using EntryRef = std::shared_ptr<Entry>;

    static BusBase& get(std::type_index type) {
        static std::mutex mtx;
        static std::unordered_map<std::type_index, BusBase> map;
        std::lock_guard lk(mtx);
        return map[type];
    }

    std::uint64_t addRaw(int priority, ErasedFn fn) {
        std::lock_guard lk(mutex_);
        auto entry = std::make_shared<Entry>();
        entry->priority = priority;
        entry->seq = ++sequence_;
        entry->fn = std::move(fn);
        entries_.emplace(std::pair<int, std::uint64_t>{priority, entry->seq}, entry);
        return entry->seq;
    }

    void remove(std::uint64_t seq) {
        EntryRef entry;
        {
            std::lock_guard lk(mutex_);
            for (auto it = entries_.begin(); it != entries_.end(); ++it) {
                if (it->first.second == seq) {
                    entry = it->second;
                    entries_.erase(it);
                    break;
                }
            }
        }
        if (!entry) return;
        const auto ownInvocations = static_cast<std::size_t>(std::count(
            invoking_.begin(), invoking_.end(), entry.get()));
        std::unique_lock lk(entry->mutex);
        entry->active = false;
        // A callback cannot wait for its own stack frames to return, but a
        // reentrant reset must still drain invocations on every other thread.
        if (entry->inFlight > ownInvocations) {
            ++entry->drainWaiters;
            entry->drained.wait(lk, [&] {
                return entry->inFlight <= ownInvocations;
            });
            --entry->drainWaiters;
        }
        ErasedFn released = std::move(entry->fn);
        lk.unlock();
        // Destroy outside the entry lock, but before reset returns. Snapshots
        // may still own EntryRefs that have not reached invoke() yet.
        released = nullptr;
    }

    std::vector<EntryRef> snapshot() const {
        std::vector<EntryRef> out;
        std::lock_guard lk(mutex_);
        out.reserve(entries_.size());
        for (const auto& [ignored, entry] : entries_) out.push_back(entry);
        return out;
    }

    void invoke(const EntryRef& entry, void* raw) const {
        ErasedFn callback;
        {
            std::lock_guard lk(entry->mutex);
            if (!entry->active) return;
            // Preserve snapshot semantics: each fire owns its callable copy.
            // A mutable target must not be invoked concurrently through the
            // same std::function object when events are fired on many threads.
            callback = entry->fn;
            ++entry->inFlight;
        }
        try {
            invoking_.push_back(entry.get());
        } catch (...) {
            callback = nullptr;
            finish(entry);
            throw;
        }
        try {
            callback(raw);
        } catch (...) {
            callback = nullptr;
            invoking_.pop_back();
            finish(entry);
            throw;
        }
        // Keep the in-flight count until destruction of the per-fire callable
        // is complete; its target may itself live in an unloadable module.
        callback = nullptr;
        invoking_.pop_back();
        finish(entry);
    }

private:
    static void finish(const EntryRef& entry) {
        std::lock_guard lk(entry->mutex);
        if (--entry->inFlight == 0 || entry->drainWaiters != 0)
            entry->drained.notify_all();
    }

    mutable std::mutex mutex_;
    std::multimap<std::pair<int, std::uint64_t>, EntryRef> entries_;
    std::uint64_t sequence_ = 0;
    static inline thread_local std::vector<const Entry*> invoking_{};
};
} // namespace detail

template <typename Ev>
class EventHook {
public:
    // Register a listener. Lower `priority` runs first. This legacy entry
    // point intentionally keeps the listener for the process lifetime.
    void subscribe(int priority, std::function<void(Ev&)> handler) const {
        detail::BusBase& bus = detail::BusBase::get(typeid(Ev));
        bus.addRaw(priority, [h = std::move(handler)](void* raw) {
            h(*static_cast<Ev*>(raw));
        });
    }

    // Scoped listeners can be unloaded after reset returns: it prevents new
    // calls and drains calls on other threads. A reset invoked from its own
    // callback cannot drain that stack frame; that handler's owner must defer
    // unloading its code until the callback returns.
    Subscription subscribeScoped(int priority,
                                 std::function<void(Ev&)> handler) const {
        detail::BusBase& bus = detail::BusBase::get(typeid(Ev));
        const auto seq = bus.addRaw(priority,
            [h = std::move(handler)](void* raw) {
                h(*static_cast<Ev*>(raw));
            });
        return Subscription([&bus, seq] { bus.remove(seq); });
    }

    bool fire(Ev& ev) const {
        auto& bus = detail::BusBase::get(typeid(Ev));
        for (const auto& entry : bus.snapshot()) bus.invoke(entry, &ev);
        if constexpr (std::is_base_of_v<Cancelable, Ev>) return !ev.cancelled;
        else return true;
    }
};

} // namespace cppfm::api
