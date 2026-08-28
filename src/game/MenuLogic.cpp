#include "MenuLogic.hpp"
#include "MenuInteraction.hpp"
#include "GameServer.hpp"
#include "CostCalculator.hpp"
#include "../generated/ItemIds.hpp"
#include <algorithm>
#include <cstdio>

namespace cppfm {

// helper to swap/merge like ClickLogic but for result slots
static bool mergeStack(ItemStack& from, ItemStack& to) {
    if (from.empty()) return false;
    if (to.empty()) { to=from; from=ItemStack::air(); return true; }
    if (from.itemId!=to.itemId) return false;
    int limit = 64;
    // assume 64
    if (to.count >= limit) return false;
    int take = std::min<int>(from.count, limit - to.count);
    to.count += take; from.count -= take;
    if (from.count<=0) from=ItemStack::air();
    return true;
}

// ---------------- Anvil ----------------

void AnvilMenuLogic::recomputeResult(Menu& menu) {
    // slots: 0 left, 1 right, 2 result (plan13 §4)
    ItemStack* left = menu.container ? &menu.container[0] : &menu.extraSlots[0];
    ItemStack* right = menu.container ? &menu.container[1] : &menu.extraSlots[1];
    ItemStack* result = menu.container ? &menu.container[2] : &menu.extraSlots[2];
    if (left->empty()) { *result = ItemStack::air(); return; }
    std::string rename = !menu.anvilRename.empty() ? menu.anvilRename : pendingRename_;
    int cost = CostCalculator::anvilCost(*left, *right, rename);
    if (cost < 0) { *result = ItemStack::air(); return; }
    if (cost==0 && right->empty() && rename.empty()) { *result = ItemStack::air(); return; }
    // Too Expensive check (creative bypass handled in Session, here assume survival)
    if (cost >= 40) { *result = ItemStack::air(); return; }
    ItemStack out = *left;
    // repair: reduce damage if both are same item or right is repair material
    if (!right->empty()) {
        // if same item type, repair 25% of max durability
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
    (void)player;
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
        std::string rename = !menu.anvilRename.empty() ? menu.anvilRename : pendingRename_;
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
        pendingRename_.clear();
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
    (void)player;
    // slots: 0 item, 1 lapis
    // Could compute enchantment offerings and send ContainerSetData (window property)
    // For now, no-op; actual enchanting via onEnchantButton
    ItemStack* item = menu.container ? &menu.container[0] : &menu.extraSlots[0];
    ItemStack* lapis = menu.container ? &menu.container[1] : &menu.extraSlots[1];
    if (item->empty() || lapis->empty()) return;
    // lapis cost check placeholder: ensure lapis count >=1
}

bool EnchantmentMenuLogic::onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                                       ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) {
    (void)player; (void)io; (void)recipes; (void)button; (void)mode;
    // Enchantment container has only 2 slots; treat similar to Anvil inputs but no result slot
    if (slotId==0 || slotId==1) {
        ItemStack* target = menu.container ? &menu.container[slotId] : &menu.extraSlots[slotId];
        bool changed=false;
        if (button==0) {
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
    if (item->empty()) return false;
    if (lapis->empty() || lapis->count < (buttonId+1)) return false;
    bookshelves = std::clamp(bookshelves, 0, 15);
    auto costs = CostCalculator::enchantingCostsForShelves(player, bookshelves);
    int levelCost = costs[std::clamp(buttonId,0,2)];
    if (player.gamemode==0 && player.xp.level < levelCost) return false;
    // Deduct lapis
    lapis->count -= (buttonId+1);
    if (lapis->count<=0) *lapis=ItemStack::air();
    // Deduct XP
    if (player.gamemode==0) {
        player.xp.level = std::max(0, player.xp.level - levelCost);
        GameServer::sendSetExperience(player);
    }
    // Apply random enchant(s) — simplified: add one enchant per button
    const char* enchants[] = {"minecraft:protection","minecraft:sharpness","minecraft:efficiency","minecraft:unbreaking"};
    const char* chosen = enchants[buttonId % 4];
    int lvl = 1 + (buttonId) + (rand()%2);
    ItemStack::addEnchant(*item, chosen, lvl);
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
            // shift-click: move to player inv or from player to brewing
            // simplified: swap with cursor
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
    // slots: 0 input, 1 result (take-only)
    if (slotId==1) {
        ItemStack* result = menu.container ? &menu.container[1] : &menu.extraSlots[1];
        if (result->empty()) return false;
        if (cursor.empty()) cursor=*result;
        else if (cursor.itemId==result->itemId && cursor.count<64) {
            int take = std::min<int>(result->count, 64-cursor.count);
            cursor.count+=take; result->count-=take; if(result->count<=0) *result=ItemStack::air();
            // already handled
            // need to consume input
        } else return false;
        // consume input (one)
        ItemStack* input = menu.container ? &menu.container[0] : &menu.extraSlots[0];
        if (!input->empty()) { input->count--; if(input->count<=0) *input=ItemStack::air(); }
        // result already taken
        if (button==1) {} // right click similar
        result->count=0; *result=ItemStack::air(); // after taking, clear? Actually we already moved
        // For simplicity after taking, keep result if input remains? Should recompute ghost recipe
        // Use recipes stonecutting
        if (!input->empty()) {
            auto* r = recipes.findStonecutting(input->itemId);
            if (r) *result = r->result;
            else *result = ItemStack::air();
        }
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
        case MenuType::Grindstone: return std::make_unique<GenericMenuLogic>("Grindstone");
        case MenuType::Smithing: return std::make_unique<GenericMenuLogic>("Smithing");
        case MenuType::Beacon: return std::make_unique<GenericMenuLogic>("Beacon");
        case MenuType::Loom: return std::make_unique<GenericMenuLogic>("Loom");
        case MenuType::Chest: return nullptr; // handled by ClickLogic generic
        case MenuType::Furnace: return nullptr;
        case MenuType::Crafting: return nullptr;
        default: return nullptr;
    }
}

MenuLogic* getMenuLogic(MenuType type) {
    static std::unordered_map<MenuType, std::unique_ptr<MenuLogic>> cache;
    auto it = cache.find(type);
    if (it!=cache.end()) return it->second.get();
    auto ptr = createMenuLogic(type);
    if (!ptr) return nullptr;
    MenuLogic* raw = ptr.get();
    cache.emplace(type, std::move(ptr));
    return raw;
}

} // namespace cppfm
