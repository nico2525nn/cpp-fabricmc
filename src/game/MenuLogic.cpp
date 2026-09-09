#include "MenuLogic.hpp"
#include "MenuInteraction.hpp"
#include "GameServer.hpp"
#include "CostCalculator.hpp"
#include "../generated/ItemIds.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <mutex>
#include <random>
#include <string>

namespace cppfm {

// helper to swap/merge like ClickLogic but for result slots — polish: respect maxStackFor and components
static bool isSameForMerge(const ItemStack& a, const ItemStack& b) {
    return !a.empty() && !b.empty() && a.itemId == b.itemId &&
           a.components == b.components && a.removedComponents == b.removedComponents;
}
// Max stack size by item id — single-sourced from MenuInteraction::stackLimit via maxStackForId (identical 47x16 / 203x1 tables,
// mechanically verified). NOTE: the merge PREDICATE here (isSameForMerge: components must be EQUAL) intentionally differs from
// MenuInteraction click merging (both components must be EMPTY) — result-slot merging (anvil/enchant) vs click merging have different
// vanilla semantics. Only the limit lookup is unified.
static bool mergeStack(ItemStack& from, ItemStack& to) {
    if (from.empty()) return false;
    if (to.empty()) { to=from; from=ItemStack::air(); return true; }
    if (!isSameForMerge(from, to)) return false;
    int limit = maxStackForId(to.itemId);
    if (to.count >= limit) return false;
    int take = std::min<int>(from.count, limit - to.count);
    to.count = static_cast<std::int16_t>(to.count + take);
    from.count = static_cast<std::int16_t>(from.count - take);
    if (from.count<=0) from=ItemStack::air();
    return true;
}

// ---------------- Anvil ----------------

void AnvilMenuLogic::recomputeResult(Menu& menu) {
    ItemStack* left = menu.container ? &menu.container[0] : &menu.extraSlots[0];
    ItemStack* right = menu.container ? &menu.container[1] : &menu.extraSlots[1];
    ItemStack* result = menu.container ? &menu.container[2] : &menu.extraSlots[2];
    if (left->empty()) { *result = ItemStack::air(); return; }
    const std::string& rename = menu.anvilRename;
    int cost = CostCalculator::anvilCost(*left, *right, rename);
    if (cost < 0) { *result = ItemStack::air(); return; }
    if (cost==0 && right->empty() && rename.empty()) { *result = ItemStack::air(); return; }
    // Too Expensive check (creative bypass handled in Session, here assume survival)
    if (cost >= 40) { *result = ItemStack::air(); return; }
    ItemStack out = *left;
    // repair: reduce damage if both are same item or right is repair material
    if (!right->empty()) {
        if (right->itemId == left->itemId) {
            int maxD = ItemStack::maxDamageFor(left->itemId);
            if (maxD>0) {
                int cur = out.getDamage();
                int repaired = std::max(0, cur - maxD/4);
                out.setDamage(repaired);
            }
        } else {
            // material repair: also reduce damage a bit
            int maxD = ItemStack::maxDamageFor(left->itemId);
            if (maxD>0) {
                int cur = out.getDamage();
                int repaired = std::max(0, cur - maxD/8 * right->count);
                out.setDamage(repaired);
            }
        }
        // merge enchantments: copy enchants from right to left? simplified
        if (right->hasEnchant("minecraft:protection") || right->hasEnchant("protection")) {
            // copy enchant level
            int lvl = right->enchantLevel("protection");
            if (lvl==0) lvl = right->enchantLevel("minecraft:protection");
            if (lvl>0) ItemStack::addEnchant(out, "minecraft:protection", lvl);
        }
    }
    // rename: store custom name as component 5
    if (!rename.empty()) {
        out.setCustomName(rename);
    }
    // next repair cost
    int nextCost = CostCalculator::nextRepairCost(*left, *right);
    out.setRepairCost(nextCost);
    out.count = 1;
    *result = out;
}

void AnvilMenuLogic::onContentChanged(Menu& menu, Player& player) {
    recomputeResult(menu);
}

bool AnvilMenuLogic::onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                                 ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) {
    (void)button; (void)mode; (void)recipes;
    // Anvil container slots 0,1 inputs 2 result
    if (slotId==2) {
        // take result
        ItemStack* result = menu.container ? &menu.container[2] : &menu.extraSlots[2];
        if (result->empty()) return false;
        ItemStack* left = menu.container ? &menu.container[0] : &menu.extraSlots[0];
        ItemStack* right = menu.container ? &menu.container[1] : &menu.extraSlots[1];
        const std::string& rename = menu.anvilRename;
        int cost = CostCalculator::anvilCost(*left, *right, rename);
        if (cost < 0) return false;
        // check XP level (player.xp.level) — require cost
        if (player.gamemode==0 && player.xp.level < cost) {
            // also allow if creative
            return false;
        }
        // deduct XP
        if (player.gamemode==0 && cost>0) {
            player.xp.level = std::max(0, player.xp.level - cost);
            // need to sync XP bar
            if (player.conn) GameServer::sendSetExperience(player);
        }
        // move result to cursor or inventory
        if (cursor.empty()) cursor = *result;
        else if (!mergeStack(*result, cursor)) return false;
        // consume inputs
        if (!left->empty()) { left->count -= 1; if (left->count<=0) *left=ItemStack::air(); }
        if (!right->empty()) { right->count -= 1; if (right->count<=0) *right=ItemStack::air(); }
        *result = ItemStack::air();
        menu.anvilRename.clear();
        io.blockEntityChanged(menu.blockKey);
        return true;
    }
    // For inputs 0,1 and player inventory, delegate to generic pickup/place via ClickLogic helper
    // We recompute result after any change to inputs
    bool changed = false;
    // Use ClickLogic pickupPlace-like for slot 0/1 via direct handling
    if (slotId==0 || slotId==1) {
        ItemStack* target = menu.container ? &menu.container[slotId] : &menu.extraSlots[slotId];
        if (button==0) {
            if (cursor.empty() && !target->empty()) { cursor=*target; *target=ItemStack::air(); changed=true; }
            else if (!cursor.empty() && target->empty()) { *target=cursor; cursor=ItemStack::air(); changed=true; }
            else if (!cursor.empty() && !target->empty() && cursor.itemId==target->itemId) {
                // merge
                int limit=64;
                int take = std::min<int>(cursor.count, limit - target->count);
                if (take>0) { target->count+=take; cursor.count-=take; if(cursor.count<=0) cursor=ItemStack::air(); changed=true; }
                else { std::swap(cursor,*target); changed=true; }
            } else { std::swap(cursor,*target); changed=true; }
        } else { // right click half
            if (cursor.empty() && !target->empty()) {
                int half=(target->count+1)/2;
                cursor=*target; cursor.count=half; target->count-=half; if(target->count<=0) *target=ItemStack::air(); changed=true;
            } else if (!cursor.empty()) {
                if (target->empty()) { *target=ItemStack::of(cursor.itemId,1); cursor.count--; if(cursor.count<=0) cursor=ItemStack::air(); changed=true; }
                else if (target->itemId==cursor.itemId && target->count<64) { target->count++; cursor.count--; if(cursor.count<=0) cursor=ItemStack::air(); changed=true; }
            }
        }
        if (changed) recomputeResult(menu);
        return changed;
    }
    return false;
}

