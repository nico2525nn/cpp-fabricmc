#pragma once

#include "GameServer.hpp"
#include "Session.hpp"
#include "SessionInteractionSupport.hpp"

#include <cstddef>

namespace cppfm {

// One decoded UseItemOn packet, with the player/item snapshot that must still
// be current before any event or authoritative world mutation is committed.
struct Session::UseItemOnRequest {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::int32_t tx = 0;
    std::int32_t ty = 0;
    std::int32_t tz = 0;
    int face = 0;
    std::size_t inventorySlot = 36;
    bool survival = false;
    ItemUseContext context;
    session_detail::SessionPlayerSnapshot playerState;
    InvSlot heldItem = ItemStack::air();
    SimulationDispatchGuard& simulationLock;
};

} // namespace cppfm
