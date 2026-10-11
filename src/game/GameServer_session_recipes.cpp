#include "GameServer.hpp"
#include "Session.hpp"
#include "GameServerHelpers.hpp"
#include "MenuInteraction.hpp"
#include "RecipePlacement.hpp"
#include "RecipeBook.hpp"
#include "SessionMenuSupport.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace cppfm {
using namespace proto;
using session_menu_detail::insertCompleteInventoryStack;

void Session::sendRecipeBook() {
    // settings: 8 booleans (gui open / filtering per station)
    {
        WriteBuffer b;
        for (int i = 0; i < 8; ++i) b.boolean(false);
        conn_->sendPacket(pl::sc::RecipeBookSettings, b);
    }
    const auto body = recipe_book::makeAddPacketBody(
        srv_.recipes().all(), nullptr, true);
    conn_->trySendPacket(pl::sc::RecipeBookAdd, body);
}
void Session::handlePlaceRecipe(std::int32_t recipeId, bool makeAll) {
    std::shared_ptr<Menu> menu;
    std::shared_ptr<BlockEntity> menuOwner;
    MenuType menuType = MenuType::Chest;
    std::int8_t dimension = 0;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!openMenu_) return;
        menu = openMenu_;
        menuOwner = menu->blockEntityOwner;
        menuType = menu->type;
        dimension = self_->dimension;
    }
    const auto& all = srv_.recipes().all();
    const auto recipeEntry = recipe_book::entryForDisplayId(all, recipeId);
    if (!recipeEntry) return;
    const Recipe selectedRecipe = recipe_book::recipeForDisplay(
        all[recipeEntry->recipeIndex], recipeEntry->displayIndex);
    const Recipe& r = selectedRecipe;

    // A recipe placement is one inventory/menu transaction.  In particular,
    // do not empty an existing grid and then call addToInventory(): a full
    // inventory used to make that path silently delete the old inputs.
    // Recipe placement mutates live furnace/stonecutter block entities. Keep
    // the same World -> block entity -> player lock order used by ordinary
    // menu clicks so persistence admission cannot race world shutdown.
    std::optional<MutationGateLease> mutationGate;
    if (menu->blockKey &&
        (menuType == MenuType::Furnace ||
         menuType == MenuType::BlastFurnace ||
         menuType == MenuType::Smoker ||
         menuType == MenuType::Stonecutter))
        mutationGate.emplace(srv_.worldFor(dimension).lockMutationGate());
    std::unique_lock<std::recursive_mutex> entityLock;
    if (menuOwner && menuOwner->stateMtx)
        entityLock = std::unique_lock<std::recursive_mutex>(*menuOwner->stateMtx);
    std::unique_lock playerLock(self_->stateMtx);
    if (openMenu_.get() != menu.get() || menu->type != menuType) return;
    Menu& m = *menu;
    const auto admitBlockEntityMutation = [&](const char* operation) {
        if (!m.blockKey) return true;
        try {
            return srv_.blockEntitiesFor(dimension).tryMarkDirty(*m.blockKey);
        } catch (const std::exception& error) {
            std::fprintf(stderr,
                         "[cppfm] %s dirty admission failed: %s\n",
                         operation, error.what());
        } catch (...) {
            std::fprintf(stderr, "[cppfm] %s dirty admission failed\n",
                         operation);
        }
        return false;
    };

    if (m.type == MenuType::Crafting) {
        std::array<ItemStack, 9> currentGrid;
        for (std::size_t i = 0; i < currentGrid.size(); ++i)
            currentGrid[i] = m.craftGrid[i];
        auto placement = planCraftingRecipePlacement(
            r, m.playerInventory, makeAll, self_->inv, currentGrid);
        if (!placement) return;
        self_->inv = std::move(placement->inventory);
        for (std::size_t i = 0; i < placement->grid.size(); ++i)
            m.craftGrid[i] = std::move(placement->grid[i]);
        m.refreshCraftResult(srv_.recipes());
        playerLock.unlock();
        if (entityLock.owns_lock()) entityLock.unlock();
        srv_.resendInventory(*self_);
        sendMenuContent(m);
        syncCursorItem();
        return;
    } else if (m.type == MenuType::Furnace ||
               m.type == MenuType::BlastFurnace ||
               m.type == MenuType::Smoker) {
        const auto station = cookingStationForMenu(m.type);
        if (!station || r.kind != Recipe::Kind::Smelting ||
            r.cookingStation != *station)
            return;
        if (!m.container || m.containerCount < 2 || r.cells.empty()) return;
        // Place ingredient into input slot 0, and if needed fuel into slot 1
        auto trial = self_->inv;
        if (!m.container[0].empty() &&
            !insertCompleteInventoryStack(trial, m.container[0])) return;
        const Ingredient& ing = r.cells.front();
        auto got = takeRecipeIngredientStack(trial, ing, makeAll);
        if (!got) return;
        ItemStack newFuel = m.container[1];
        // try to place fuel if empty and makeAll is true or slot empty
        if (newFuel.empty()) {
            // find any fuel item in inventory
            for (int i = 9; i <= 44; ++i) {
                auto& s = trial[static_cast<std::size_t>(i)];
                if (!s.empty() && isFuelItem(s.itemId)) {
                    ItemStack one = s;
                    one.count = 1;
                    if (--s.count <= 0) s = ItemStack::air();
                    newFuel = one;
                    break;
                }
            }
        }
        // Publish persistence dirtiness before touching either the live
        // furnace slots or the player's inventory. If the world is closing
        // its mutation gate, reject the whole recipe placement unchanged.
        if (!admitBlockEntityMutation("furnace recipe")) return;
        self_->inv = std::move(trial);
        m.container[0] = std::move(*got);
        m.container[1] = newFuel;
        if (m.blockEntity &&
            m.blockEntity->kind == BlockEntity::Kind::Furnace)
            m.blockEntity->furnace.cookTotal =
                static_cast<std::int16_t>(r.cookingTicks);
        playerLock.unlock();
        if (entityLock.owns_lock()) entityLock.unlock();
        if (mutationGate && mutationGate->owns_lock()) mutationGate->unlock();
        sendMenuContent(m);
        srv_.resendInventory(*self_);
        syncCursorItem();
        return;
    } else if (m.type == MenuType::Stonecutter) {
        if (r.kind != Recipe::Kind::Stonecutting) return;
        if (r.cells.empty()) return;
        ItemStack* input = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* output = m.container ? &m.container[1] : &m.extraSlots[1];
        auto trial = self_->inv;
        if (!input->empty() && !insertCompleteInventoryStack(trial, *input)) return;
        const Ingredient& ing = r.cells.front();
        auto got = takeRecipeIngredientStack(trial, ing, makeAll);
        if (!got) return;
        if (!admitBlockEntityMutation("stonecutter recipe")) return;
        self_->inv = std::move(trial);
        *input = std::move(*got);
        *output = r.result;
        const std::int32_t windowId = m.windowId;
        playerLock.unlock();
        if (entityLock.owns_lock()) entityLock.unlock();
        if (mutationGate && mutationGate->owns_lock()) mutationGate->unlock();
        // The full menu snapshot below advances the revision atomically; an
        // additional `invStateId + 1` SetSlot used to advertise a revision
        // that was not actually committed.
        WriteBuffer b;
        b.varint(windowId);
        b.varint(recipeId);
        conn_->trySendPacket(pl::sc::PlaceGhostRecipe, b);
        sendMenuContent(m);
        srv_.resendInventory(*self_);
        syncCursorItem();
        return;
    }
    // For other containers (Enchantment, Anvil, Brewing, etc.), PlaceRecipe is no-op but we still ack
}
void Session::handlePlaceGhostRecipe(std::int32_t recipeId) {
    std::shared_ptr<Menu> menu;
    std::shared_ptr<BlockEntity> menuOwner;
    std::int32_t playerEntityId = 0;
    {
        std::lock_guard playerLock(self_->stateMtx);
        if (!openMenu_ || openMenu_->type != MenuType::Stonecutter) return;
        menu = openMenu_;
        menuOwner = menu->blockEntityOwner;
        playerEntityId = self_->entityId;
    }
    // throttle 0x39: limit to 1 per 5 ticks per player
    {
        // ghostThrottle_ is shared by all session threads, whereas the
        // throttle entry is not part of Player::stateMtx.  Protect the map
        // separately and hold it only for the lookup/update.
        static std::mutex ghostThrottleMutex;
        std::lock_guard throttleLock(ghostThrottleMutex);
        auto& thr = srv_.ghostThrottle_;
        std::int64_t now = srv_.tickNo_;
        auto it = thr.find(playerEntityId);
        if (it != thr.end() && now - it->second < 5) return;
        thr[playerEntityId] = now;
    }
    const auto& all = srv_.recipes().all();
    const auto recipeEntry = recipe_book::entryForDisplayId(all, recipeId);
    if (!recipeEntry) return;
    const Recipe selectedRecipe = recipe_book::recipeForDisplay(
        all[recipeEntry->recipeIndex], recipeEntry->displayIndex);
    const Recipe& r = selectedRecipe;
    if (r.kind != Recipe::Kind::Stonecutting) return;

    std::unique_lock<std::recursive_mutex> entityLock;
    if (menuOwner && menuOwner->stateMtx)
        entityLock = std::unique_lock<std::recursive_mutex>(*menuOwner->stateMtx);
    std::unique_lock playerLock(self_->stateMtx);
    if (openMenu_.get() != menu.get() || menu->type != MenuType::Stonecutter) return;
    Menu& m = *menu;
    if (r.cells.empty()) return;
    ItemStack* input = m.container ? &m.container[0] : &m.extraSlots[0];
    ItemStack* output = m.container ? &m.container[1] : &m.extraSlots[1];
    if (input->empty() || !r.cells.front().accepts(input->itemId)) return;
    *output = r.result;
    const std::int32_t windowId = m.windowId;
    const ItemStack outputSnapshot = *output;
    ++self_->invStateId;
    const std::int32_t stateId = self_->invStateId;
    playerLock.unlock();
    if (entityLock.owns_lock()) entityLock.unlock();
    // Send the committed revision, not a fabricated `current + 1` value.
    sendSetSlot(windowId, stateId, 1, outputSnapshot);
    // echo PlaceGhostRecipe back to client
    WriteBuffer b;
    b.varint(windowId);
    b.varint(recipeId);
    conn_->trySendPacket(pl::sc::PlaceGhostRecipe, b);
}


} // namespace cppfm