// ---------------- Enchantment ----------------

void EnchantmentMenuLogic::onContentChanged(Menu& menu, Player& player) {
    // Offers are derived from the two inputs and the player's seed. They are
    // intentionally not cached here because MenuLogic instances are shared by
    // menu type, while the inputs and seed belong to one open menu/player.
    (void)menu;
    (void)player;
}

namespace {

struct EnchantmentKind {
    const char* name;
    int maxLevel;
};

std::vector<EnchantmentKind> supportedEnchantmentsFor(const ItemStack& item) {
    const std::string itemName = item.name();
    if (itemName == "minecraft:book") {
        return {{"minecraft:protection", 4}, {"minecraft:efficiency", 5},
                {"minecraft:unbreaking", 3}};
    }
    if (item.isArmor()) {
        return {{"minecraft:protection", 4}, {"minecraft:fire_protection", 4},
                {"minecraft:unbreaking", 3}};
    }
    if (itemName.find("sword") != std::string::npos) {
        return {{"minecraft:sharpness", 5}, {"minecraft:looting", 3},
                {"minecraft:unbreaking", 3}};
    }
    if (itemName == "minecraft:bow") {
        return {{"minecraft:power", 5}, {"minecraft:punch", 2},
                {"minecraft:unbreaking", 3}};
    }
    if (item.isTool()) {
        return {{"minecraft:efficiency", 5}, {"minecraft:fortune", 3},
                {"minecraft:unbreaking", 3}};
    }
    return {};
}

std::uint32_t offerSeed(const ItemStack& item, const Player& player,
                        int bookshelves) {
    std::uint32_t seed = static_cast<std::uint32_t>(player.enchantmentSeed);
    seed ^= item.itemId * 0x9e3779b9u;
    seed ^= static_cast<std::uint32_t>(std::clamp(bookshelves, 0, 15)) *
            0x85ebca6bu;
    return CostCalculator::splitmix32(seed);
}

} // namespace

