#pragma once

#include "Items.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>

namespace cppfm {

struct ChestData {
    static constexpr int kSlots = 27;
    ItemStack slots[kSlots];
};

struct FurnaceData {
    static constexpr int kInput = 0, kFuel = 1, kOutput = 2;
    ItemStack slots[3];
    std::int16_t burnTicks = 0;
    std::int16_t burnDuration = 0;
    std::int16_t cookProgress = 0;
    std::int16_t cookTotal = 200;
};

struct BrewingData {
    static constexpr int kSlots = 5;
    static constexpr int kBrewTicks = 400;
    static constexpr int kFuelPerBlaze = 20;
    ItemStack slots[kSlots];
    std::int16_t brewTime = 0;
    std::int16_t fuel = 0;
};

struct GenericContainerData {
    static constexpr int kMaxSlots = 9;
    ItemStack slots[kMaxSlots];
    std::uint8_t slotCount = kMaxSlots;
};

// CrafterBlockEntity is a nine-slot recipe inventory, but its disabled-slot
// mask and redstone bookkeeping are distinct from a dispenser/dropper. Keep
// that state separate so automation cannot accidentally treat a disabled
// recipe slot as an ordinary container slot.
struct CrafterData {
    static constexpr int kSlots = 9;
    ItemStack slots[kSlots];
    std::uint16_t disabledSlots = 0; // bit i corresponds to recipe slot i
    std::int32_t craftingTicksRemaining = 0;
    std::int64_t redstoneCraftDueTick = -1;
    bool triggered = false;

    bool isSlotDisabled(int slot) const {
        return slot >= 0 && slot < kSlots &&
               (disabledSlots & (std::uint16_t{1} << slot)) != 0;
    }
    void setSlotEnabled(int slot, bool enabled) {
        if (slot < 0 || slot >= kSlots) return;
        const auto bit = static_cast<std::uint16_t>(std::uint16_t{1} << slot);
        if (enabled) disabledSlots = static_cast<std::uint16_t>(disabledSlots & ~bit);
        else disabledSlots = static_cast<std::uint16_t>(disabledSlots | bit);
    }
};

struct MovingPistonData {
    std::uint16_t state = 0;
    std::int32_t facing = 0;
    bool extending = true;
    bool source = false;
    float progress = 0.f;
    std::int64_t finishTick = 0;
};

struct SignData {
    std::string front[4];
    std::string back[4];
    bool hasFront = false;
    bool hasBack = false;
};

// Copyable payload state deliberately excludes the live synchronization
// identity. Persistence and network projections take this value while holding
// the owner's mutex, without allocating a throwaway mutex per snapshot record.
struct BlockEntityState {
    enum class Kind {
        Chest,
        Furnace,
        Hopper,
        Dispenser,
        Dropper,
        Barrel,
        ShulkerBox,
        Brewing,
        Crafter,
        MovingPiston,
        Sign
    };

    Kind kind = Kind::Chest;
    ChestData chest{};
    FurnaceData furnace{};
    GenericContainerData generic{};
    CrafterData crafter{};
    BrewingData brewing{};
    MovingPistonData movingPiston{};
    SignData sign{};

    void swapState(BlockEntityState& other) noexcept {
        static_assert(std::is_nothrow_swappable_v<decltype(kind)>);
        static_assert(std::is_nothrow_swappable_v<ChestData>);
        static_assert(std::is_nothrow_swappable_v<FurnaceData>);
        static_assert(std::is_nothrow_swappable_v<GenericContainerData>);
        static_assert(std::is_nothrow_swappable_v<CrafterData>);
        static_assert(std::is_nothrow_swappable_v<BrewingData>);
        static_assert(std::is_nothrow_swappable_v<MovingPistonData>);
        static_assert(std::is_nothrow_swappable_v<SignData>);
        using std::swap;
        swap(kind, other.kind);
        swap(chest, other.chest);
        swap(furnace, other.furnace);
        swap(generic, other.generic);
        swap(crafter, other.crafter);
        swap(brewing, other.brewing);
        swap(movingPiston, other.movingPiston);
        swap(sign, other.sign);
    }
};

struct BlockEntity : BlockEntityState {
    // Menus from different sessions can operate on one container while the
    // server tick is ticking the same block entity. Live copies retain this
    // lock identity. Persistence snapshots use BlockEntityState instead.
    mutable std::shared_ptr<std::recursive_mutex> stateMtx =
        std::make_shared<std::recursive_mutex>();

    BlockEntity() = default;
    BlockEntity(const BlockEntity&) = default;
    BlockEntity(BlockEntity&&) noexcept = default;
    void swapState(BlockEntity& other) noexcept {
        BlockEntityState::swapState(other);
        // stateMtx is an ownership/locking identity, not replaceable state.
    }
    BlockEntity& operator=(const BlockEntity& other) {
        if (this == &other) return *this;
        // Build every allocation-bearing value before mutating the live
        // owner. Once the copy succeeds, the no-throw swap gives assignment
        // the strong exception guarantee while retaining this owner's lock.
        BlockEntity staged(other);
        swapState(staged);
        return *this;
    }
    bool isDropper() const { return kind == Kind::Dropper; }
    bool isDispenser() const { return kind == Kind::Dispenser; }
};

using BlockEntityOwner = std::shared_ptr<BlockEntity>;

} // namespace cppfm
