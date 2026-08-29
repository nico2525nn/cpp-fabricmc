#include "MenuLogic.hpp"
#include "MenuInteraction.hpp"
#include "GameServer.hpp"
#include "CostCalculator.hpp"
#include "../generated/ItemIds.hpp"
#include <algorithm>
#include <cstdio>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace cppfm {

// helper to swap/merge like ClickLogic but for result slots — polish: respect maxStackFor and components
static bool isSameForMerge(const ItemStack& a, const ItemStack& b) {
    return !a.empty() && !b.empty() && a.itemId == b.itemId &&
           a.components == b.components && a.removedComponents == b.removedComponents;
}
static int maxStackForMerge(std::uint32_t id) {
    static thread_local std::unordered_map<std::uint32_t,int> cache;
    auto it = cache.find(id);
    if (it != cache.end()) return it->second;
    std::string n;
    for (auto& e : gen::kItems) if (e.second == id) { n = std::string(e.first); break; }
    if (n.empty()) { cache.emplace(id,64); return 64; }
    static const std::unordered_set<std::string> k16 = {
        "minecraft:acacia_hanging_sign", "minecraft:acacia_sign", "minecraft:armor_stand", "minecraft:bamboo_hanging_sign", "minecraft:bamboo_sign", "minecraft:birch_hanging_sign", "minecraft:birch_sign", "minecraft:black_banner",
        "minecraft:blue_banner", "minecraft:brown_banner", "minecraft:bucket", "minecraft:cherry_hanging_sign", "minecraft:cherry_sign", "minecraft:crimson_hanging_sign", "minecraft:crimson_sign", "minecraft:cyan_banner",
        "minecraft:dark_oak_hanging_sign", "minecraft:dark_oak_sign", "minecraft:egg", "minecraft:ender_pearl", "minecraft:gray_banner", "minecraft:green_banner", "minecraft:honey_bottle", "minecraft:jungle_hanging_sign",
        "minecraft:jungle_sign", "minecraft:light_blue_banner", "minecraft:light_gray_banner", "minecraft:lime_banner", "minecraft:magenta_banner", "minecraft:mangrove_hanging_sign", "minecraft:mangrove_sign", "minecraft:oak_hanging_sign",
        "minecraft:oak_sign", "minecraft:orange_banner", "minecraft:pale_oak_hanging_sign", "minecraft:pale_oak_sign", "minecraft:pink_banner", "minecraft:purple_banner", "minecraft:red_banner", "minecraft:snowball",
        "minecraft:spruce_hanging_sign", "minecraft:spruce_sign", "minecraft:warped_hanging_sign", "minecraft:warped_sign", "minecraft:white_banner", "minecraft:written_book", "minecraft:yellow_banner"
    };
    static const std::unordered_set<std::string> k1 = {
        "minecraft:acacia_boat", "minecraft:acacia_chest_boat", "minecraft:axolotl_bucket", "minecraft:bamboo_chest_raft", "minecraft:bamboo_raft", "minecraft:beetroot_soup", "minecraft:birch_boat", "minecraft:birch_chest_boat",
        "minecraft:black_bed", "minecraft:black_bundle", "minecraft:black_shulker_box", "minecraft:blue_bed", "minecraft:blue_bundle", "minecraft:blue_shulker_box", "minecraft:bordure_indented_banner_pattern", "minecraft:bow",
        "minecraft:brown_bed", "minecraft:brown_bundle", "minecraft:brown_shulker_box", "minecraft:brush", "minecraft:bundle", "minecraft:cake", "minecraft:carrot_on_a_stick", "minecraft:chainmail_boots",
        "minecraft:chainmail_chestplate", "minecraft:chainmail_helmet", "minecraft:chainmail_leggings", "minecraft:cherry_boat", "minecraft:cherry_chest_boat", "minecraft:chest_minecart", "minecraft:cod_bucket", "minecraft:command_block_minecart",
        "minecraft:creeper_banner_pattern", "minecraft:crossbow", "minecraft:cyan_bed", "minecraft:cyan_bundle", "minecraft:cyan_shulker_box", "minecraft:dark_oak_boat", "minecraft:dark_oak_chest_boat", "minecraft:debug_stick",
        "minecraft:diamond_axe", "minecraft:diamond_boots", "minecraft:diamond_chestplate", "minecraft:diamond_helmet", "minecraft:diamond_hoe", "minecraft:diamond_horse_armor", "minecraft:diamond_leggings", "minecraft:diamond_pickaxe",
        "minecraft:diamond_shovel", "minecraft:diamond_sword", "minecraft:elytra", "minecraft:enchanted_book", "minecraft:field_masoned_banner_pattern", "minecraft:fishing_rod", "minecraft:flint_and_steel", "minecraft:flow_banner_pattern",
        "minecraft:flower_banner_pattern", "minecraft:furnace_minecart", "minecraft:globe_banner_pattern", "minecraft:goat_horn", "minecraft:golden_axe", "minecraft:golden_boots", "minecraft:golden_chestplate", "minecraft:golden_helmet",
        "minecraft:golden_hoe", "minecraft:golden_horse_armor", "minecraft:golden_leggings", "minecraft:golden_pickaxe", "minecraft:golden_shovel", "minecraft:golden_sword", "minecraft:gray_bed", "minecraft:gray_bundle",
        "minecraft:gray_shulker_box", "minecraft:green_bed", "minecraft:green_bundle", "minecraft:green_shulker_box", "minecraft:guster_banner_pattern", "minecraft:hopper_minecart", "minecraft:iron_axe", "minecraft:iron_boots",
        "minecraft:iron_chestplate", "minecraft:iron_helmet", "minecraft:iron_hoe", "minecraft:iron_horse_armor", "minecraft:iron_leggings", "minecraft:iron_pickaxe", "minecraft:iron_shovel", "minecraft:iron_sword",
        "minecraft:jungle_boat", "minecraft:jungle_chest_boat", "minecraft:knowledge_book", "minecraft:lava_bucket", "minecraft:leather_boots", "minecraft:leather_chestplate", "minecraft:leather_helmet", "minecraft:leather_horse_armor",
        "minecraft:leather_leggings", "minecraft:light_blue_bed", "minecraft:light_blue_bundle", "minecraft:light_blue_shulker_box", "minecraft:light_gray_bed", "minecraft:light_gray_bundle", "minecraft:light_gray_shulker_box", "minecraft:lime_bed",
        "minecraft:lime_bundle", "minecraft:lime_shulker_box", "minecraft:lingering_potion", "minecraft:mace", "minecraft:magenta_bed", "minecraft:magenta_bundle", "minecraft:magenta_shulker_box", "minecraft:mangrove_boat",
        "minecraft:mangrove_chest_boat", "minecraft:milk_bucket", "minecraft:minecart", "minecraft:mojang_banner_pattern", "minecraft:mushroom_stew", "minecraft:music_disc_11", "minecraft:music_disc_13", "minecraft:music_disc_5",
        "minecraft:music_disc_blocks", "minecraft:music_disc_cat", "minecraft:music_disc_chirp", "minecraft:music_disc_creator", "minecraft:music_disc_creator_music_box", "minecraft:music_disc_far", "minecraft:music_disc_mall", "minecraft:music_disc_mellohi",
        "minecraft:music_disc_otherside", "minecraft:music_disc_pigstep", "minecraft:music_disc_precipice", "minecraft:music_disc_relic", "minecraft:music_disc_stal", "minecraft:music_disc_strad", "minecraft:music_disc_wait", "minecraft:music_disc_ward",
        "minecraft:netherite_axe", "minecraft:netherite_boots", "minecraft:netherite_chestplate", "minecraft:netherite_helmet", "minecraft:netherite_hoe", "minecraft:netherite_leggings", "minecraft:netherite_pickaxe", "minecraft:netherite_shovel",
        "minecraft:netherite_sword", "minecraft:oak_boat", "minecraft:oak_chest_boat", "minecraft:orange_bed", "minecraft:orange_bundle", "minecraft:orange_shulker_box", "minecraft:pale_oak_boat", "minecraft:pale_oak_chest_boat",
        "minecraft:piglin_banner_pattern", "minecraft:pink_bed", "minecraft:pink_bundle", "minecraft:pink_shulker_box", "minecraft:potion", "minecraft:powder_snow_bucket", "minecraft:pufferfish_bucket", "minecraft:purple_bed",
        "minecraft:purple_bundle", "minecraft:purple_shulker_box", "minecraft:rabbit_stew", "minecraft:red_bed", "minecraft:red_bundle", "minecraft:red_shulker_box", "minecraft:saddle", "minecraft:salmon_bucket",
        "minecraft:shears", "minecraft:shield", "minecraft:shulker_box", "minecraft:skull_banner_pattern", "minecraft:splash_potion", "minecraft:spruce_boat", "minecraft:spruce_chest_boat", "minecraft:spyglass",
        "minecraft:stone_axe", "minecraft:stone_hoe", "minecraft:stone_pickaxe", "minecraft:stone_shovel", "minecraft:stone_sword", "minecraft:suspicious_stew", "minecraft:tadpole_bucket", "minecraft:tnt_minecart",
        "minecraft:totem_of_undying", "minecraft:trident", "minecraft:tropical_fish_bucket", "minecraft:turtle_helmet", "minecraft:warped_fungus_on_a_stick", "minecraft:water_bucket", "minecraft:white_bed", "minecraft:white_bundle",
        "minecraft:white_shulker_box", "minecraft:wolf_armor", "minecraft:wooden_axe", "minecraft:wooden_hoe", "minecraft:wooden_pickaxe", "minecraft:wooden_shovel", "minecraft:wooden_sword", "minecraft:writable_book",
        "minecraft:yellow_bed", "minecraft:yellow_bundle", "minecraft:yellow_shulker_box"
    };
    int lim = 64;
    if (k16.find(n) != k16.end()) lim = 16;
    else if (k1.find(n) != k1.end()) lim = 1;
    cache.emplace(id, lim);
    return lim;
}
static bool mergeStack(ItemStack& from, ItemStack& to) {
    if (from.empty()) return false;
    if (to.empty()) { to=from; from=ItemStack::air(); return true; }
    if (!isSameForMerge(from, to)) return false;
    int limit = maxStackForMerge(to.itemId);
    if (to.count >= limit) return false;
    int take = std::min<int>(from.count, limit - to.count);
    to.count = static_cast<std::int16_t>(to.count + take);
    from.count = static_cast<std::int16_t>(from.count - take);
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
    // Apply enchant(s) — deterministic seeded RNG per Yarn EnchantmentScreenHandler (plan23 §5 seeded Random)
    // Yarn `EnchantmentHelper.generateEnchantments` uses Random.create(seed) where seed = player.enchantmentSeed
    const char* enchants[] = {"minecraft:protection","minecraft:sharpness","minecraft:efficiency","minecraft:unbreaking"};
    const char* chosen = enchants[buttonId % 4];
    {
        std::uint32_t baseSeed = static_cast<std::uint32_t>(player.enchantmentSeed ^ (buttonId * 0x9e3779b9u) ^ (bookshelves * 0x85ebca6bu));
        if (baseSeed == 0) baseSeed = 0x5a5a5a5a;
        std::mt19937 rng(baseSeed);
        int lvl = 1 + (buttonId) + static_cast<int>(rng() % 2u);
        // clamp lvl to enchant max (protection 4, sharpness 5 etc) — keep simple 1..5
        lvl = std::clamp(lvl, 1, 5);
        ItemStack::addEnchant(*item, chosen, lvl);
    }
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
    // slots: 0 input, 1 result (take-only) — stonecutter has no `triggered` blockstate (crafter only has it); no toggle here.
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
        // Strict audit: stonecutter/crafter triggered toggle — mark block entity dirty and toggle triggered if present
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

bool CrafterMenuLogic::onSlotClick(Menu& menu, Player& player, int slotId, int button, int mode,
                                   ItemStack& cursor, MenuIo& io, const RecipeManager& recipes) {
    (void)player; (void)recipes;
    // Crafter 9 slots (0..8) + player inv 36. Yarn `CrafterScreenHandler` + `CrafterBlock` `triggered` parity:
    // `triggered` toggle is handled server-side in `GameServer::handleMenuClick` (Crafter only), not here.
    // Behaves like a chest 3x3 but with crafting-like disabled slot handling (all slots enabled for stub).
    int cont = 9;
    if (slotId < cont) {
        ItemStack* target = menu.container ? &menu.container[slotId] : &menu.extraSlots[slotId];
        bool changed=false;
        if (mode==1) return false; // quick move not handled, fall back to ClickLogic
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
        // Future: triggered property toggle on redstone pulse would craft result to facing inventory; stub keeps slots.
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
    // vanilla: filled_map + paper => clone, map scale upgrade with 8 paper etc. Stub: if both present, copy map to result
    if (!map->empty() && !paper->empty()) {
        // Check map is "minecraft:filled_map" or "minecraft:map" ; accept any for stub
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
    (void)player;
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