std::array<EnchantmentOffer, 3> EnchantmentMenuLogic::offers(
    const Menu& menu, const Player& player, int bookshelves) const {
    std::array<EnchantmentOffer, 3> out{};
    const ItemStack* item = menu.container ? &menu.container[0]
                                           : &menu.extraSlots[0];
    if (item->empty()) return out;

    auto candidates = supportedEnchantmentsFor(*item);
    if (candidates.empty()) return out;

    const auto costs = CostCalculator::enchantingCostsForShelves(
        player, std::clamp(bookshelves, 0, 15));
    std::mt19937 rng(offerSeed(*item, player, bookshelves));
    const std::size_t rotation =
        static_cast<std::size_t>(rng() % candidates.size());
    const bool plainBook = item->name() == "minecraft:book";
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto candidate = candidates[(rotation + i) % candidates.size()];
        const int cost = std::clamp(costs[i], 1, 30);
        const int levelRange = std::max(
            1, std::min(candidate.maxLevel, cost / 5 + 1));
        const int level = 1 + static_cast<int>(
            rng() % static_cast<std::uint32_t>(levelRange));
        out[i].levelCost = cost;
        out[i].lapisCost = static_cast<int>(i) + 1;
        out[i].enchantmentId = ItemStack::enchantIdByName(candidate.name);
        out[i].enchantmentLevel = level;
        out[i].enchantment = candidate.name;
        out[i].result = *item;
        if (plainBook) {
            const auto enchantedBook = gen::itemIdByName().find(
                "minecraft:enchanted_book");
            if (enchantedBook != gen::itemIdByName().end())
                out[i].result.itemId = enchantedBook->second;
        }
        ItemStack::addEnchant(out[i].result, out[i].enchantment, level);
    }
    return out;
}

