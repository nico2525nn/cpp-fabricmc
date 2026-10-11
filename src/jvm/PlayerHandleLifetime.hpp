#pragma once

#include "NativeHandleTable.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace cppfm::jvm {

// Every Player handle publication must pass through this gate. A player is
// activated only by its admitted join path; beginning retirement closes the
// gate before the quit callback, and final retirement invalidates its handle.
// The Retiring tombstone remains until roster removal is committed, so even an
// explicit activation using stale roster-only evidence cannot reopen the gate.
class PlayerHandleLifetime {
public:
    explicit PlayerHandleLifetime(NativeHandleTable& handles) noexcept
        : handles_(handles) {}

    template <typename IsLive>
    std::uint64_t activate(void* address, IsLive&& isLive) {
        if (!address) return 0;
        std::lock_guard lock(mutex_);
        if (!std::forward<IsLive>(isLive)()) return 0;

        const auto found = states_.find(address);
        if (found != states_.end() && found->second != State::Live) return 0;
        if (found == states_.end()) states_.emplace(address, State::Live);
        return handles_.registerObject(address, HandleKind::Player);
    }

    template <typename IsLive>
    std::uint64_t publish(void* address, IsLive&& isLive) {
        if (!address) return 0;
        std::lock_guard lock(mutex_);
        const auto found = states_.find(address);
        if (found == states_.end() || found->second != State::Live ||
            !std::forward<IsLive>(isLive)())
            return 0;
        return handles_.registerObject(address, HandleKind::Player);
    }

    // Keep the existing handle valid for the quit callback, while preventing
    // every other callback or online-player lookup from publishing a handle.
    std::uint64_t beginRetirement(void* address) {
        if (!address) return 0;
        std::lock_guard lock(mutex_);
        const auto found = states_.try_emplace(address, State::Retiring).first;
        found->second = State::Retiring;
        return handles_.findHandle(address, HandleKind::Player).value_or(0);
    }

    // Invalidate the handle but retain the tombstone until PlayerDirectory has
    // removed the player. A later join activation cannot reuse stale roster
    // membership to revive this retired object.
    std::optional<std::uint64_t> retire(void* address) {
        if (!address) return std::nullopt;
        std::lock_guard lock(mutex_);
        states_.insert_or_assign(address, State::Retiring);

        const auto handle = handles_.findHandle(address, HandleKind::Player);
        if (handle) handles_.invalidateHandle(*handle);
        return handle;
    }

    // Called only after PlayerDirectory has removed the retired Player. Do not
    // discard its tombstone while a handle is still active.
    bool finishRetirement(void* address) {
        if (!address) return false;
        std::lock_guard lock(mutex_);
        const auto found = states_.find(address);
        if (found == states_.end() || found->second != State::Retiring ||
            handles_.findHandle(address, HandleKind::Player).has_value())
            return false;
        states_.erase(found);
        return true;
    }

    // Runtime shutdown clears the handle table immediately afterwards. Keeping
    // this operation separate avoids taking the Java-object cache lock here.
    void clear() {
        std::lock_guard lock(mutex_);
        states_.clear();
    }

private:
    enum class State : std::uint8_t { Live, Retiring };

    NativeHandleTable& handles_;
    std::mutex mutex_;
    std::unordered_map<void*, State> states_;
};

} // namespace cppfm::jvm
