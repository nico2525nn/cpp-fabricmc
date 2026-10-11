#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

namespace cppfm {

// The authoritative mining attempt is a single versioned state. Callers use
// Player::digTransitionMtx to serialize protocol-visible transitions; this
// class owns the data lock and never invokes callbacks while holding it.
class MiningProgress {
public:
    struct Snapshot {
        bool active = false;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t z = 0;
        std::int64_t startTick = 0;
        std::int32_t totalTicks = 0;
        std::uint8_t lastStage = 255;
        std::uint64_t revision = 0;
    };

    [[nodiscard]] Snapshot snapshot() const {
        std::lock_guard lock(mutex_);
        return state_;
    }

    Snapshot start(std::int32_t x, std::int32_t y, std::int32_t z,
                   std::int64_t startTick, std::int32_t totalTicks,
                   std::uint8_t initialStage) {
        std::lock_guard lock(mutex_);
        state_.active = true;
        state_.x = x;
        state_.y = y;
        state_.z = z;
        state_.startTick = startTick;
        state_.totalTicks = totalTicks;
        state_.lastStage = initialStage;
        ++state_.revision;
        return state_;
    }

    // A missing result means the expected revision was superseded. A returned
    // inactive snapshot is a successful no-op, distinguishing stale work from
    // an already-idle state without exposing unlocked fields.
    std::optional<Snapshot> cancel(
        std::optional<std::uint64_t> expectedRevision = std::nullopt,
        bool advanceInactiveRevision = false) {
        std::lock_guard lock(mutex_);
        if (expectedRevision && state_.revision != *expectedRevision)
            return std::nullopt;

        const Snapshot ended = state_;
        state_.active = false;
        state_.totalTicks = 0;
        state_.lastStage = 255;
        if (ended.active || advanceInactiveRevision) ++state_.revision;
        return ended;
    }

    bool setStageIfCurrent(std::uint64_t expectedRevision,
                           std::uint8_t stage) {
        std::lock_guard lock(mutex_);
        if (!state_.active || state_.revision != expectedRevision)
            return false;
        state_.lastStage = stage;
        return true;
    }

    std::optional<std::uint64_t> finishIfCurrent(
        std::uint64_t expectedRevision) {
        std::lock_guard lock(mutex_);
        if (!state_.active || state_.revision != expectedRevision)
            return std::nullopt;
        state_.active = false;
        state_.totalTicks = 0;
        state_.lastStage = 255;
        return ++state_.revision;
    }

    [[nodiscard]] bool inactiveAtRevision(
        std::uint64_t expectedRevision) const {
        std::lock_guard lock(mutex_);
        return !state_.active && state_.revision == expectedRevision;
    }

private:
    mutable std::mutex mutex_;
    Snapshot state_;
};

} // namespace cppfm
