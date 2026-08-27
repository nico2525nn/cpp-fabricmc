// InventoryController: extracted inventory / container / recipe / hunger (plan7)
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <algorithm>
#include "Items.hpp"
#include "Containers.hpp"

namespace cppfm {
struct Player;

class InventoryController {
public:
    // hunger exhaustion constants (vanilla)
    static constexpr float EXHAUST_WALK = 0.01f;
    static constexpr float EXHAUST_SPRINT = 0.1f;
    static constexpr float EXHAUST_JUMP = 0.05f;
    static constexpr float EXHAUST_SPRINT_JUMP = 0.2f;
    static constexpr float EXHAUST_ATTACK = 0.1f;
    static constexpr float EXHAUST_DAMAGE = 0.1f;
    static constexpr float EXHAUST_SWIM = 0.01f;

    static void addExhaustion(Player& p, float amount) { (void)p; (void)amount; }
    static void addFoodAndSaturation(Player& p, int food, float sat) { (void)p; (void)food; (void)sat; }
    static void handleFoodConsume(Player& p, const std::string& itemName) { (void)p; (void)itemName; }
    static bool handleCakeConsume(Player& p, class World& world, std::int32_t x, std::int32_t y, std::int32_t z) { (void)p; (void)world; (void)x; (void)y; (void)z; return false; }

    // inventory merge helpers
    static bool addToInventory(Player& p, std::uint32_t itemId, std::uint16_t count) { (void)p; (void)itemId; (void)count; return false; }
    static void resendInventory(Player& p) { (void)p; }

    // food table (shared with GameServer::handleFoodConsume)
    struct FoodInfo { int food; float saturation; };
    static const std::unordered_map<std::string, FoodInfo>& foodTable() {
        static const std::unordered_map<std::string, FoodInfo> k = {
            {"minecraft:apple",{4,2.4f}}, {"minecraft:bread",{5,6.0f}},
            {"minecraft:cake",{2,0.4f}}, {"minecraft:cookie",{2,0.4f}},
            {"minecraft:mushroom_stew",{6,7.2f}}, {"minecraft:beetroot_soup",{6,7.2f}},
            {"minecraft:cooked_beef",{8,12.8f}}, {"minecraft:cooked_chicken",{6,7.2f}},
            {"minecraft:cooked_porkchop",{8,12.8f}}, {"minecraft:cooked_mutton",{6,9.6f}},
            {"minecraft:baked_potato",{5,6.0f}}, {"minecraft:carrot",{3,3.6f}},
            {"minecraft:melon_slice",{2,1.2f}}, {"minecraft:golden_apple",{4,9.6f}},
            {"minecraft:golden_carrot",{6,14.4f}}, {"minecraft:steak",{8,12.8f}},
            {"minecraft:pumpkin_pie",{8,4.8f}}, {"minecraft:beetroot",{1,1.2f}},
            {"minecraft:dried_kelp",{1,0.6f}}, {"minecraft:sweet_berries",{2,0.4f}},
            {"minecraft:glow_berries",{2,0.4f}}, {"minecraft:honey_bottle",{6,1.2f}},
            {"minecraft:rabbit_stew",{10,12.0f}}, {"minecraft:suspicious_stew",{6,7.2f}},
        };
        return k;
    }
};
} // namespace cppfm