bool EnchantmentMenuLogic::onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                                       ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) {
    (void)io; (void)recipes; (void)mode;
    // Vanilla constrains the item slot to one enchantable item and the
    // second slot to lapis lazuli.  Keep those constraints in the shared
    // menu path so a client cannot bypass them with WindowClick.
    if (slotId==0 || slotId==1) {
        ItemStack* target = menu.container ? &menu.container[slotId] : &menu.extraSlots[slotId];
        if (slotId == 0 && !cursor.empty() &&
            supportedEnchantmentsFor(cursor).empty())
            return false;
        if (slotId == 1 && !cursor.empty()) {
            const auto lapis = gen::itemIdByName().find("minecraft:lapis_lazuli");
            if (lapis == gen::itemIdByName().end() ||
                cursor.itemId != lapis->second)
                return false;
        }
        bool changed=false;
        if (button==0) {
            if (cursor.empty() && !target->empty()) { cursor=*target; *target=ItemStack::air(); changed=true; }
            else if (!cursor.empty() && target->empty()) {
                const int limit = slotId == 0 ? 1 : maxStackForId(cursor.itemId);
                const int moved = std::min<int>(cursor.count, limit);
                *target = cursor;
                target->count = static_cast<std::int16_t>(moved);
                cursor.count = static_cast<std::int16_t>(cursor.count - moved);
                if (cursor.count <= 0) cursor=ItemStack::air();
                changed = moved > 0;
            } else if (!cursor.empty() && !target->empty() &&
                       cursor.itemId == target->itemId &&
                       cursor.components == target->components &&
                       cursor.removedComponents == target->removedComponents) {
                const int limit = slotId == 0 ? 1 : maxStackForId(target->itemId);
                const int moved = std::min<int>(cursor.count, limit - target->count);
                if (moved > 0) {
                    target->count = static_cast<std::int16_t>(target->count + moved);
                    cursor.count = static_cast<std::int16_t>(cursor.count - moved);
                    if (cursor.count <= 0) cursor=ItemStack::air();
                    changed = true;
                }
            } else {
                return false;
            }
        } else {
            if (cursor.empty() && !target->empty()) {
                int half=(target->count+1)/2;
                cursor=*target; cursor.count=half; target->count-=half; if(target->count<=0) *target=ItemStack::air(); changed=true;
            } else if (!cursor.empty() && target->empty()) {
                *target=cursor; target->count=1; cursor.count--; if(cursor.count<=0) cursor=ItemStack::air(); changed=true;
            } else if (!cursor.empty() && !target->empty() &&
                       cursor.itemId == target->itemId &&
                       cursor.components == target->components &&
                       cursor.removedComponents == target->removedComponents) {
                const int limit = slotId == 0 ? 1 : maxStackForId(target->itemId);
                if (target->count < limit) {
                    ++target->count;
                    if (--cursor.count <= 0) cursor=ItemStack::air();
                    changed=true;
                }
            }
        }
        if (changed) onContentChanged(menu, player);
        return changed;
    }
    return false;
}

bool EnchantmentMenuLogic::onEnchantButton(Menu& menu, Player& player, int buttonId, MenuIo& io) {
    return onEnchantButton(menu, player, buttonId, io, 15);
}
bool EnchantmentMenuLogic::onEnchantButton(Menu& menu, Player& player, int buttonId, MenuIo& io, int bookshelves) {
    ItemStack* item = menu.container ? &menu.container[0] : &menu.extraSlots[0];
    ItemStack* lapis = menu.container ? &menu.container[1] : &menu.extraSlots[1];
    const auto lapisId = gen::itemIdByName().find("minecraft:lapis_lazuli");
    if (buttonId < 0 || buttonId >= 3 || item->empty() || lapis->empty() ||
        lapisId == gen::itemIdByName().end() || lapis->itemId != lapisId->second)
        return false;

    const auto choices = offers(menu, player, bookshelves);
    const EnchantmentOffer& choice =
        choices[static_cast<std::size_t>(buttonId)];
    if (!choice.selectable() || lapis->count < choice.lapisCost) return false;
    const int requiredLevel = buttonId + 1;
    if (player.gamemode == 0 &&
        (player.xp.level < requiredLevel || player.xp.level < choice.levelCost))
        return false;

    // Apply the already-presented choice, rather than re-rolling a different
    // enchantment during the packet handler.
    *item = choice.result;
    lapis->count -= choice.lapisCost;
    if (lapis->count<=0) *lapis=ItemStack::air();
    // Deduct XP
    if (player.gamemode==0) {
        player.xp.level = std::max(0, player.xp.level - choice.levelCost);
        GameServer::sendSetExperience(player);
    }
    // Vanilla advances the table seed after a successful enchantment.  The
    // exact Java Random stream is outside this bounded native model, but
    // retaining the state transition prevents the next offer set from being
    // a replay of the previous one.
    std::uint32_t nextSeed = CostCalculator::splitmix32(
        static_cast<std::uint32_t>(player.enchantmentSeed) +
        0x9e3779b9u + static_cast<std::uint32_t>(buttonId));
    if (nextSeed == 0) nextSeed = 0x5a5a5a5au;
    player.enchantmentSeed = static_cast<std::int32_t>(nextSeed);
    io.blockEntityChanged(menu.blockKey);
    return true;
}

