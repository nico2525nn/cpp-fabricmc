// CombatManager: extracted combat/damage responsibility (plan8 modular split)
// Wraps DamageCalculator, EPF, armor sync and DamageSource classification.
// Pure functions are testable; GameServer delegates to this manager.
#pragma once
#include <cstdint>
#include <vector>
#include "DamageSource.hpp"
#include "MobEffects.hpp"

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

    // Attribute sync for armor
    static void syncPlayerArmor(GameServer& srv, Player& p);
};

} // namespace cppfm
