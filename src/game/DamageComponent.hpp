// DamageComponent — plan8 entity section
// Handles item durability (minecraft:damage component) with Unbreaking and Mending.
// Vanilla logic: tools 1/(lvl+1) chance to damage, armor 60%+40%/(lvl+1) ignore per Yarn EnchantmentHelper.shouldDamage.
#pragma once
#include <cstdint>
#include <cstdlib>
#include "Items.hpp"

namespace cppfm {

class DamageComponent {
public:
    static bool isArmorItem(const ItemStack& s) {
        std::string n = s.name();
        return n.find("_helmet")!=std::string::npos || n.find("_chestplate")!=std::string::npos
            || n.find("_leggings")!=std::string::npos || n.find("_boots")!=std::string::npos
            || n.find("turtle_helmet")!=std::string::npos || n.find("elytra")!=std::string::npos
            || n.find("horse_armor")!=std::string::npos;
    }
    // Apply damage to a stack, respecting Unbreaking enchantment.
    // Returns true if the stack was destroyed (damage >= max).
    // Vanilla Yarn: armor 0.6+0.4/(lvl+1) ignore, tools 1/(lvl+1) damage.
    static bool applyDamage(ItemStack& stack, int amount) {
        if (stack.empty() || amount<=0) return false;
        int maxd = ItemStack::maxDamageFor(stack.itemId);
        if (maxd<=0) return false;
        int unb = stack.unbreakingLevel();
        if (unb>0) {
            bool armor = isArmorItem(stack);
            int effective = 0;
            for (int i=0;i<amount;++i) {
                if (armor) {
                    float ignoreChance = 0.6f + 0.4f / float(unb + 1);
                    float r = float(rand()) / float(RAND_MAX);
                    if (r >= ignoreChance) effective++;
                } else {
                    if (rand() % (unb + 1) == 0) effective++;
                }
            }
            amount = effective;
            if (amount==0) return false;
        }
        return stack.applyDamage(amount);
    }
    static bool shouldDamage(const ItemStack& stack) {
        int unb = stack.unbreakingLevel();
        if (unb<=0) return true;
        if (isArmorItem(stack)) {
            float ignoreChance = 0.6f + 0.4f / float(unb + 1);
            float r = float(rand()) / float(RAND_MAX);
            return r >= ignoreChance;
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
