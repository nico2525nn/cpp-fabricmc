#include "HungerManager.hpp"
#include "GameServer.hpp"
#include "Items.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/BlockStates.hpp"
#include <algorithm>
#include <cmath>

namespace cppfm {

void HungerManager::addExhaustion(Player& p, float amount) {
    if (p.gamemode != 0) return;
    p.exhaustion += amount;
}

void HungerManager::addFoodAndSaturation(Player& p, int food, float sat) {
    p.food = std::clamp(p.food + food, 0, 20);
    p.saturation = std::clamp(p.saturation + sat, 0.f, static_cast<float>(p.food));
    if (p.saturation > static_cast<float>(p.food)) p.saturation = static_cast<float>(p.food);
}

const std::unordered_map<std::string, FoodInfo>& HungerManager::foodTable() {
    // plan16 strict: complete Java 1.21.4 food table (40 entries, was 26+dup)
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

bool HungerManager::isFoodItem(const std::string& name) {
    return foodTable().find(name) != foodTable().end()
        || name.find("stew") != std::string::npos
        || name.find("soup") != std::string::npos
        || name.find("cake") != std::string::npos;
}

void HungerManager::handleFoodConsume(Player& p, const std::string& itemName, GameServer& srv) {
    auto it = foodTable().find(itemName);
    if (it != foodTable().end()) {
        addFoodAndSaturation(p, it->second.food, it->second.saturation);
        if (itemName.find("stew") != std::string::npos || itemName.find("soup") != std::string::npos) {
            auto pit = gen::itemIdByName().find("minecraft:bowl");
            if (pit != gen::itemIdByName().end()) {
                srv.addToInventory(p, pit->second, 1);
                srv.resendInventory(p);
            }
        }
        if (itemName == "minecraft:honey_bottle") {
            auto pit = gen::itemIdByName().find("minecraft:glass_bottle");
            if (pit != gen::itemIdByName().end()) {
                srv.addToInventory(p, pit->second, 1);
                srv.resendInventory(p);
            }
        }
        srv.sendSetHealth(p);
        return;
    }
    if (itemName.find("stew") != std::string::npos || itemName.find("soup") != std::string::npos) {
        addFoodAndSaturation(p, 6, 7.2f);
        auto pit = gen::itemIdByName().find("minecraft:bowl");
        if (pit != gen::itemIdByName().end()) { srv.addToInventory(p, pit->second, 1); srv.resendInventory(p); }
        srv.sendSetHealth(p);
    } else if (itemName.find("cake") != std::string::npos) {
        addFoodAndSaturation(p, 2, 0.4f);
        srv.sendSetHealth(p);
    }
}

bool HungerManager::handleCakeBlockConsume(GameServer& srv, Player& p, std::int32_t x, std::int32_t y, std::int32_t z) {
    World& w = srv.worldFor(p.dimension);
    uint16_t st = w.getBlock(x, y, z);
    auto* d = gen::blockByState(st);
    if (!d || std::string(d->name) != "minecraft:cake") return false;
    if (p.food >= 20) return false;
    int bites = 0;
    for (auto& kv : gen::propsOf(st)) if (kv.first == "bites") bites = std::stoi(std::string(kv.second));
    handleFoodConsume(p, "minecraft:cake", srv);
    addExhaustion(p, EXHAUST_EAT);
    if (bites >= 6) {
        w.setBlock(x, y, z, 0);
        srv.broadcastBlockChange(x, y, z, 0);
    } else {
        uint16_t ns = static_cast<uint16_t>(gen::stateWithPropsList("minecraft:cake", {{"bites", std::to_string(bites+1)}}));
        if (ns == 0) ns = st + 1;
        w.setBlock(x, y, z, ns);
        srv.broadcastBlockChange(x, y, z, ns);
    }
    srv.sendSetHealth(p);
    return true;
}

void HungerManager::tickExhaustion(Player& p, GameServer& srv) {
    if (p.exhaustion >= 4.0) {
        p.exhaustion -= 4.0;
        if (p.saturation > 0) p.saturation = std::max(0.f, p.saturation - 1.f);
        else p.food = std::max(0, p.food - 1);
        srv.sendSetHealth(p);
    }
}

void HungerManager::tickRegenAndStarve(Player& p, int64_t tickNo, GameServer& srv) {
    (void)tickNo;
    // plan15 strict: per-player foodTickTimer + saturation fast heal (10) + naturalRegeneration gamerule + freeze 40 handled in survivalTick
    bool naturalRegeneration = true;
    if (srv.gameRules().contains("naturalRegeneration")) naturalRegeneration = srv.gameRules().getBool("naturalRegeneration");
    // difficulty for starvation thresholds
    std::string diff = "normal";
    // access via GameServer difficulty if available
    try { diff = srv.difficultyPublic(); } catch(...) { diff = "normal"; }

    // Fast healing with saturation when food==20 and saturation>0
    if (naturalRegeneration && p.saturation > 0.f && p.food >= FULL_FOOD_LEVEL && p.health > 0.f && p.health < 20.f) {
        ++p.foodTickTimer;
        if (p.foodTickTimer >= FAST_HEALING_INTERVAL) {
            p.health = std::min(20.f, p.health + 1.f);
            p.exhaustion += EXHAUSTION_PER_HEAL;
            p.saturation = std::max(0.f, p.saturation - 1.f);
            // saturation fast heal also consumes exhaustion; foodTickTimer reset
            p.foodTickTimer = 0;
            srv.sendSetHealth(p);
        }
        return;
    }
    // Slow healing when food >=18
    if (naturalRegeneration && p.food >= SLOW_HEALING_FOOD_LEVEL && p.health > 0.f && p.health < 20.f) {
        ++p.foodTickTimer;
        if (p.foodTickTimer >= SLOW_HEALING_INTERVAL) {
            p.health = std::min(20.f, p.health + 1.f);
            p.exhaustion += EXHAUSTION_PER_HEAL;
            p.foodTickTimer = 0;
            srv.sendSetHealth(p);
        }
        return;
    }
    // Starvation when food ==0
    if (p.food <= STARVING_FOOD_LEVEL) {
        ++p.foodTickTimer;
        if (p.foodTickTimer >= SLOW_HEALING_INTERVAL) {
            bool canStarve = false;
            if (diff == "hard") canStarve = p.health > 0.f;
            else if (diff == "easy") canStarve = p.health > 10.f;
            else if (diff == "normal") canStarve = p.health > 1.f;
            else if (diff == "peaceful") canStarve = false;
            else canStarve = p.health > 1.f;
            if (canStarve) {
                // starve damage bypasses armor per DamageSource starve
                p.health = std::max(0.f, p.health - 1.f);
                srv.sendSetHealth(p);
                if (p.health <= 0.f) srv.killPlayer(p, "starve");
            }
            p.foodTickTimer = 0;
        }
        return;
    }
    // otherwise reset timer
    p.foodTickTimer = 0;
}

void HungerManager::onPlayerMove(Player& p, double oldX, double /*oldY*/, double oldZ,
                                 double newX, double /*newY*/, double newZ,
                                 bool wasOnGround, bool nowOnGround,
                                 bool isSprinting, bool isSwimming, GameServer& srv) {
    (void)wasOnGround; (void)nowOnGround; (void)srv;
    if (p.gamemode != 0) return;
    double hdx = newX - oldX, hdz = newZ - oldZ;
    double hDist = std::sqrt(hdx*hdx + hdz*hdz);
    if (hDist > 0.001) {
        float mult = isSwimming ? EXHAUST_SWIM : (isSprinting ? EXHAUST_SPRINT : EXHAUST_WALK);
        p.exhaustion += static_cast<float>(hDist) * mult;
    }
}

void HungerManager::onPlayerJump(Player& p, bool wasOnGround, bool nowOnGround, double dy, bool isSprinting, GameServer& srv) {
    if (p.gamemode != 0) return;
    if (wasOnGround && !nowOnGround && dy > 0.05) {
        float cost = isSprinting ? EXHAUST_SPRINT_JUMP : EXHAUST_JUMP;
        addExhaustion(p, cost);
        (void)srv;
    }
}

void HungerManager::onPlayerAttack(Player& p, GameServer& srv) {
    addExhaustion(p, EXHAUST_ATTACK);
    (void)srv;
}

void HungerManager::onBowUse(Player& p, GameServer& srv) {
    addExhaustion(p, EXHAUST_BOW);
    (void)srv;
}

} // namespace cppfm
