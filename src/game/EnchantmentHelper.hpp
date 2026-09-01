// EnchantmentHelper — plan8 entity section
// plan22 combat polish: E7 strict weight 1 for Protection (was 2 for fire/explosion/projectile), sonic_boom bypass all
// plan28 combat polish: verify EnchantmentHelper remains orthogonal to Scoreboard ResetScore 0x49 (D26) — EPF weight 1 (protection=1, fire/blast/proj=2, feather=3), sonic_boom bypassEnchant, sharpness/efficiency formulas verified intact after deep 31 merges.
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
        // plan22 combat polish: sonic_boom bypasses all enchantments (including protection)
        if (ds.isSonic()) return 0;
        int prot = std::max(stack.enchantLevel("protection"), stack.enchantLevel("minecraft:protection"));
        int fire = std::max(stack.enchantLevel("fire_protection"), stack.enchantLevel("minecraft:fire_protection"));
        int blast= std::max(stack.enchantLevel("blast_protection"), stack.enchantLevel("minecraft:blast_protection"));
        int proj = std::max(stack.enchantLevel("projectile_protection"), stack.enchantLevel("minecraft:projectile_protection"));
        int feather= std::max(stack.enchantLevel("feather_falling"), stack.enchantLevel("minecraft:feather_falling"));
        int total=0;
        // plan22 E7 strict: Protection weight 1 for all (was prot*2 for fire/explosion/projectile)
        total += prot;
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

    // Efficiency: mining speed multiplier (vanilla: lvl^2 +1, plan13 §5)
    static float getEfficiencyMultiplier(const ItemStack& stack) {
        int lvl = efficiencyLevel(stack);
        if (lvl<=0) return 1.f;
        return 1.f + float(lvl*lvl + 1); // vanilla: base * (1 + lvl^2+1)
    }
    static int efficiencyLevel(const ItemStack& stack) {
        return std::max(stack.enchantLevel("efficiency"), stack.enchantLevel("minecraft:efficiency"));
    }
    static int frostWalkerLevel(const ItemStack& stack) {
        return std::max(stack.enchantLevel("frost_walker"), stack.enchantLevel("minecraft:frost_walker"));
    }
    static int soulSpeedLevel(const ItemStack& stack) {
        return std::max(stack.enchantLevel("soul_speed"), stack.enchantLevel("minecraft:soul_speed"));
    }
    static int swiftSneakLevel(const ItemStack& stack) {
        return std::max(stack.enchantLevel("swift_sneak"), stack.enchantLevel("minecraft:swift_sneak"));
    }
    static float miningSpeedBonus(int lvl){ return lvl==0?1.0f: 1.0f + float(lvl*lvl + 1); }
    static float soulSpeedBonus(int lvl){ return 1.0f + 0.105f*float(lvl); }
    static float swiftSneakFactor(int lvl){ return 0.3f + 0.08f*float(lvl); } // plan13 §5 simplified

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
    // plan37 B-11: 7 new enchants (32/41) — mending already, infinity/silk/fortune/channeling/riptide/curses
    static bool hasInfinity(const ItemStack& stack) {
        return stack.hasEnchant("infinity") || stack.hasEnchant("minecraft:infinity");
    }
    static int getInfinity(const ItemStack& stack) {
        return std::max(stack.enchantLevel("infinity"), stack.enchantLevel("minecraft:infinity"));
    }
    static bool hasChanneling(const ItemStack& stack) {
        return stack.hasEnchant("channeling") || stack.hasEnchant("minecraft:channeling");
    }
    static int getChanneling(const ItemStack& stack) {
        return std::max(stack.enchantLevel("channeling"), stack.enchantLevel("minecraft:channeling"));
    }
    static bool hasRiptide(const ItemStack& stack) {
        return stack.hasEnchant("riptide") || stack.hasEnchant("minecraft:riptide");
    }
    static int getRiptide(const ItemStack& stack) {
        return std::max(stack.enchantLevel("riptide"), stack.enchantLevel("minecraft:riptide"));
    }
    static bool hasBindingCurse(const ItemStack& stack) {
        return stack.hasEnchant("binding_curse") || stack.hasEnchant("minecraft:binding_curse")
            || stack.hasEnchant("binding") || stack.hasEnchant("minecraft:binding");
    }
    static bool hasVanishingCurse(const ItemStack& stack) {
        return stack.hasEnchant("vanishing_curse") || stack.hasEnchant("minecraft:vanishing_curse")
            || stack.hasEnchant("vanishing") || stack.hasEnchant("minecraft:vanishing");
    }
    static int getMending(const ItemStack& stack) {
        return std::max(stack.enchantLevel("mending"), stack.enchantLevel("minecraft:mending"));
    }
    // additional enchants to reach 32/41 coverage (plan37 B-11 remaining 2 to fill 25→32)
    static int getLoyalty(const ItemStack& s) { return std::max(s.enchantLevel("loyalty"), s.enchantLevel("minecraft:loyalty")); }
    static int getImpaling(const ItemStack& s) { return std::max(s.enchantLevel("impaling"), s.enchantLevel("minecraft:impaling")); }

    // Generic enchant existence
    static bool hasEnchant(const ItemStack& s, const std::string& name) { return s.hasEnchant(name); }
    static int level(const ItemStack& s, const std::string& name) { return s.enchantLevel(name); }

    // Melee damage bonus including enchant (used in onUseEntity)
    static float meleeDamageWithEnchant(float base, const ItemStack& weapon) {
        return base + getSharpnessBonus(weapon);
    }
};

} // namespace cppfm
