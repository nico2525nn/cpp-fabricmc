// CostCalculator: enchanting and anvil cost logic (plan6 items 47,48)
#pragma once
#include <algorithm>
#include <cstdlib>
#include <string>
#include "Items.hpp"

namespace cppfm {
struct Player;

class CostCalculator {
public:
    // Enchanting: vanilla 1.21.4: base = rand(1,8) + floor(bs/2) + rand(0,bs)
    // clamped 1..30. Then 3 levels derived as max(base/3,1), (base*2)/3+1, max(base, bs*2)
    static int enchantingCost(const Player& /*player*/, int bookshelves) {
        int bs = std::clamp(bookshelves, 0, 15);
        int base = 1 + (std::rand() % 8); // 1..8
        base += bs / 2;
        if (bs > 0) base += (std::rand() % (bs + 1)); // 0..bs
        if (base < 1) base = 1;
        if (base > 30) base = 30;
        return base;
    }
    static std::array<int,3> enchantingCostsForShelves(const Player& p, int bookshelves) {
        int bs = std::clamp(bookshelves, 0, 15);
        int base = enchantingCost(p, bs);
        int c0 = std::max(base / 3, 1);
        int c1 = (base * 2) / 3 + 1;
        int c2 = std::max(base, bs * 2);
        c0 = std::clamp(c0, 1, 30);
        c1 = std::clamp(c1, 1, 30);
        c2 = std::clamp(c2, 1, 30);
        return {c0, c1, c2};
    }

    // Anvil: plan13 §4 vanilla accurate: prior work penalty + enchant cost + rename, Too Expensive 39 limit
    // Returns -1 if name too long (invalid), otherwise returns total cost (may be >=40 for Too Expensive).
    // Callers must check >=40 to block.
    static int anvilCost(const ItemStack& left, const ItemStack& right, const std::string& newName) {
        if (newName.size() > 50) return -1;
        if (left.empty()) return 0;
        int prior = left.getRepairCost() + right.getRepairCost();
        // prior is 0,1,3,7,15,31... (2^n-1)
        int renameCost = 0;
        if (!newName.empty()) {
            std::string curName = left.getCustomName();
            if (newName != curName) renameCost = 1;
        }
        int enchantCost = 0;
        if (!right.empty()) {
            // if right is enchanted book or same item type, sum levels with weight
            // Simplified: sum of all enchant levels on right (weight 1 for common, 2 for rare)
            // Use textual payload parsing
            for (auto &pr: right.components) if(pr.first==10||pr.first==21){
                std::string txt(pr.second.begin(), pr.second.end());
                // count ',' as enchant entries
                size_t pos=0;
                while((pos=txt.find(':', pos)) != std::string::npos){
                    size_t comma = txt.find(',', pos);
                    std::string lvlStr = txt.substr(pos+1, (comma==std::string::npos? txt.size():comma)-pos-1);
                    try{
                        int lvl = std::stoi(lvlStr);
                        // weight: protection/sharpness etc weight 1, mending/soul etc weight 2? simplified 1
                        enchantCost += lvl;
                    }catch(...){ enchantCost += 1; }
                    if(comma==std::string::npos) break;
                    pos = comma+1;
                }
                break;
            }
            // if no enchants but same item repair, add base 2 for durability repair
            if (enchantCost==0 && ItemStack::maxDamageFor(left.itemId)>0 && right.itemId==left.itemId) {
                enchantCost = 2;
            } else if (enchantCost==0 && !right.empty()) {
                // material repair value
                int matVal = 1;
                std::string rn = right.name();
                if (rn.find("diamond") != std::string::npos) matVal = 2;
                else if (rn.find("netherite") != std::string::npos) matVal = 3;
                enchantCost = static_cast<int>(right.count) * matVal;
                if (enchantCost==0) enchantCost = 1;
            }
        }
        int total = prior + enchantCost + renameCost;
        if (total==0 && !newName.empty()) total = 1;
        if (total==0 && right.empty() && newName.empty()) return 0;
        // don't clamp here; caller handles Too Expensive 39 limit (property 0)
        if (total < 0) total = 0;
        return total;
    }
    static int nextRepairCost(const ItemStack& left, const ItemStack& right){
        int cur = std::max(left.getRepairCost(), right.getRepairCost());
        if(cur==0) return 1;
        return cur*2+1; // 0->1,1->3,3->7,7->15,15->31,31->63
    }
    static bool isTooExpensive(int cost, bool isCreative){
        if(isCreative) return false;
        return cost >= 40;
    }
};
} // namespace cppfm
