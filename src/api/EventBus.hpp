// EventBus: typed publish/subscribe events with priority + cancellation.
//
// "Modding API基盤の設計"): server internals fire strongly-typed events at
// well-defined points and listeners (built-in systems or future "mods") may
// observe or cancel them. Handlers run on the firing thread; ordering is by
// ascending priority value (lower = earlier), then registration order.
#pragma once
#include <functional>
#include <map>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace cppfm::api {

// Derive (or embed) to make an event cancellable.
struct Cancelable {
    bool cancelled = false;
    void cancel() { cancelled = true; }
};

namespace detail {
class BusBase {
public:
    using ErasedFn = std::function<void(void*)>;
    struct Entry { int priority; std::uint64_t seq; ErasedFn fn; };

    static BusBase& get(std::type_index type) {
        static std::mutex mtx;
        static std::unordered_map<std::type_index, BusBase> map;
        std::lock_guard lk(mtx);
        return map[type];
    }

    void addRaw(int priority, ErasedFn fn) {
        std::lock_guard lk(mtx_);
        entries_.emplace(std::pair<int, std::uint64_t>{priority, ++seq_}, std::move(fn));
    }
    // ordered snapshot
    std::vector<ErasedFn> snapshot() {
        std::vector<ErasedFn> out;
        std::lock_guard lk(mtx_);
        out.reserve(entries_.size());
        for (auto& e : entries_) out.push_back(e.second);
        return out;
    }

private:
    std::mutex mtx_;
    std::multimap<std::pair<int, std::uint64_t>, ErasedFn> entries_;  // key comparable
    std::uint64_t seq_ = 0;
};
} // namespace detail

template <typename Ev>
class EventHook {
public:
    // Register a listener. Lower `priority` runs first.
    void subscribe(int priority, std::function<void(Ev&)> handler) const {
        detail::BusBase& bus = detail::BusBase::get(typeid(Ev));
        bus.addRaw(priority, [h = std::move(handler)](void* raw) { h(*static_cast<Ev*>(raw)); });
    }
    bool fire(Ev& ev) const {
        for (auto& fn : detail::BusBase::get(typeid(Ev)).snapshot()) fn(&ev);
        if constexpr (std::is_base_of_v<Cancelable, Ev>) return !ev.cancelled;
        else return true;
    }
};

} // namespace cppfm::api
