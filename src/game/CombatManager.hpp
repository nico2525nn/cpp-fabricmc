// merges (caps 30/20, E7 w1, sonic bypass).
#pragma once
#include <cstdint>
#include <vector>
#include "DamageSource.hpp"
#include "MobEffects.hpp"
#include "Items.hpp"
#include "MeleeHelper.hpp"

namespace cppfm {
struct Player;
struct MobEntity;
class GameServer;

class CombatManager {
public:
    // EPF calculation (vanilla-accurate per DamageSource category)
    static int computeEPF(const DamageSource& ds, const Player& p);
    static int computeEPF(const DamageSource& ds, const MobEntity& m);

    // defined in CombatShield.cpp (standalone TU: no GameServer methods, unit-linkable)
    static bool holdsShield(const Player& p);
    static bool isShieldBlocking(const Player& p);
    static bool isFrontal(const Player& victim, double ax, double az);
    static bool tryShieldBlock(GameServer& srv, Player& victim, const DamageSource& src,
                               double attackerX, double attackerZ, bool attackerWeaponIsAxe);
    static void applyThornsReflection(GameServer& srv, Player& victim,
                                      MobEntity* attackerMob, Player* attackerPlayer);

    // Attribute sync for armor
    static void syncPlayerArmor(GameServer& srv, Player& p);
};

} // namespace cppfm
