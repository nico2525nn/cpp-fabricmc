#pragma once

#include "MenuInteraction.hpp"

#include <array>
#include <cstdint>

namespace cppfm::session_menu_detail {

// Menu transactions must either return a whole stack to inventory or leave
// both the menu and inventory unchanged. Keep that policy shared between
// ordinary menu clicks and recipe placement.
inline bool insertCompleteInventoryStack(
    std::array<ItemStack, kPlayerInventorySlotCount>& inventory,
    const ItemStack& source) {
    if (source.empty()) return true;
    return insertCompletePlayerStack(
        inventory, source, static_cast<std::uint16_t>(source.count),
        maxStackFor(source));
}

} // namespace cppfm::session_menu_detail
