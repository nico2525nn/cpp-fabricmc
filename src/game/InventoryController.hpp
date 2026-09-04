// Delegates hunger to HungerManager; inventory helpers mirror GameServer logic but are testable.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <algorithm>
#include "Items.hpp"
#include "Containers.hpp"
#include "HungerManager.hpp"

namespace cppfm {
struct Player;
class World;
class GameServer;

class InventoryController {
public:
    // hunger exhaustion constants (vanilla) — mirrored from HungerManager for compat
    static constexpr float EXHAUST_WALK = HungerManager::EXHAUST_WALK;
    static constexpr float EXHAUST_SPRINT = HungerManager::EXHAUST_SPRINT;
    static constexpr float EXHAUST_JUMP = HungerManager::EXHAUST_JUMP;
    static constexpr float EXHAUST_SPRINT_JUMP = HungerManager::EXHAUST_SPRINT_JUMP;
    static constexpr float EXHAUST_ATTACK = HungerManager::EXHAUST_ATTACK;
    static constexpr float EXHAUST_DAMAGE = HungerManager::EXHAUST_DAMAGE_TAKEN;
    static constexpr float EXHAUST_SWIM = HungerManager::EXHAUST_SWIM;

    static void addExhaustion(Player& p, float amount);
    static void addFoodAndSaturation(Player& p, int food, float sat);
    static void handleFoodConsume(Player& p, const std::string& itemName, GameServer& srv);
    static bool handleCakeConsume(GameServer& srv, Player& p, World& world, std::int32_t x, std::int32_t y, std::int32_t z);

    // inventory merge helpers
    static bool addToInventory(Player& p, std::uint32_t itemId, std::uint16_t count);
    static void resendInventory(Player& p);

    struct FoodInfo { int food; float saturation; };
    static const std::unordered_map<std::string, FoodInfo>& foodTable() {
        static const std::unordered_map<std::string, FoodInfo> k = {
            {"minecraft:apple",{4,2.4f}}, {"minecraft:baked_potato",{5,6.0f}}, {"minecraft:beetroot",{1,1.2f}},
            {"minecraft:beetroot_soup",{6,7.2f}}, {"minecraft:bread",{5,6.0f}}, {"minecraft:cake",{2,0.4f}},
            {"minecraft:carrot",{3,3.6f}}, {"minecraft:chorus_fruit",{4,2.4f}}, {"minecraft:cooked_beef",{8,12.8f}},
            {"minecraft:steak",{8,12.8f}}, {"minecraft:cooked_chicken",{6,7.2f}}, {"minecraft:cooked_cod",{5,6.0f}},
            {"minecraft:cooked_mutton",{6,9.6f}}, {"minecraft:cooked_porkchop",{8,12.8f}}, {"minecraft:cooked_rabbit",{5,6.0f}},
            {"minecraft:cooked_salmon",{6,9.6f}}, {"minecraft:cookie",{2,0.4f}}, {"minecraft:dried_kelp",{1,0.6f}},
            {"minecraft:enchanted_golden_apple",{4,9.6f}}, {"minecraft:golden_apple",{4,9.6f}}, {"minecraft:golden_carrot",{6,14.4f}},
            {"minecraft:glow_berries",{2,0.4f}}, {"minecraft:honey_bottle",{6,1.2f}}, {"minecraft:melon_slice",{2,1.2f}},
            {"minecraft:mushroom_stew",{6,7.2f}}, {"minecraft:poisonous_potato",{2,1.2f}}, {"minecraft:potato",{1,0.6f}},
            {"minecraft:pumpkin_pie",{8,4.8f}}, {"minecraft:rabbit_stew",{10,12.0f}}, {"minecraft:suspicious_stew",{6,7.2f}},
            {"minecraft:beef",{3,1.8f}}, {"minecraft:chicken",{2,1.2f}}, {"minecraft:porkchop",{3,1.8f}},
            {"minecraft:mutton",{2,1.2f}}, {"minecraft:rabbit",{3,1.8f}}, {"minecraft:cod",{2,0.4f}},
            {"minecraft:salmon",{2,0.4f}}, {"minecraft:rotten_flesh",{4,0.8f}}, {"minecraft:spider_eye",{2,3.2f}},
            {"minecraft:tropical_fish",{1,0.2f}}, {"minecraft:pufferfish",{1,0.2f}}, {"minecraft:sweet_berries",{2,0.4f}},
        };
        return k;
    }
};
} // namespace cppfm
