#include "GameServer.hpp"
#include "GameServerHelpers.hpp"
#include "MenuInteraction.hpp"
#include "SessionMenuSupport.hpp"
#include "../generated/ItemIds.hpp"

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
    const auto& all = srv_.recipes().all();
    WriteBuffer b;
    b.varint(static_cast<std::int32_t>(all.size()));
    std::int32_t displayId = 0;
    const auto tableItem = gen::itemIdByName().at("minecraft:crafting_table");
    const auto furnaceItem = gen::itemIdByName().at("minecraft:furnace");
    for (const auto& r : all) {
        // entry: {recipe:{displayId,display,group,category,requirements?},flags}
        b.varint(displayId);
        switch (r.kind) {
        case Recipe::Kind::Shaped:
            b.varint(1);                       // crafting_shaped
            b.varint(r.width);
            b.varint(r.height);
            b.varint(static_cast<std::int32_t>(r.cells.size()));
            for (auto& ing : r.cells)
                writeSlotDisplayItem(b, ing.items.empty()
                                         ? 0 : *ing.items.begin());
            writeSlotDisplayItem(b, r.result.itemId);
            writeSlotDisplayItem(b, tableItem);   // craftingStation
            break;
        case Recipe::Kind::Shapeless: {
            b.varint(0);                       // crafting_shapeless
            b.varint(static_cast<std::int32_t>(r.ingredients.size()));
            for (auto& ing : r.ingredients)
                writeSlotDisplayItem(b, ing.items.empty()
                                         ? 0 : *ing.items.begin());
            writeSlotDisplayItem(b, r.result.itemId);
            writeSlotDisplayItem(b, tableItem);
            break;
        }
        case Recipe::Kind::Smelting: {
            b.varint(2);                       // furnace
            writeSlotDisplayItem(b, r.cells.front().items.empty()
                                     ? 0 : *r.cells.front().items.begin());
            writeSlotDisplayItem(b,
                gen::itemIdByName().at("minecraft:coal"));   // fuel
            writeSlotDisplayItem(b, r.result.itemId);
            writeSlotDisplayItem(b, furnaceItem); // station
            b.varint(r.cookingTicks);
            b.f32(r.experience);
            break;
        }
        case Recipe::Kind::Stonecutting: {
            b.varint(3);                       // stonecutter
            writeSlotDisplayItem(b, r.cells.front().items.empty()
                                     ? 0 : *r.cells.front().items.begin());
            writeSlotDisplayItem(b, r.result.itemId);
            writeSlotDisplayItem(b, furnaceItem);
            break;
        }
        case Recipe::Kind::Smithing: {
            b.varint(0);                       // smithing as shapeless for book display
            b.varint(static_cast<std::int32_t>(r.ingredients.size()));
            for (auto& ing : r.ingredients)
                writeSlotDisplayItem(b, ing.items.empty() ? 0 : *ing.items.begin());
            writeSlotDisplayItem(b, r.result.itemId);
            writeSlotDisplayItem(b, tableItem);
            break;
        }
        case Recipe::Kind::Special: {
            b.varint(0);                       // special as shapeless placeholder
            b.varint(0);
            writeSlotDisplayItem(b, r.result.itemId);
            writeSlotDisplayItem(b, tableItem);
            break;
        }
        }
        b.varint(0);                           // group: none (could use r.group hash but 0 for parity)
        b.varint(r.category);                  // use JSON-derived category
        b.boolean(false);                      // craftingRequirements absent
        b.u8(0x03);                            // notification | highlight
        ++displayId;
    }
    b.boolean(true);                           // replace=true
    conn_->trySendPacket(pl::sc::RecipeBookAdd, b);
}
void Session::handlePlaceRecipe(std::int32_t recipeId, bool makeAll) {
    (void)makeAll;
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
    if (recipeId < 0 || static_cast<std::size_t>(recipeId) >= all.size()) return;
    const Recipe& r = all[static_cast<std::size_t>(recipeId)];

    // A recipe placement is one inventory/menu transaction.  In particular,
    // do not empty an existing grid and then call addToInventory(): a full
    // inventory used to make that path silently delete the old inputs.
    // Recipe placement mutates live furnace/stonecutter block entities. Keep
    // the same World -> block entity -> player lock order used by ordinary
    // menu clicks so persistence admission cannot race world shutdown.
    std::optional<MutationGateLease> mutationGate;
    if (menuOwner &&
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

    auto take = [&](
                    std::array<InvSlot,
                               kPlayerInventorySlotCount>& inventory,
                    const Ingredient& ing) -> ItemStack {
        // RecipeBook placement consumes the player's main inventory and
        // hotbar, not armor or off-hand slots.
        for (int i = 9; i <= 44; ++i) {
            auto& s = inventory[static_cast<std::size_t>(i)];
            if (!s.empty() && ing.accepts(s.itemId)) {
                ItemStack one = s;
                one.count = 1;
                if (--s.count <= 0) s = ItemStack::air();
                return one;
            }
        }
        return ItemStack::air();
    };

    if (m.type == MenuType::Crafting) {
        auto trial = self_->inv;
        for (auto& s : m.craftGrid) {
            if (!s.empty()) {
                if (!insertCompleteInventoryStack(trial, s)) return;
            }
        }
        std::array<ItemStack, 9> placed{};
        for (auto& s : placed) s = ItemStack::air();
        bool complete = true;
        if (r.kind == Recipe::Kind::Shaped) {
            if (r.width <= 0 || r.width > 3 || r.height <= 0 || r.height > 3 ||
                r.cells.size() < static_cast<std::size_t>(r.width * r.height))
                complete = false;
            for (int y = 0; y < r.height && complete; ++y)
                for (int x = 0; x < r.width && complete; ++x) {
                    const auto& ing = r.cells[static_cast<std::size_t>(y) *
                                              r.width + x];
                    if (ing.empty()) continue;
                    ItemStack it2 = take(trial, ing);
                    if (it2.empty()) { complete = false; break; }
                    placed[static_cast<std::size_t>(y) * 3 + x] = it2;
                }
        } else if (r.kind == Recipe::Kind::Shapeless) {
            int i = 0;
            for (const auto& ing : r.ingredients) {
                if (i >= 9) break;
                ItemStack it2 = take(trial, ing);
                if (it2.empty()) { complete = false; break; }
                placed[static_cast<std::size_t>(i++)] = it2;
            }
        } else complete = false;
        if (!complete) {
            // `trial` is discarded, so both the original grid and inventory
            // remain exactly as they were when a required ingredient is
            // missing.
            return;
        }
        self_->inv = std::move(trial);
        for (std::size_t i = 0; i < placed.size(); ++i)
            m.craftGrid[i] = placed[i];
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
        if (r.kind != Recipe::Kind::Smelting) return;
        if (!m.container || m.containerCount < 2 || r.cells.empty()) return;
        // Place ingredient into input slot 0, and if needed fuel into slot 1
        auto trial = self_->inv;
        if (!m.container[0].empty() &&
            !insertCompleteInventoryStack(trial, m.container[0])) return;
        const Ingredient& ing = r.cells.front();
        ItemStack got = take(trial, ing);
        if (got.empty()) return;
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
        m.container[0] = got;
        m.container[1] = newFuel;
        const std::int32_t windowId = m.windowId;
        const int cookProgress = m.blockEntity &&
            m.blockEntity->kind == BlockEntity::Kind::Furnace
                ? m.blockEntity->furnace.cookProgress : 0;
        playerLock.unlock();
        if (entityLock.owns_lock()) entityLock.unlock();
        if (mutationGate && mutationGate->owns_lock()) mutationGate->unlock();
        sendMenuContent(m);
        srv_.resendInventory(*self_);
        syncCursorItem();
        WriteBuffer pb;
        pb.varint(windowId);
        pb.i16(0);
        pb.i16(static_cast<std::int16_t>(cookProgress));
        conn_->trySendPacket(pl::sc::ContainerSetData, pb);
        return;
    } else if (m.type == MenuType::Stonecutter) {
        if (r.kind != Recipe::Kind::Stonecutting) return;
        if (r.cells.empty()) return;
        ItemStack* input = m.container ? &m.container[0] : &m.extraSlots[0];
        ItemStack* output = m.container ? &m.container[1] : &m.extraSlots[1];
        auto trial = self_->inv;
        if (!input->empty() && !insertCompleteInventoryStack(trial, *input)) return;
        const Ingredient& ing = r.cells.front();
        ItemStack got = take(trial, ing);
        if (got.empty()) return;
        if (!admitBlockEntityMutation("stonecutter recipe")) return;
        self_->inv = std::move(trial);
        *input = got;
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
    if (recipeId < 0 || static_cast<std::size_t>(recipeId) >= all.size()) return;
    const Recipe& r = all[static_cast<std::size_t>(recipeId)];
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
