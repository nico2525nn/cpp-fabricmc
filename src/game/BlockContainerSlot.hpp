#pragma once

#include "BlockEntityData.hpp"
#include "MutationGateLease.hpp"

#include <cstddef>
#include <mutex>

namespace cppfm {

// A command editing a block inventory must keep world mutation serialized
// from block-state validation through persistence admission and owner commit.
// Declare the world lock first so reverse member destruction releases the
// owner lock before the world gate.
struct BlockContainerSlotAccess {
    BlockEntityOwner owner;
    ItemStack* stack = nullptr;
    std::size_t slotIndex = 0;
    MutationGateLease worldMutationLock;
    std::unique_lock<std::recursive_mutex> blockEntityLock;

    explicit operator bool() const noexcept { return stack != nullptr; }
    ItemStack& operator*() const noexcept { return *stack; }
    ItemStack* operator->() const noexcept { return stack; }

    void releaseLocks() noexcept {
        if (blockEntityLock.owns_lock()) blockEntityLock.unlock();
        if (worldMutationLock.owns_lock()) worldMutationLock.unlock();
    }
};

} // namespace cppfm
