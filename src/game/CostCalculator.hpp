// CostCalculator: enchanting and anvil cost logic (plan6 items 47,48)
#pragma once
#include <algorithm>
#include <cstdlib>
#include <string>
#include "Items.hpp"
#include "World.hpp"

namespace cppfm {
struct Player;

class CostCalculator {
public:
    // Enchanting: vanilla 1.21.4: base = rand(1,8) + floor(bs/2) + rand(0,bs)
    // clamped 1..30. Then 3 levels derived as max(base/3,1), (base*2)/3+1, max(base, bs*2)
    // bookshelf count 15 max, base range 4..17 for smoke verification
    static int enchantingCost(const Player& /*player*/, int bookshelves) {
        int bs = std::clamp(bookshelves, 0, 15);
        int base = 1 + (std::rand() % 8); // 1..8
        base += bs / 2;
        if (bs > 0) base += (std::rand() % (bs + 1)); // 0..bs
        if (base < 1) base = 1;
        if (base > 30) base = 30;
        // ensure smoke range: bs=0 => 1..8, bs=15 => 10..30 (clamped)
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
    // vanilla bookshelf counting with air gap (Yarn EnchantingTableBlock)
    static int countBookshelves(const World& w, std::int32_t bx, std::int32_t by, std::int32_t bz) {
        int count = 0;
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dy = 0; dy <= 1; ++dy) {
                    if (dx == 0 && dz == 0) continue;
                    if (std::abs(dx) == 2 && std::abs(dz) == 2) continue;
                    std::uint16_t st = w.getBlock(bx + dx, by + dy, bz + dz);
                    auto* d = gen::blockByState(st);
                    if (!d || std::string(d->name) != "minecraft:bookshelf") continue;
                    // need air between table and bookshelf
                    int mx = bx + (dx == 0 ? 0 : (dx > 0 ? 1 : -1));
                    int mz = bz + (dz == 0 ? 0 : (dz > 0 ? 1 : -1));
                    // for straight cardinal, intermediate is single block; for diagonal, need both?
                    // check intermediate air column at mx,mz and also at bx+dx, bz and bx, bz+dz for diagonal
                    bool airOk = true;
                    // primary intermediate
                    std::uint16_t mid = w.getBlock(mx, by + dy, mz);
                    if (mid != 0) {
                        auto* md = gen::blockByState(mid);
                        if (md && md->name != std::string("minecraft:air")) airOk = false;
                        else if (mid != 0) airOk = false;
                    }
                    // for diagonal (both dx and dz non-zero and one is 2), need extra check
                    if (airOk && std::abs(dx) == 2 && std::abs(dz) == 1) {
                        std::uint16_t mid2 = w.getBlock(bx + (dx>0?1:-1), by+dy, bz);
                        if (mid2 != 0) airOk = false;
                    } else if (airOk && std::abs(dz) == 2 && std::abs(dx) == 1) {
                        std::uint16_t mid2 = w.getBlock(bx, by+dy, bz + (dz>0?1:-1));
                        if (mid2 != 0) airOk = false;
                    } else if (airOk && std::abs(dx)==2 && dz==0) {
                        // already checked mx
                    } else if (airOk && std::abs(dz)==2 && dx==0) {
                        // already checked
                    }
                    if (airOk) {
                        ++count;
                        if (count >= 15) return 15;
                    }
                }
            }
        }
        if (count > 15) count = 15;
        return count;
    }

    // Anvil: repairCost = count * materialValue + renameCost ; validate name length <=50
    // Returns -1 if name too long (invalid), otherwise cost (may exceed 39 for Too Expensive)
    // Vanilla Too Expensive threshold is 40 (cost >=40 and not creative => empty result)
    static int anvilCost(const ItemStack& left, const ItemStack& right, const std::string& newName) {
        if (newName.size() > 50) return -1;
        int renameCost = newName.empty() ? 0 : 1;
        int repairCost = 0;
        if (!right.empty()) {
            int materialValue = 1;
            std::string rn = right.name();
            if (rn.find("diamond") != std::string::npos) materialValue = 2;
            else if (rn.find("netherite") != std::string::npos) materialValue = 3;
            // count based
            repairCost = static_cast<int>(right.count) * materialValue;
            if (!left.empty() && ItemStack::maxDamageFor(left.itemId) > 0) {
                repairCost += 2;
                // enchant merging cost
                if (right.hasEnchant("minecraft:protection") || right.hasEnchant("protection") ||
                    right.hasEnchant("minecraft:sharpness") || right.hasEnchant("sharpness")) {
                    repairCost += 2;
                }
            }
            // prior work penalty (simplified): if left has any enchant, add 2
            if (!left.empty() && (left.hasEnchant("minecraft:protection") || left.hasEnchant("protection"))) {
                repairCost += 1;
            }
        }
        int total = repairCost + renameCost;
        if (total == 0) {
            if (left.empty() && right.empty() && newName.empty()) return 0;
            if (!newName.empty()) total = 1;
        }
        if (total < 0) total = 0;
        // do not clamp to 39 here; caller checks >=40 for Too Expensive
        return total;
    }
};
} // namespace cppfm
