// EnchantmentHelper — plan8 entity section
// Centralizes enchantment calculations: protection EPF, sharpness, efficiency, etc.
// Vanilla formulas referenced from plan8.md § Enchant effects.
#pragma once
#include <string>
#include <algorithm>
#include <cmath>
#include "Items.hpp"
#include "DamageSource.hpp"

namespace cppfm {

class EnchantmentHelper {
public:
    // Protection EPF calculation (mirrors GameServer::computeProtectionEPF but as static helper)
    // EPF weighting: protection=1, fire/blast/projectile=2 when matching damage type, feather=3 for fall.
    static int getProtectionEPF(const DamageSource& ds, const ItemStack& stack) {
        if (ds.bypassEnchant || ds.isDrown() || ds.isStarveFlag) return 0;
        int prot = std::max(stack.enchantLevel("protection"), stack.enchantLevel("minecraft:protection"));
        int fire = std::max(stack.enchantLevel("fire_protection"), stack.enchantLevel("minecraft:fire_protection"));
        int blast= std::max(stack.enchantLevel("blast_protection"), stack.enchantLevel("minecraft:blast_protection"));
        int proj = std::max(stack.enchantLevel("projectile_protection"), stack.enchantLevel("minecraft:projectile_protection"));
        int feather= std::max(stack.enchantLevel("feather_falling"), stack.enchantLevel("minecraft:feather_falling"));
        int total=0;
        int weight = 1;
        if (ds.isFire() || ds.isExplosion() || ds.isProjectile()) weight=2;
        else if (ds.isFall()) weight=1;
        total += prot * weight;
        if (ds.isFire()) total += fire*2;
        if (ds.isExplosion()) total += blast*2;
        if (ds.isProjectile()) total += proj*2;
        if (ds.isFall()) total += feather*3;
        return std::min(total, 20);
    }

    // Sharpness bonus for melee (vanilla: 0.5 * level + 0.5? simplified)
    static float getSharpnessBonus(const ItemStack& stack) {
        int lvl = std::max(stack.enchantLevel("sharpness"), stack.enchantLevel("minecraft:sharpness"));
        if (lvl<=0) return 0.f;
        return 0.5f * lvl + 0.5f;
    }

    // Power bonus for bows
    static float getPowerBonus(const ItemStack& stack) {
        int lvl = std::max(stack.enchantLevel("power"), stack.enchantLevel("minecraft:power"));
        if (lvl<=0) return 0.f;
        return 0.25f * (lvl+1);
    }

    // Efficiency: mining speed multiplier (vanilla: lvl^2 +1)
    static float getEfficiencyMultiplier(const ItemStack& stack) {
        int lvl = std::max(stack.enchantLevel("efficiency"), stack.enchantLevel("minecraft:efficiency"));
        if (lvl<=0) return 1.f;
        return 1.f + float(lvl*lvl + 1) * 0.3f; // simplified
    }

    // Fortune level
    static int getFortune(const ItemStack& stack) { return stack.fortuneLevel(); }

    // Silk touch
    static bool hasSilkTouch(const ItemStack& stack) { return stack.hasSilkTouch(); }

    // Frost Walker (freezes water when walking)
    static bool hasFrostWalker(const ItemStack& stack) {
        return stack.hasEnchant("frost_walker") || stack.hasEnchant("minecraft:frost_walker");
    }
    // Soul Speed (speed on soul sand)
    static bool hasSoulSpeed(const ItemStack& stack) {
        return stack.hasEnchant("soul_speed") || stack.hasEnchant("minecraft:soul_speed");
    }
    // Swift Sneak (sneak speed)
    static bool hasSwiftSneak(const ItemStack& stack) {
        return stack.hasEnchant("swift_sneak") || stack.hasEnchant("minecraft:swift_sneak");
    }
    // Unbreaking handled in DamageComponent, but expose helper
    static int getUnbreaking(const ItemStack& stack) {
        return std::max(stack.enchantLevel("unbreaking"), stack.enchantLevel("minecraft:unbreaking"));
    }
    // Mending
    static bool hasMending(const ItemStack& stack) {
        return stack.hasEnchant("mending") || stack.hasEnchant("minecraft:mending");
    }

    // Generic enchant existence
    static bool hasEnchant(const ItemStack& s, const std::string& name) { return s.hasEnchant(name); }
    static int level(const ItemStack& s, const std::string& name) { return s.enchantLevel(name); }

    // Melee damage bonus including enchant (used in onUseEntity)
    static float meleeDamageWithEnchant(float base, const ItemStack& weapon) {
        return base + getSharpnessBonus(weapon);
    }
};

} // namespace cppfm