// ---------------- Brewing ----------------

bool BrewingMenuLogic::onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                                   ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) {
    (void)player; (void)recipes;
    // Brewing slots: 0-2 bottles, 3 ingredient, 4 fuel (blaze powder)
    if (slotId <5) {
        ItemStack* target = menu.container ? &menu.container[slotId] : &menu.extraSlots[slotId];
        bool changed=false;
        if (mode==1) { // quick move
            // shift-click: move to player inv or from player to brewing simplified: swap with cursor
            std::swap(cursor, *target);
            changed=true;
        } else if (button==0) {
            if (cursor.empty() && !target->empty()) { cursor=*target; *target=ItemStack::air(); changed=true; }
            else if (!cursor.empty() && target->empty()) { *target=cursor; cursor=ItemStack::air(); changed=true; }
            else { std::swap(cursor,*target); changed=true; }
        } else {
            if (cursor.empty() && !target->empty()) {
                int half=(target->count+1)/2;
                cursor=*target; cursor.count=half; target->count-=half; if(target->count<=0) *target=ItemStack::air(); changed=true;
            } else if (!cursor.empty() && target->empty()) {
                *target=ItemStack::of(cursor.itemId,1); cursor.count--; if(cursor.count<=0) cursor=ItemStack::air(); changed=true;
            }
        }
        if (changed) io.blockEntityChanged(menu.blockKey);
        return changed;
    }
    return false;
}

// ---------------- Stonecutter ----------------

bool StonecutterMenuLogic::onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                                       ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) {
    (void)player; (void)mode;
    // slots: 0 input, 1 result (take-only) — stonecutter has no `triggered` blockstate (crafter only has it); no toggle here.
    if (slotId==1) {
        ItemStack* result = menu.container ? &menu.container[1] : &menu.extraSlots[1];
        if (result->empty()) return false;
        if (cursor.empty()) cursor=*result;
        else if (cursor.itemId==result->itemId && cursor.count<64) {
            int take = std::min<int>(result->count, 64-cursor.count);
            cursor.count+=take; result->count-=take; if(result->count<=0) *result=ItemStack::air();
            // already handled need to consume input
        } else return false;
        // consume input (one)
        ItemStack* input = menu.container ? &menu.container[0] : &menu.extraSlots[0];
        if (!input->empty()) { input->count--; if(input->count<=0) *input=ItemStack::air(); }
        // result already taken
        result->count=0; *result=ItemStack::air(); // after taking, clear? Actually we already moved
        // For simplicity after taking, keep result if input remains? Should recompute ghost recipe Use recipes stonecutting
        if (!input->empty()) {
            auto* r = recipes.findStonecutting(input->itemId);
            if (r) *result = r->result;
            else *result = ItemStack::air();
        }
        // The menu mutation is persisted through the shared block-entity
        // dirty hook; stonecutter and crafter block states are not toggled by
        // taking an output from a menu.
        io.blockEntityChanged(menu.blockKey);
        return true;
    }
    if (slotId==0) {
        ItemStack* input = menu.container ? &menu.container[0] : &menu.extraSlots[0];
        bool changed=false;
        if (button==0) {
            if (cursor.empty() && !input->empty()) { cursor=*input; *input=ItemStack::air(); changed=true; }
            else if (!cursor.empty() && input->empty()) { *input=cursor; cursor=ItemStack::air(); changed=true; }
            else { std::swap(cursor,*input); changed=true; }
        } else {
            if (cursor.empty() && !input->empty()) {
                int half=(input->count+1)/2; cursor=*input; cursor.count=half; input->count-=half; if(input->count<=0) *input=ItemStack::air(); changed=true;
            } else if (!cursor.empty() && input->empty()) {
                *input=ItemStack::of(cursor.itemId,1); cursor.count--; if(cursor.count<=0) cursor=ItemStack::air(); changed=true;
            }
        }
        if (changed) {
            ItemStack* result = menu.container ? &menu.container[1] : &menu.extraSlots[1];
            if (!input->empty()) {
                auto* rec = recipes.findStonecutting(input->itemId);
                if (rec) *result = rec->result;
                else *result = ItemStack::air();
            } else *result = ItemStack::air();
            io.blockEntityChanged(menu.blockKey);
        }
        return changed;
    }
    return false;
}

