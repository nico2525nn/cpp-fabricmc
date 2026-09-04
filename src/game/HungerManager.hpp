// FAST/SLOW, starve gates, food table).
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace cppfm {
struct Player;
class World;
class GameServer;

struct FoodInfo { int food; float saturation; };

class HungerManager {
public:
    static constexpr float EXHAUST_WALK = 0.0f;
    static constexpr float EXHAUST_SPRINT = 0.10f;
    static constexpr float EXHAUST_JUMP = 0.05f;
    static constexpr float EXHAUST_SPRINT_JUMP = 0.20f;
    static constexpr float EXHAUST_ATTACK = 0.10f;
    static constexpr float EXHAUST_DAMAGE_TAKEN = 0.10f;
    static constexpr float EXHAUST_SWIM = 0.01f;
    static constexpr float EXHAUST_EAT = 0.005f;
    static constexpr float EXHAUST_BLOCK_BREAK = 0.005f;
    static constexpr float EXHAUST_BOW = 0.0f; // vanilla bow has no exhaustion (was 0.01)
    static constexpr int FAST_HEALING_INTERVAL = 10;
    static constexpr int SLOW_HEALING_INTERVAL = 80;
    static constexpr int SLOW_HEALING_FOOD_LEVEL = 18;
    static constexpr int FULL_FOOD_LEVEL = 20;
    static constexpr int STARVING_FOOD_LEVEL = 0;
    static constexpr float EXHAUSTION_PER_HEAL = 6.0f;

    static inline bool canStarveForDifficulty(const std::string& diff, float health) {
        if (diff == "hard") return health > 0.f;
        if (diff == "easy") return health > 10.f;
        if (diff == "normal") return health > 1.f;
        if (diff == "peaceful") return false;
        return health > 1.f;
    }

    // Exhaustion -> saturation/food conversion and regen/starve
    static void addExhaustion(Player& p, float amount);
    static void addFoodAndSaturation(Player& p, int food, float sat);
    static void handleFoodConsume(Player& p, const std::string& itemName, GameServer& srv);
    static bool handleCakeBlockConsume(GameServer& srv, Player& p, std::int32_t x, std::int32_t y, std::int32_t z);

    // Called from GameServer::survivalTick per player
    static void tickExhaustion(Player& p, GameServer& srv);
    static void tickRegenAndStarve(Player& p, int64_t tickNo, GameServer& srv);

    // Movement-derived exhaustion
    static void onPlayerMove(Player& p, double oldX, double oldY, double oldZ,
                             double newX, double newY, double newZ,
                             bool wasOnGround, bool nowOnGround,
                             bool isSprinting, bool isSwimming, GameServer& srv);
    static void onPlayerJump(Player& p, bool wasOnGround, bool nowOnGround, double dy, bool isSprinting, GameServer& srv);
    static void onPlayerAttack(Player& p, GameServer& srv);
    static void onBowUse(Player& p, GameServer& srv);
    static void onBlockBreak(Player& p, GameServer& srv);
    static void onDamageTaken(Player& p, GameServer& srv);

    static inline const std::unordered_map<std::string, FoodInfo>& foodTable() {
        static const std::unordered_map<std::string, FoodInfo> k = {
            {"minecraft:apple", {4, 2.4f}},
            {"minecraft:baked_potato", {5, 6.0f}},
            {"minecraft:beetroot", {1, 1.2f}},
            {"minecraft:beetroot_soup", {6, 7.2f}},
            {"minecraft:bread", {5, 6.0f}},
            {"minecraft:cake", {2, 0.4f}},
            {"minecraft:carrot", {3, 3.6f}},
            {"minecraft:chorus_fruit", {4, 2.4f}},
            {"minecraft:cooked_beef", {8, 12.8f}},
            {"minecraft:steak", {8, 12.8f}},
            {"minecraft:cooked_chicken", {6, 7.2f}},
            {"minecraft:cooked_cod", {5, 6.0f}},
            {"minecraft:cooked_mutton", {6, 9.6f}},
            {"minecraft:cooked_porkchop", {8, 12.8f}},
            {"minecraft:cooked_rabbit", {5, 6.0f}},
            {"minecraft:cooked_salmon", {6, 9.6f}},
            {"minecraft:cookie", {2, 0.4f}},
            {"minecraft:dried_kelp", {1, 0.6f}},
            {"minecraft:enchanted_golden_apple", {4, 9.6f}},
            {"minecraft:golden_apple", {4, 9.6f}},
            {"minecraft:golden_carrot", {6, 14.4f}},
            {"minecraft:glow_berries", {2, 0.4f}},
            {"minecraft:honey_bottle", {6, 1.2f}},
            {"minecraft:melon_slice", {2, 1.2f}},
            {"minecraft:mushroom_stew", {6, 7.2f}},
            {"minecraft:poisonous_potato", {2, 1.2f}},
            {"minecraft:potato", {1, 0.6f}},
            {"minecraft:pumpkin_pie", {8, 4.8f}},
            {"minecraft:rabbit_stew", {10, 12.0f}},
            {"minecraft:suspicious_stew", {6, 7.2f}},
            {"minecraft:beef", {3, 1.8f}},
            {"minecraft:chicken", {2, 1.2f}},
            {"minecraft:porkchop", {3, 1.8f}},
            {"minecraft:mutton", {2, 1.2f}},
            {"minecraft:rabbit", {3, 1.8f}},
            {"minecraft:cod", {2, 0.4f}},
            {"minecraft:salmon", {2, 0.4f}},
            {"minecraft:rotten_flesh", {4, 0.8f}},
            {"minecraft:spider_eye", {2, 3.2f}},
            {"minecraft:tropical_fish", {1, 0.2f}},
            {"minecraft:pufferfish", {1, 0.2f}},
            {"minecraft:sweet_berries", {2, 0.4f}},
        };
        return k;
    }

    // Helpers for inventory: same table as InventoryController (inline for light unit tests)
    static inline bool isFoodItem(const std::string& name) {
        return foodTable().find(name) != foodTable().end()
            || name.find("stew") != std::string::npos
            || name.find("soup") != std::string::npos
            || name.find("cake") != std::string::npos;
    }
};

} // namespace cppfm
