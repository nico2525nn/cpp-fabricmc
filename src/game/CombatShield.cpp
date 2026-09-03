// CombatShield — plan44 §3 G-08 shield posture helpers (standalone TU).
// No GameServer methods are used here, so test_gameplay_full can link this file alone.
// tryShieldBlock / applyThornsReflection (need GameServer) live in CombatManager.cpp.
#include "CombatManager.hpp"
#include "GameServer.hpp"

namespace cppfm {

bool CombatManager::holdsShield(const Player& p) {
    if (p.heldSlot >= 0 && p.heldSlot < 9) {
        const auto& mh = p.inv[36 + p.heldSlot];
        if (!mh.empty() && mh.name().find("shield") != std::string::npos) return true;
    }
    const auto& off = p.inv[45];
    if (!off.empty() && off.name().find("shield") != std::string::npos) return true;
    return false;
}
bool CombatManager::isShieldBlocking(const Player& p) {
    // sneak-hold or UseItem-hold with shield, 5t to raise, not axe-disabled
    if (!(p.isBlocking || p.isSneaking)) return false;
    return isShieldActive(holdsShield(p), p.blockingTicks, p.shieldDisableTicks);
}
bool CombatManager::isFrontal(const Player& victim, double ax, double az) {
    return isFrontalAttack(victim.yaw, victim.x, victim.z, ax, az);
}

} // namespace cppfm