// ---------------- Crafter ----------------

bool CrafterMenuLogic::craftOnRedstone(Menu& menu, const RecipeManager& recipes,
                                       ItemStack& outputSink, MenuIo& io) const {
    if (menu.type != MenuType::Crafter) return false;
    if (menu.containerCount > 0 && menu.containerCount < 9) return false;

    ItemStack* slots = menu.container ? menu.container : menu.extraSlots;
    std::vector<ItemStack> grid(9, ItemStack::air());
    for (int i = 0; i < 9; ++i) {
        // Disabled slots are not recipe inputs and automation must not consume
        // or refill them.  Keeping them empty in the recipe view also mirrors
        // CrafterBlockEntity's RecipeInputInventory contract.
        if (!menu.crafterDisabledSlots ||
            ((*menu.crafterDisabledSlots & (std::uint16_t{1} << i)) == 0))
            grid[i] = slots[i];
    }
    const Recipe* recipe = recipes.findCrafting(grid, 3, 3);
    if (!recipe || recipe->result.empty()) return false;

    const ItemStack& result = recipe->result;
    const int limit = maxStackForId(result.itemId);
    if (result.count <= 0 || result.count > limit) return false;

    // Check and reserve the destination before consuming any input. A
    // redstone pulse must be atomic when the adjacent destination is full or
    // contains a different item.
    if (outputSink.empty()) {
        outputSink = result;
    } else {
        const bool same = outputSink.itemId == result.itemId &&
                          outputSink.components == result.components &&
                          outputSink.removedComponents == result.removedComponents;
        if (!same || outputSink.count < 0 || outputSink.count > limit - result.count)
            return false;
        outputSink.count = static_cast<std::int16_t>(outputSink.count + result.count);
    }

    for (int i = 0; i < 9; ++i) {
        if (menu.crafterDisabledSlots &&
            ((*menu.crafterDisabledSlots & (std::uint16_t{1} << i)) != 0))
            continue;
        if (slots[i].empty()) continue;
        --slots[i].count;
        if (slots[i].count <= 0) slots[i] = ItemStack::air();
    }
    io.blockEntityChanged(menu.blockKey);
    return true;
}

bool CrafterMenuLogic::onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                                   ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) {
    (void)player; (void)recipes;
    // Crafter 9 slots (0..8) + player inventory 36.  An empty click on an
    // empty input slot toggles that slot's disabled state, matching the
    // vanilla CrafterBlockEntity mask.  Redstone automation, including the
    // `triggered` block-state edge and output routing, belongs to the server
    // tick path and must not be coupled to a player menu click.
    int cont = 9;
    if (slotId < cont) {
        ItemStack* target = menu.container ? &menu.container[slotId] : &menu.extraSlots[slotId];
        bool changed=false;
        if (mode==1) return false; // quick move not handled, fall back to ClickLogic
        const bool disabled = menu.crafterDisabledSlots &&
            ((*menu.crafterDisabledSlots & (std::uint16_t{1} << slotId)) != 0);
        if (cursor.empty() && target->empty()) {
            if (!menu.crafterDisabledSlots) return false;
            const auto bit = static_cast<std::uint16_t>(std::uint16_t{1} << slotId);
            if (disabled) *menu.crafterDisabledSlots =
                static_cast<std::uint16_t>(*menu.crafterDisabledSlots & ~bit);
            else *menu.crafterDisabledSlots =
                static_cast<std::uint16_t>(*menu.crafterDisabledSlots | bit);
            io.blockEntityChanged(menu.blockKey);
            return true;
        }
        // A disabled slot is a hard automation/UI insertion barrier, but an
        // existing stack can still be taken out after the slot is disabled.
        if (disabled && !cursor.empty()) return false;
        if (button==0) {
            if (cursor.empty() && !target->empty()) { cursor=*target; *target=ItemStack::air(); changed=true; }
            else if (!cursor.empty() && target->empty()) { *target=cursor; cursor=ItemStack::air(); changed=true; }
            else { std::swap(cursor,*target); changed=true; }
        } else {
            if (cursor.empty() && !target->empty()) {
                int half=(target->count+1)/2; cursor=*target; cursor.count=half; target->count-=half; if(target->count<=0) *target=ItemStack::air(); changed=true;
            } else if (!cursor.empty() && target->empty()) {
                *target=ItemStack::of(cursor.itemId,1); cursor.count--; if(cursor.count<=0) cursor=ItemStack::air(); changed=true;
            }
        }
        if (changed) io.blockEntityChanged(menu.blockKey);
        // The server-side redstone path is intentionally separate; a menu
        // click must never toggle `triggered` or craft implicitly.
        return changed;
    }
    return false;
}

