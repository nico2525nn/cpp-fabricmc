#pragma once

#include "Items.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace cppfm {

// Per-session state for an item whose ConsumableComponent completes over
// multiple authoritative server ticks. Session packet handling and server
// ticking are serialized by the simulation-dispatch gate.
class ConsumableUseState {
public:
    enum class Phase : std::uint8_t { Idle, Active, Ready, Cancelled };
    enum class TickResult : std::uint8_t { Inactive, Active, Ready };

    static std::optional<std::uint32_t> durationTicks(
        const float consumeSeconds) noexcept {
        if (!std::isfinite(consumeSeconds) || consumeSeconds <= 0.0F)
            return std::nullopt;
        // Vanilla multiplies the component's float duration by 20f before
        // applying ceil. Promoting first changes the result for values such
        // as 1.6f (32 ticks becomes 33 due to its binary representation).
        const float ticks = consumeSeconds * 20.0F;
        if (!std::isfinite(ticks)) return std::nullopt;
        const float rounded = std::max(1.0F, std::ceil(ticks));
        constexpr auto kMaxTicks =
            static_cast<double>(std::numeric_limits<std::int32_t>::max());
        if (!std::isfinite(rounded) ||
            static_cast<double>(rounded) > kMaxTicks)
            return std::nullopt;
        return static_cast<std::uint32_t>(rounded);
    }

    bool start(const std::size_t inventorySlot, const float consumeSeconds,
               const ItemStack& heldStack) {
        const auto ticks = durationTicks(consumeSeconds);
        return ticks && startWithTicks(inventorySlot, *ticks, heldStack);
    }

    bool startWithTicks(const std::size_t inventorySlot,
                        const std::uint32_t duration,
                        const ItemStack& heldStack) {
        if (inventorySlot >= kPlayerInventorySlotCount || duration == 0 ||
            duration > static_cast<std::uint32_t>(
                           std::numeric_limits<std::int32_t>::max()) ||
            heldStack.empty() ||
            (phase_ != Phase::Idle && phase_ != Phase::Cancelled))
            return false;
        inventorySlot_ = inventorySlot;
        durationTicks_ = duration;
        elapsedTicks_ = 0;
        heldStack_ = heldStack;
        phase_ = Phase::Active;
        return true;
    }

    TickResult advanceOneTick() noexcept {
        if (phase_ == Phase::Ready) return TickResult::Ready;
        if (phase_ != Phase::Active) return TickResult::Inactive;
        ++elapsedTicks_;
        if (elapsedTicks_ >= durationTicks_) {
            phase_ = Phase::Ready;
            return TickResult::Ready;
        }
        return TickResult::Active;
    }

    // Releasing early cancels. Once the authoritative final tick has marked
    // the use ready, a later release cannot revoke its completion.
    bool release() noexcept {
        if (phase_ == Phase::Active) {
            cancel();
            return false;
        }
        return phase_ == Phase::Ready;
    }

    void cancel() noexcept {
        if (phase_ != Phase::Active) return;
        clear(Phase::Cancelled);
    }

    bool matchesActiveStack(const std::size_t inventorySlot,
                            const ItemStack& stack) const {
        return phase_ == Phase::Active && inventorySlot == inventorySlot_ &&
               !stack.empty() && heldStack_.count == stack.count &&
               heldStack_.sameItemData(stack);
    }

    std::optional<std::size_t> activeSlot() const noexcept {
        if (phase_ != Phase::Active) return std::nullopt;
        return inventorySlot_;
    }

    std::optional<std::size_t> takeCompletedSlot() noexcept {
        if (phase_ != Phase::Ready) return std::nullopt;
        const auto completedSlot = inventorySlot_;
        clear(Phase::Idle);
        return completedSlot;
    }

    Phase phase() const noexcept { return phase_; }

private:
    void clear(const Phase nextPhase) noexcept {
        phase_ = nextPhase;
        inventorySlot_ = 0;
        durationTicks_ = 0;
        elapsedTicks_ = 0;
        heldStack_ = ItemStack::air();
    }

    Phase phase_ = Phase::Idle;
    std::size_t inventorySlot_ = 0;
    std::uint32_t durationTicks_ = 0;
    std::uint32_t elapsedTicks_ = 0;
    ItemStack heldStack_;
};

} // namespace cppfm
