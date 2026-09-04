// CombatManager: extracted combat/damage responsibility (plan8 modular split) Wraps DamageCalculator, EPF, armor sync and DamageSource
// classification. Pure functions are testable; GameServer delegates to this manager. plan25/26/28 combat: verify combat intact across
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
    // Armor helpers
    static int armorForItem(uint32_t itemId);
    static int totalArmorForPlayer(const Player& p);
    static int totalArmorForMob(const MobEntity& m);

    // EPF calculation (vanilla-accurate per DamageSource category)
    static int computeEPF(const DamageSource& ds, const Player& p);
    static int computeEPF(const DamageSource& ds, const MobEntity& m);

    // Damage pipeline wrappers
    static float calculatePlayerDamage(float base, const DamageSource& src,
                                       int armor, double toughness, int epf,
                                       const std::vector<EffectInstance>& effects);
    static float calculateMobDamage(float base, const DamageSource& src,
                                    int armor, double toughness, int epf,
                                    const std::vector<EffectInstance>& effects);

    // High-level apply (includes armor sync, exhaustion, health, packets)
    static void applyToPlayer(GameServer& srv, Player& p, float amount, const DamageSource& src);
    static void applyToMob(GameServer& srv, MobEntity& m, float amount, const DamageSource& src);

    // plan44 §3 G-08 shield block (vanilla Blocking: 100% frontal negate after 5t, axe 100t disable)
    // defined in CombatShield.cpp (standalone TU: no GameServer methods, unit-linkable)
    static bool holdsShield(const Player& p);
    static bool isShieldBlocking(const Player& p);
    static bool isFrontal(const Player& victim, double ax, double az);
    // true = damage fully negated (durability consumed); axe attacks disable instead and pass through
    static bool tryShieldBlock(GameServer& srv, Player& victim, const DamageSource& src,
                               double attackerX, double attackerZ, bool attackerWeaponIsAxe);
    // plan44 §3 G-09 thorns: reflect to melee attacker from victim armor (per-piece lv*15%)
    static void applyThornsReflection(GameServer& srv, Player& victim,
                                      MobEntity* attackerMob, Player* attackerPlayer);

    // Attribute sync for armor
    static void syncPlayerArmor(GameServer& srv, Player& p);
};

} // namespace cppfm