// ---------------- Cartography ----------------

void CartographyMenuLogic::recomputeResult(Menu& menu) {
    // slots: 0 map, 1 paper, 2 result (output)
    ItemStack* map = menu.container ? &menu.container[0] : &menu.extraSlots[0];
    ItemStack* paper = menu.container ? &menu.container[1] : &menu.extraSlots[1];
    ItemStack* result = menu.container ? &menu.container[2] : &menu.extraSlots[2];
    // Vanilla supports cloning and scale upgrades. This partial path currently
    // exposes the cloning-shaped result when both inputs are present.
    if (!map->empty() && !paper->empty()) {
        // Check map is "minecraft:filled_map" or "minecraft:map"; ItemStack names
        // are used here because map components are not modeled by this menu yet.
        std::string mn = map->name();
        if (mn.find("map") != std::string::npos) {
            *result = *map;
            result->count = 1;
            return;
        }
    }
    *result = ItemStack::air();
}

void CartographyMenuLogic::onContentChanged(Menu& menu, Player& player) {
    recomputeResult(menu);
}

bool CartographyMenuLogic::onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                                       ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) {
    (void)player; (void)recipes;
    if (slotId == 2) {
        ItemStack* result = menu.container ? &menu.container[2] : &menu.extraSlots[2];
        if (result->empty()) return false;
        if (cursor.empty()) cursor = *result;
        else if (cursor.itemId == result->itemId && cursor.count < 64) {
            int take = std::min<int>(result->count, 64 - cursor.count);
            cursor.count = static_cast<std::int16_t>(cursor.count + take);
            result->count = static_cast<std::int16_t>(result->count - take);
            if (result->count <= 0) *result = ItemStack::air();
            // already moved part; need to consume inputs if we took something
            if (!result->empty()) return true;
        } else return false;
        // consume inputs
        ItemStack* map = menu.container ? &menu.container[0] : &menu.extraSlots[0];
        ItemStack* paper = menu.container ? &menu.container[1] : &menu.extraSlots[1];
        if (!map->empty()) { if (--map->count <= 0) *map = ItemStack::air(); }
        if (!paper->empty()) { if (--paper->count <= 0) *paper = ItemStack::air(); }
        *result = ItemStack::air();
        recomputeResult(menu);
        io.blockEntityChanged(menu.blockKey);
        return true;
    }
    if (slotId == 0 || slotId == 1) {
        ItemStack* target = menu.container ? &menu.container[slotId] : &menu.extraSlots[slotId];
        bool changed=false;
        if (button==0) {
            if (cursor.empty() && !target->empty()) { cursor=*target; *target=ItemStack::air(); changed=true; }
            else if (!cursor.empty() && target->empty()) { *target=cursor; cursor=ItemStack::air(); changed=true; }
            else { std::swap(cursor,*target); changed=true; }
        } else {
            if (cursor.empty() && !target->empty()) {
                int half=(target->count+1)/2; cursor=*target; cursor.count=half; target->count-=half; if(target->count<=0) *target=ItemStack::air(); changed=true;
            } else if (!cursor.empty() && target->empty()) {
                *target=ItemStack::of(cursor.itemId,1); cursor.count--; if(cursor.count<=0) cursor=ItemStack::air(); changed=true;
            }
        }
        if (changed) recomputeResult(menu);
        if (changed) io.blockEntityChanged(menu.blockKey);
        return changed;
    }
    return false;
}

