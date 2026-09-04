#include "InventoryController.hpp"
#include "MenuInteraction.hpp" // maxStackForId single truth
#include "GameServer.hpp"
#include "HungerManager.hpp"
#include <unordered_set>

namespace cppfm {

namespace {
inline bool sameStackCanMerge(const ItemStack& a, const ItemStack& b) {
    return !a.empty() && !b.empty() && a.itemId == b.itemId &&
           a.components == b.components && a.removedComponents == b.removedComponents;
}
} // namespace

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
    return HungerManager::handleCakeBlockConsume(srv, p, x, y, z);
}

bool InventoryController::addToInventory(Player& p, std::uint32_t itemId, std::uint16_t count) {
    // Polish: respect per-item max stack (16/1/64) and component equality — vanilla stacking requires identical components
    const int maxStack = maxStackForId(itemId);
    ItemStack probe; probe.itemId = itemId; probe.count = 1;
    // First pass: merge into existing stacks with same item + components and space
    for (int i = 0; i < 46; ++i) {
        if (!p.inv[i].empty() && sameStackCanMerge(p.inv[i], probe) && p.inv[i].count < maxStack) {
            int space = maxStack - p.inv[i].count;
            int add = std::min<int>(space, count);
            p.inv[i].count = static_cast<std::int16_t>(p.inv[i].count + add);
            count = static_cast<std::uint16_t>(count - add);
            if (count == 0) return true;
        }
    }
    for (int i = 0; i < 46; ++i) {
        if (p.inv[i].empty()) {
            int take = std::min<int>(count, maxStack);
            p.inv[i].itemId = itemId;
            p.inv[i].count = static_cast<std::int16_t>(take);
            // empty components for new stack (air components)
            p.inv[i].components.clear();
            p.inv[i].removedComponents.clear();
            count = static_cast<std::uint16_t>(count - take);
            if (count == 0) return true;
        }
    }
    return count == 0;
}

void InventoryController::resendInventory(Player& p) {
    if (!p.conn || !p.inPlay) return;
    // send full inventory content via ContainerSetContent for player inventory (window 0) This is a stub that would normally be
    // GameServer::resendInventory; we keep minimal GameServer will handle actual packet; this keeps modular split visible.
}

} // namespace cppfm
