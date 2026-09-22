#pragma once
#include <cstdint>
#include <random>
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
    // armor 0.6+0.4/(lvl+1) ignore, tools 1/(lvl+1) damage.
    static bool applyDamage(ItemStack& stack, int amount) {
        if (stack.empty() || amount<=0) return false;
        int maxd = ItemStack::maxDamageFor(stack.itemId);
        if (maxd<=0) return false;
        int unb = stack.unbreakingLevel();
        if (unb>0) {
            bool armor = isArmorItem(stack);
            int effective = 0;
            thread_local std::mt19937 rng{std::random_device{}()};
            std::uniform_real_distribution<float> dist(0.f, 1.f);
            std::uniform_int_distribution<int> intDist(0, unb);
            for (int i=0;i<amount;++i) {
                if (armor) {
                    float ignoreChance = 0.6f + 0.4f / float(unb + 1);
                    float r = dist(rng);
                    if (r >= ignoreChance) effective++;
                } else {
                    if (intDist(rng) == 0) effective++;
                }
            }
            amount = effective;
            if (amount==0) return false;
        }
        return stack.applyDamage(amount);
    }
};

} // namespace cppfm