// ---------------- Generic ----------------

bool GenericMenuLogic::onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                                   ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) {
    (void)player; (void)recipes;
    // For grindstone/smithing/beacon/loom etc., just handle inputs generically: slots 0..containerCount-1 are inputs
    int cont = menu.totalSlots() - 36;
    if (slotId < cont) {
        ItemStack* target = menu.container ? &menu.container[slotId] : &menu.extraSlots[slotId];
        bool changed=false;
        if (mode==1) {
            // quick move not handled here
            return false;
        }
        if (button==0) {
            if (cursor.empty() && !target->empty()) { cursor=*target; *target=ItemStack::air(); changed=true; }
            else if (!cursor.empty() && target->empty()) { *target=cursor; cursor=ItemStack::air(); changed=true; }
            else { std::swap(cursor,*target); changed=true; }
        } else {
            if (cursor.empty() && !target->empty()) {
                int half=(target->count+1)/2; cursor=*target; cursor.count=half; target->count-=half; if(target->count<=0) *target=ItemStack::air(); changed=true;
            } else if (!cursor.empty() && target->empty()) {
                *target=ItemStack::of(cursor.itemId,1); cursor.count--; if(cursor.count<=0) cursor=ItemStack::air(); changed=true;
            }
        }
        if (changed) io.blockEntityChanged(menu.blockKey);
        return changed;
    }
    return false;
}

// ---------------- Factory ----------------

std::unique_ptr<MenuLogic> createMenuLogic(MenuType type) {
    switch(type) {
        case MenuType::Anvil: return std::make_unique<AnvilMenuLogic>();
        case MenuType::Enchantment: return std::make_unique<EnchantmentMenuLogic>();
        case MenuType::Brewing: return std::make_unique<BrewingMenuLogic>();
        case MenuType::Stonecutter: return std::make_unique<StonecutterMenuLogic>();
        case MenuType::Crafter: return std::make_unique<CrafterMenuLogic>();
        case MenuType::CartographyTable: return std::make_unique<CartographyMenuLogic>();
        case MenuType::Grindstone: return std::make_unique<GenericMenuLogic>("Grindstone");
        case MenuType::Smithing: return std::make_unique<GenericMenuLogic>("Smithing");
        case MenuType::Beacon: return std::make_unique<GenericMenuLogic>("Beacon");
        case MenuType::Loom: return std::make_unique<GenericMenuLogic>("Loom");
        case MenuType::BlastFurnace: return std::make_unique<GenericMenuLogic>("BlastFurnace");
        case MenuType::Smoker: return std::make_unique<GenericMenuLogic>("Smoker");
        case MenuType::Lectern: return std::make_unique<GenericMenuLogic>("Lectern");
        case MenuType::Merchant: return std::make_unique<GenericMenuLogic>("Merchant");
        case MenuType::Chest: return nullptr; // handled by ClickLogic generic
        case MenuType::Furnace: return nullptr;
        case MenuType::Crafting: return nullptr;
        case MenuType::Generic9x1: return nullptr;
        case MenuType::Generic9x2: return nullptr;
        case MenuType::Generic9x4: return nullptr;
        case MenuType::Generic9x6: return nullptr;
        default: return nullptr;
    }
}

MenuLogic* getMenuLogic(MenuType type) {
    // Menu logic is shared by sessions, so initialise the complete table once
    // instead of mutating an unordered_map on the first concurrent click.
    // The object implementations are stateless; per-menu state stays in Menu.
    static std::once_flag once;
    static std::array<std::unique_ptr<MenuLogic>, 25> cache;
    std::call_once(once, [] {
        for (int raw = 0; raw < static_cast<int>(cache.size()); ++raw) {
            const auto menuType = static_cast<MenuType>(raw);
            cache[static_cast<std::size_t>(raw)] = createMenuLogic(menuType);
        }
    });
    const auto raw = static_cast<int>(type);
    if (raw < 0 || raw >= static_cast<int>(cache.size())) return nullptr;
    return cache[static_cast<std::size_t>(raw)].get();
}

} // namespace cppfm
