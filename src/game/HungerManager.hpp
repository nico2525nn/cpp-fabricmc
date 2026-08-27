// HungerManager: extracted hunger/saturation/exhaustion logic (plan8 modular split)
// Owns food table, exhaustion constants, and per-tick handling.
// Pure helpers are testable; GameServer delegates to this manager.
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
    // vanilla exhaustion values
    static constexpr float EXHAUST_WALK = 0.01f;
    static constexpr float EXHAUST_SPRINT = 0.10f;
    static constexpr float EXHAUST_JUMP = 0.05f;
    static constexpr float EXHAUST_SPRINT_JUMP = 0.20f;
    static constexpr float EXHAUST_ATTACK = 0.10f;
    static constexpr float EXHAUST_DAMAGE_TAKEN = 0.10f;
    static constexpr float EXHAUST_SWIM = 0.01f;
    static constexpr float EXHAUST_EAT = 0.005f;
    static constexpr float EXHAUST_BOW = 0.01f;

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

    static const std::unordered_map<std::string, FoodInfo>& foodTable();

    // Helpers for inventory: same table as InventoryController
    static bool isFoodItem(const std::string& name);
};

} // namespace cppfm
