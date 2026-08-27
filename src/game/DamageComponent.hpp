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
    static bool applyDamage(ItemStack& stack, int amount) {
        if (stack.empty() || amount<=0) return false;
        int maxd = ItemStack::maxDamageFor(stack.itemId);
        if (maxd<=0) return false;
        // Unbreaking logic (vanilla: for armor/tools, chance to avoid damage)
        int unb = std::max(stack.enchantLevel("unbreaking"), stack.enchantLevel("minecraft:unbreaking"));
        if (unb>0) {
            int effective = 0;
            for (int i=0;i<amount;++i) {
                // armor: (60 + 40/(lvl+1))% chance? Simplified: 1/(lvl+1) chance to take damage for tools
                // We use generic: rand % (lvl+1) ==0 -> take damage
                if (rand() % (unb + 1) == 0) effective++;
            }
            amount = effective;
            if (amount==0) return false;
        }
        return stack.applyDamage(amount);
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
