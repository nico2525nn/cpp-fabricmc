#pragma once

#include "GameServer.hpp"

#include <mutex>

namespace cppfm::session_detail {

// Packet handlers take a stable player snapshot before accessing worlds or
// other entities. Keeping the snapshot shape shared avoids returning to the
// unsafe pattern of holding Player::stateMtx across callbacks and I/O.
struct SessionPlayerSnapshot {
    std::int8_t dimension = 0;
    std::uint8_t gamemode = 0;
    std::int32_t entityId = 0;
    std::int32_t heldSlot = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool sneaking = false;
    bool onGround = true;
    bool dead = false;
    bool inPlay = false;
    bool awaitingTeleportConfirmation = false;
};

inline SessionPlayerSnapshot snapshotPlayerForSession(const Player& player) {
    std::lock_guard playerLock(player.stateMtx);
    return {GameServer::canonicalDimension(player.dimension), player.gamemode,
            player.entityId, player.heldSlot, player.x, player.y, player.z,
            player.yaw, player.pitch, player.isSneaking, player.onGround,
            player.dead, player.inPlay, player.pendingTeleportId.has_value()};
}

inline bool interactionContextUnchanged(
    const Player& player, const SessionPlayerSnapshot& expected,
    const ItemStack& expectedHeldItem,
    const std::size_t inventorySlot) {
    std::lock_guard playerLock(player.stateMtx);
    if (GameServer::canonicalDimension(player.dimension) !=
            expected.dimension ||
        player.gamemode != expected.gamemode ||
        player.x != expected.x || player.y != expected.y ||
        player.z != expected.z ||
        player.yaw != expected.yaw || player.pitch != expected.pitch ||
        player.isSneaking != expected.sneaking ||
        player.dead != expected.dead || player.inPlay != expected.inPlay ||
        player.pendingTeleportId.has_value() !=
            expected.awaitingTeleportConfirmation)
        return false;

    if (inventorySlot >= player.inv.size()) return false;
    const bool selectedMainHand = inventorySlot >= 36 && inventorySlot <= 44;
    if (selectedMainHand &&
        (expected.heldSlot < 0 || expected.heldSlot >= 9 ||
         inventorySlot != static_cast<std::size_t>(36 + expected.heldSlot) ||
         player.heldSlot != expected.heldSlot))
        return false;
    const auto& currentHeld = player.inv[inventorySlot];
    return currentHeld.count == expectedHeldItem.count &&
           currentHeld.sameItemData(expectedHeldItem);
}

inline double squaredDistanceToBox(double x, double y, double z,
                                   double minX, double minY, double minZ,
                                   double maxX, double maxY, double maxZ) {
    const double dx = x < minX ? minX - x : (x > maxX ? x - maxX : 0.0);
    const double dy = y < minY ? minY - y : (y > maxY ? y - maxY : 0.0);
    const double dz = z < minZ ? minZ - z : (z > maxZ ? z - maxZ : 0.0);
    return dx * dx + dy * dy + dz * dz;
}

} // namespace cppfm::session_detail
