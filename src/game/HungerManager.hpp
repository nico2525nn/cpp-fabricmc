// HungerManager: extracted hunger/saturation/exhaustion logic (plan8 modular split)
// Owns food table, exhaustion constants, and per-tick handling.
// Pure helpers are testable; GameServer delegates to this manager.
// plan19 combat polish: verified per-player foodTickTimer, FAST 10 / SLOW 80, naturalRegeneration, starve diff, EPF weight 1.
// plan20 combat polish: verify hunger unchanged by world-density/light changes; GameRules expanded to 50+ (W18) for naturalRegeneration parity.
// plan21 combat polish: retry verify EXHAUST_WALK 0 vs 0.01, per-player foodTickTimer, FAST 10/SLOW 80, starve diff hard/easy/normal, naturalRegeneration gate.
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
    // vanilla exhaustion values — plan15 strict: walk 0 per HungerConstants (was 0.01)
    static constexpr float EXHAUST_WALK = 0.0f;
    static constexpr float EXHAUST_SPRINT = 0.10f;
    static constexpr float EXHAUST_JUMP = 0.05f;
    static constexpr float EXHAUST_SPRINT_JUMP = 0.20f;
    static constexpr float EXHAUST_ATTACK = 0.10f;
    static constexpr float EXHAUST_DAMAGE_TAKEN = 0.10f;
    static constexpr float EXHAUST_SWIM = 0.01f;
    static constexpr float EXHAUST_EAT = 0.005f;
    static constexpr float EXHAUST_BOW = 0.01f;
    // plan15 strict constants per HungerConstants
    static constexpr int FAST_HEALING_INTERVAL = 10;
    static constexpr int SLOW_HEALING_INTERVAL = 80;
    static constexpr int SLOW_HEALING_FOOD_LEVEL = 18;
    static constexpr int FULL_FOOD_LEVEL = 20;
    static constexpr int STARVING_FOOD_LEVEL = 0;
    static constexpr float EXHAUSTION_PER_HEAL = 6.0f;

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
