// DamageComponent — plan8 entity section
// Handles item durability (minecraft:damage component) with Unbreaking and Mending.
// Vanilla logic: Unbreaking gives (100 / (level+1))% chance to actually take damage;
// Mending repairs via XP orbs (handled elsewhere). This component centralizes durability.
#pragma once
#include <cstdint>
#include <cstdlib>
#include "Items.hpp"

namespace cppfm {

class DamageComponent {
public:
    // Apply damage to a stack, respecting Unbreaking enchantment.
    // Returns true if the stack was destroyed (damage >= max).
    // If hasUnbreaking, each point of damage has a chance to be ignored.
    // Vanilla Yarn EnchantmentHelper.shouldDamage: tools 1/(lvl+1), armor 60% + 40%/(lvl+1) per plan17 §10 E9 (vanilla armor: 60% ignore + 1/(lvl+1)).
    static bool applyDamage(ItemStack& stack, int amount) {
        if (stack.empty() || amount<=0) return false;
        int maxd = ItemStack::maxDamageFor(stack.itemId);
        if (maxd<=0) return false;
        int unb = stack.unbreakingLevel();
        if (unb>0) {
            int effective = 0;
            bool isArmor = stack.isArmor();
            for (int i=0;i<amount;++i) {
                if (isArmor) {
                    if ((rand() % 100) < 60) continue; // 60% ignore for armor (vanilla)
                }
                if (rand() % (unb + 1) == 0) effective++;
            }
            amount = effective;
            if (amount==0) return false;
        }
        return stack.applyDamage(amount);
    }
    // Unbreaking check helper for shouldDamage (mirrors Yarn EnchantmentHelper.shouldDamage)
    static bool shouldDamage(const ItemStack& stack) {
        int unb = stack.unbreakingLevel();
        if (unb<=0) return true;
        if (stack.isArmor()) {
            if ((rand() % 100) < 60) return false; // 60% ignore for armor
        }
        return rand() % (unb + 1) == 0;
    }

    // Repair via Mending: consume XP to repair one durability point.
    // Returns true if repaired.
    static bool mend(ItemStack& stack, int xp) {
        if (stack.empty()) return false;
        int dmg = stack.getDamage();
        if (dmg<=0) return false;
        // 2 durability per 1 xp (vanilla)
        int repair = xp * 2;
        int newDmg = dmg - repair;
        if (newDmg<0) newDmg=0;
        stack.setDamage(newDmg);
        return true;
    }

    // Get current damage value (0 = undamaged)
    static int getDamage(const ItemStack& s) { return s.getDamage(); }
    static void setDamage(ItemStack& s, int dmg) { s.setDamage(dmg); }
    static int maxDamage(const ItemStack& s) { return ItemStack::maxDamageFor(s.itemId); }
    static bool isBroken(const ItemStack& s) { return s.empty(); }
};

} // namespace cppfm
