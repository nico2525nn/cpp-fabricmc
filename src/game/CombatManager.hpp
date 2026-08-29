// CombatManager: extracted combat/damage responsibility (plan8 modular split)
// Wraps DamageCalculator, EPF, armor sync and DamageSource classification.
// Pure functions are testable; GameServer delegates to this manager.
// plan25 combat polish: verify no regressions from W16-W19 world, B7-B26 block, E1/E3 entity after wt25 merges; caps 30/20 intact.
// plan26 combat polish: verify no regressions from D5/D6 palette, D10 registry, D11 slot, D16/D17 warden, D19/D20 particles, D22/D25 network; caps 30/20, E7 weight1, sonic 15x20 bypass intact.
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
