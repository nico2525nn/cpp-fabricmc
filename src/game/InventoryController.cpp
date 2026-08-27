#include "InventoryController.hpp"
#include "GameServer.hpp"
#include "HungerManager.hpp"

namespace cppfm {

void InventoryController::addExhaustion(Player& p, float amount) {
    HungerManager::addExhaustion(p, amount);
}

void InventoryController::addFoodAndSaturation(Player& p, int food, float sat) {
    HungerManager::addFoodAndSaturation(p, food, sat);
}

void InventoryController::handleFoodConsume(Player& p, const std::string& itemName, GameServer& srv) {
    HungerManager::handleFoodConsume(p, itemName, srv);
}

bool InventoryController::handleCakeConsume(GameServer& srv, Player& p, World& world, std::int32_t x, std::int32_t y, std::int32_t z) {
    (void)world;
    return HungerManager::handleCakeBlockConsume(srv, p, x, y, z);
}

bool InventoryController::addToInventory(Player& p, std::uint32_t itemId, std::uint16_t count) {
    // delegate to GameServer's logic via temporary server ref? For now direct implementation
    // Find first partial stack, else empty slot
    for (int i = 0; i < 46; ++i) {
        if (!p.inv[i].empty() && p.inv[i].itemId == itemId && p.inv[i].count < 64) {
            int space = 64 - p.inv[i].count;
            int add = std::min<int>(space, count);
            p.inv[i].count += add;
            count -= add;
            if (count == 0) return true;
        }
    }
    for (int i = 0; i < 46; ++i) {
        if (p.inv[i].empty()) {
            p.inv[i].itemId = itemId;
            p.inv[i].count = std::min<uint16_t>(count, 64);
            count -= p.inv[i].count;
            if (count == 0) return true;
        }
    }
    return count == 0;
}

void InventoryController::resendInventory(Player& p) {
    if (!p.conn || !p.inPlay) return;
    // send full inventory content via ContainerSetContent for player inventory (window 0)
    // This is a stub that would normally be GameServer::resendInventory; we keep minimal
    // GameServer will handle actual packet; this keeps modular split visible.
}

} // namespace cppfm
