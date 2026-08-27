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
    // Enchanting: base = rand(4,17) + bookshelves*2 (vanilla formula simplified)
    static int enchantingCost(const Player& /*player*/, int bookshelves) {
        int bs = std::clamp(bookshelves, 0, 15);
        int base = 4 + (std::rand() % 14); // 4..17 inclusive
        base += bs * 2;
        if (base < 1) base = 1;
        if (base > 30) base = 30;
        return base;
    }

    // Anvil: repairCost = count * materialValue + renameCost ; validate name length <=50
    // Returns -1 if name too long (invalid), otherwise cost 0..39 clamped.
    static int anvilCost(const ItemStack& left, const ItemStack& right, const std::string& newName) {
        if (newName.size() > 50) return -1;
        int renameCost = newName.empty() ? 0 : 1;
        // If newName equals existing custom name, no rename cost? Simplified: still 1 if non-empty and different.
        // We treat any non-empty newName as 1.
        int repairCost = 0;
        if (!right.empty()) {
            int materialValue = 1;
            // slightly higher value for diamond/netherite
            std::string rn = right.name();
            if (rn.find("diamond") != std::string::npos) materialValue = 2;
            else if (rn.find("netherite") != std::string::npos) materialValue = 3;
            else if (rn.find("iron") != std::string::npos) materialValue = 1;
            else if (rn.find("gold") != std::string::npos) materialValue = 1;
            repairCost = static_cast<int>(right.count) * materialValue;
            // extra base for damageable items
            if (!left.empty() && ItemStack::maxDamageFor(left.itemId) > 0) {
                repairCost += 2;
            }
        }
        int total = repairCost + renameCost;
        if (total == 0) {
            // if only rename? already counted. If nothing, cost 0 means no operation.
            // anvil with just rename of empty? return 0.
            if (left.empty() && right.empty() && newName.empty()) return 0;
            if (!newName.empty()) total = 1;
        }
        if (total > 39) total = 39;
        if (total < 0) total = 0;
        return total;
    }
};
} // namespace cppfm
