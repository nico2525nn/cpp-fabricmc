#include "HungerManager.hpp"
#include "GameServer.hpp"
#include "Items.hpp"
#include "../generated/ItemIds.hpp"
#include "../generated/BlockStates.hpp"
#include <algorithm>
#include <cctype>
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

// (plan44 G-01: foodTable()/isFoodItem moved inline to HungerManager.hpp; bodies removed here.)

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
    // plan23 §7 strict: per-player foodTickTimer + fast heal 10t (food 20 + saturation>0) + slow heal 80t (food>=18) + starve difficulty + naturalRegeneration gate
    bool naturalRegeneration = true;
    if (srv.gameRules().contains("naturalRegeneration")) naturalRegeneration = srv.gameRules().getBool("naturalRegeneration");
    std::string diff = "normal";
    try { diff = srv.difficultyPublic(); } catch(...) { diff = "normal"; }
    std::transform(diff.begin(), diff.end(), diff.begin(), [](unsigned char c){ return std::tolower(c); });

    // Fast healing with saturation when food==20 and saturation>0 — Yarn HungerManager: heal 1 per 10t at cost 6 exhaustion
    if (naturalRegeneration && p.saturation > 0.f && p.food >= FULL_FOOD_LEVEL && p.health > 0.f && p.health < 20.f) {
        ++p.foodTickTimer;
        if (p.foodTickTimer >= FAST_HEALING_INTERVAL) {
            p.health = std::min(20.f, p.health + 1.f);
            p.exhaustion += EXHAUSTION_PER_HEAL;
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
    // Starvation when food ==0 — difficulty gates: PEACEFUL 0, EASY health>10, NORMAL health>1, HARD health>0 per Yarn
    if (p.food <= STARVING_FOOD_LEVEL) {
        ++p.foodTickTimer;
        if (p.foodTickTimer >= SLOW_HEALING_INTERVAL) {
            // plan44 G-01: gate extracted to pure canStarveForDifficulty (behavior identical)
            bool canStarve = canStarveForDifficulty(diff, p.health);
            if (canStarve) {
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
    if (EXHAUST_BOW != 0.f) addExhaustion(p, EXHAUST_BOW);
    (void)srv;
}

void HungerManager::onBlockBreak(Player& p, GameServer& srv) {
    addExhaustion(p, EXHAUST_BLOCK_BREAK);
    (void)srv;
}

void HungerManager::onDamageTaken(Player& p, GameServer& srv) {
    addExhaustion(p, EXHAUST_DAMAGE_TAKEN);
    (void)srv;
}

} // namespace cppfm
