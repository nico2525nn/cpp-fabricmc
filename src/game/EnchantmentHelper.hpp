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
#include "Entities.hpp"
#include "MeleeHelper.hpp"

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

    // plan40 C-08: 9 new accessors (smite/bane/punch/flame/knockback/luck/lure/aqua/respiration)
    static bool hasAquaAffinity(const ItemStack& s){ return s.hasEnchant("aqua_affinity")||s.hasEnchant("minecraft:aqua_affinity"); }
    static int getRespiration(const ItemStack& s){ return std::max(s.enchantLevel("respiration"), s.enchantLevel("minecraft:respiration")); }
    static int getSmite(const ItemStack& s){ return std::max(s.enchantLevel("smite"), s.enchantLevel("minecraft:smite")); }
    static int getBaneOfArthropods(const ItemStack& s){ return std::max(s.enchantLevel("bane_of_arthropods"), s.enchantLevel("minecraft:bane_of_arthropods")); }
    static int getPunch(const ItemStack& s){ return std::max(s.enchantLevel("punch"), s.enchantLevel("minecraft:punch")); }
    static int getFlame(const ItemStack& s){ return std::max(s.enchantLevel("flame"), s.enchantLevel("minecraft:flame")); }
    static int getKnockback(const ItemStack& s){ return std::max(s.enchantLevel("knockback"), s.enchantLevel("minecraft:knockback")); }
    static int getLuckOfSea(const ItemStack& s){ return std::max(s.enchantLevel("luck_of_the_sea"), s.enchantLevel("minecraft:luck_of_the_sea")); }
    static int getLure(const ItemStack& s){ return std::max(s.enchantLevel("lure"), s.enchantLevel("minecraft:lure")); }
    // crossbow helpers (deferred but accessor present for 41/41)
    static int getMultishot(const ItemStack& s){ return std::max(s.enchantLevel("multishot"), s.enchantLevel("minecraft:multishot")); }
    static int getPiercing(const ItemStack& s){ return std::max(s.enchantLevel("piercing"), s.enchantLevel("minecraft:piercing")); }
    static int getQuickCharge(const ItemStack& s){ return std::max(s.enchantLevel("quick_charge"), s.enchantLevel("minecraft:quick_charge")); }
    // plan44 §3 G-09: remaining effect getters (41/41 coverage — vanilla 1.21.4 has 42 incl. breach/density/wind_burst)
    static int getSweepingEdge(const ItemStack& s){ return std::max(s.enchantLevel("sweeping_edge"), s.enchantLevel("minecraft:sweeping_edge")); }
    static int getThorns(const ItemStack& s){ return std::max(s.enchantLevel("thorns"), s.enchantLevel("minecraft:thorns")); }
    static int getDepthStrider(const ItemStack& s){ return std::max(s.enchantLevel("depth_strider"), s.enchantLevel("minecraft:depth_strider")); }
    static int getBreach(const ItemStack& s){ return std::max(s.enchantLevel("breach"), s.enchantLevel("minecraft:breach")); }
    static int getDensity(const ItemStack& s){ return std::max(s.enchantLevel("density"), s.enchantLevel("minecraft:density")); }
    static int getWindBurst(const ItemStack& s){ return std::max(s.enchantLevel("wind_burst"), s.enchantLevel("minecraft:wind_burst")); }
    static int getFireAspect(const ItemStack& s){ return std::max(s.enchantLevel("fire_aspect"), s.enchantLevel("minecraft:fire_aspect")); }
    static int getLooting(const ItemStack& s){ return std::max(s.enchantLevel("looting"), s.enchantLevel("minecraft:looting")); }
    // plan44 §3 G-09 effect functions (pure, delegate to MeleeHelper formulas):
    // sweep victims take round(1 + AD*lv/(lv+1)); lv0 => 1
    static float sweepingDamage(float attackDamage, const ItemStack& weapon) {
        return sweepingEdgeDamage(attackDamage, getSweepingEdge(weapon));
    }
    // breach pre-discounts armor input (DamageCalculator formula untouched — E-06 lock)
    static int breachArmor(int armor, const ItemStack& weapon) {
        return breachAdjustedArmor(armor, getBreach(weapon));
    }
    // density mace smash bonus from attacker fall distance
    static float densityBonus(int fallBlocks, const ItemStack& weapon) {
        return densitySmashBonus(fallBlocks, getDensity(weapon));
    }
    static bool isAquatic(MobKind k) {
        switch (k) {
            case MobKind::Drowned: case MobKind::Guardian: case MobKind::ElderGuardian:
            case MobKind::Squid: case MobKind::GlowSquid: case MobKind::Turtle:
            case MobKind::Axolotl: case MobKind::Cod: case MobKind::Salmon:
            case MobKind::Pufferfish: case MobKind::TropicalFish: case MobKind::Dolphin:
            case MobKind::Frog: case MobKind::Tadpole: return true;
            default: return false;
        }
    }
    // impaling +2.5/lv vs aquatic mobs (JE)
    static float impalingBonusFor(const ItemStack& weapon, MobKind victimKind) {
        if (!isAquatic(victimKind)) return 0.f;
        return 2.5f * static_cast<float>(getImpaling(weapon));
    }
    // thorns reflect roll: returns reflected damage (0 = no proc); per-piece independent
    static int thornsReflect(const ItemStack& armorPiece, float rollProc, float rollDmg) {
        int lv = std::max(armorPiece.enchantLevel("thorns"), armorPiece.enchantLevel("minecraft:thorns"));
        if (lv <= 0 || rollProc >= 0.15f * static_cast<float>(lv)) return 0;
        int d = 1 + static_cast<int>(rollDmg * 4.f);
        return std::clamp(d, 1, 4);
    }
    // riptide launch distance in blocks (9/15/21 for I/II/III)
    static float riptideBlocks(const ItemStack& trident) {
        int lv = getRiptide(trident);
        if (lv <= 0) return 0.f;
        return 3.f + 6.f * static_cast<float>(std::min(lv, 3));
    }
    // multishot: 3 arrows, durability cost of 1 (plan44 §3)
    static int multishotShots(const ItemStack& crossbow) {
        return getMultishot(crossbow) > 0 ? 3 : 1;
    }
    static int frostRadiusFor(const ItemStack& boots) {
        int lv = frostWalkerLevel(boots);
        return 2 + std::max(0, lv);
    }

    // plan40 C-08: undead / arthropod helpers for smite/bane
    static bool isUndead(MobKind k){
        switch(k){
            case MobKind::Zombie: case MobKind::Skeleton: case MobKind::WitherSkeleton:
            case MobKind::Drowned: case MobKind::Husk: case MobKind::Stray:
            case MobKind::Phantom: case MobKind::Wither: case MobKind::Bogged:
            case MobKind::ZombieVillager: case MobKind::Zoglin: case MobKind::SkeletonHorse:
            case MobKind::ZombieHorse: return true;
            default: return false;
        }
    }
    static bool isArthropod(MobKind k){
        switch(k){ case MobKind::Spider: case MobKind::CaveSpider: case MobKind::Silverfish:
                 case MobKind::Endermite: case MobKind::Bee: return true; default: return false; }
    }
    static float aquaAffinityMiningPenalty(const ItemStack& helm){ return hasAquaAffinity(helm) ? 1.f : 0.2f; }

    // Generic enchant existence
    static bool hasEnchant(const ItemStack& s, const std::string& name) { return s.hasEnchant(name); }
    static int level(const ItemStack& s, const std::string& name) { return s.enchantLevel(name); }

    // Melee damage bonus including enchant (used in onUseEntity) — base sharpness only (compat)
    static float meleeDamageWithEnchant(float base, const ItemStack& weapon) {
        return base + getSharpnessBonus(weapon);
    }
    // plan40 C-08: victim-aware melee (sharp + smite 2.5*lvl vs undead + bane 2.5*lvl vs arthropod)
    static float meleeDamageWithEnchant(float base, const ItemStack& weapon, MobKind victimKind) {
        float bonus = getSharpnessBonus(weapon);
        if (isUndead(victimKind)) bonus += 2.5f * getSmite(weapon);
        if (isArthropod(victimKind)) bonus += 2.5f * getBaneOfArthropods(weapon);
        return base + bonus;
    }
    static float extraDamageFor(const ItemStack& weapon, MobKind victimKind){
        float extra=0;
        extra += getSharpnessBonus(weapon);
        if (isUndead(victimKind)) extra += 2.5f * getSmite(weapon);
        if (isArthropod(victimKind)) extra += 2.5f * getBaneOfArthropods(weapon);
        return extra;
    }
};

} // namespace cppfm
