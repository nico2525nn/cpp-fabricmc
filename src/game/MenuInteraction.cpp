// MenuInteraction implementation (vanilla click semantics).
#include "MenuInteraction.hpp"
#include "GameServer.hpp"
#include <unordered_map>
#include <unordered_set>

namespace cppfm {

namespace {

bool sameItemLocal(const ItemStack& a, const ItemStack& b) {
    return !a.empty() && !b.empty() && a.itemId == b.itemId;
}
bool sameItem(const ItemStack& a, const ItemStack& b) {
    return !a.empty() && !b.empty() && a.itemId == b.itemId &&
           a.components.empty() && b.components.empty() &&
           a.removedComponents.empty() && b.removedComponents.empty();
}

} // namespace

// Maps a player-inventory region protocol slot onto Player::inv storage.
static int containerSlotCount(const Menu& m) {
    // crafting and other menus: totalSlots includes container+36
    // For all types totalSlots() = container + 36
    return m.totalSlots() - 36;
}
static ItemStack* playerInvSlot(const Menu& m, int slot, Player& p) {
    int cont = containerSlotCount(m);
    if (slot >= cont && slot < cont + 36) return &p.inv[slot - cont + 9];
    return nullptr;
}

bool isFuelItem(std::uint32_t itemId);

// Adds an ItemStack to the player's inventory (merge then empty slots).
// Returns true when fully absorbed.
static bool addToPlayerInv(Player& p, const ItemStack& stack) {
    if (stack.empty()) return true;
    ItemStack remaining = stack;
    const int limit = maxStackFor(remaining);
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 9; i <= 44; ++i) {
            auto& s = p.inv[i];
            if (pass == 0 && sameItemLocal(s, remaining)) {
                while (s.count < limit && remaining.count > 0) { ++s.count; --remaining.count; }
                if (remaining.count <= 0) return true;
            } else if (pass == 1 && s.empty()) {
                s = remaining;
                remaining = ItemStack::air();
                return true;
            }
        }
    return false;
}

// Minimal per-item max stack table — plan19 §10 accurate via prismarine 1.21.4 (47×16, 203×1)
static int stackLimit(std::uint32_t itemId) {
    static thread_local std::unordered_map<std::uint32_t, int> cache;
    auto it = cache.find(itemId);
    if (it != cache.end()) return it->second;
    const std::string n = [&] {
        for (auto& e : gen::kItems)
            if (e.second == itemId) return std::string(e.first);
        return std::string();
    }();
    if(n.empty()) { cache.emplace(itemId,64); return 64; }
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
    int limit = 64;
    if(k16.find(n)!=k16.end()) limit=16;
    else if(k1.find(n)!=k1.end()) limit=1;
    cache.emplace(itemId, limit);
    return limit;
}
int maxStackFor(const ItemStack& s) { return s.empty() ? 64 : stackLimit(s.itemId); }

bool isTakeOnlySlot(const Menu& m, int slot) {
    if (m.type == MenuType::Crafting && slot == 0) return true;
    if ((m.type == MenuType::Furnace || m.type == MenuType::BlastFurnace || m.type == MenuType::Smoker) && slot == FurnaceData::kOutput) return true;
    if (m.type == MenuType::CartographyTable && slot == 2) return true;
    if (m.type == MenuType::Stonecutter && slot == 1) return true;
    if (m.type == MenuType::Anvil && slot == 2) return true;
    if (m.type == MenuType::Grindstone && slot == 2) return true; // result slot for grindstone
    if (m.type == MenuType::Smithing && slot == 3) return true;
    return false;
}

bool ClickLogic::mergeInto(ItemStack& from, ItemStack& into, int amount) {
    if (from.empty()) return false;
    if (into.empty()) {
        const int take = amount < 0 ? from.count
                                    : std::min<int>(amount, from.count);
        into = from;
        into.count = static_cast<std::int16_t>(take);
        from.count = static_cast<std::int16_t>(from.count - take);
        if (from.count <= 0) from = ItemStack::air();
        return true;
    }
    if (!sameItem(from, into)) return false;
    const int limit = stackLimit(into.itemId);
    if (into.count >= limit) return false;
    const int want = amount < 0 ? from.count : std::min<int>(amount, from.count);
    const int take = std::min(want, limit - into.count);
    if (take <= 0) return false;
    into.count = static_cast<std::int16_t>(into.count + take);
    from.count = static_cast<std::int16_t>(from.count - take);
    if (from.count <= 0) from = ItemStack::air();
    return true;
}

void ClickLogic::craftTaken(Menu& m, const RecipeManager& recipes) {
    m.consumeCraftIngredients();
    m.refreshCraftResult(recipes);
}

bool ClickLogic::pickupPlace(Menu& m, Player&, int slot, int button,
                             ItemStack& cursor, MenuIo& io) {
    ItemStack* target = nullptr;
    if (m.type == MenuType::Crafting) {
        if (slot == 0) return false;                 // handled by caller (result)
        if (slot >= 1 && slot < 10) target = &m.craftGrid[slot - 1];
        else return false;   // player inv via session
        if (!target) return false;
    } else {
        int cont = containerSlotCount(m);
        if (slot < 0 || slot >= cont) return false;
        if (m.container) target = &m.container[slot];
        else target = &m.extraSlots[slot];
        if (m.blockKey >= 0) io.blockEntityChanged(m.blockKey);
    }

    if (isTakeOnlySlot(m, slot)) return false;

    bool changed = false;
    if (button == 0) {                               // left: full swap / merge
        if (sameItem(cursor, *target)) {
            changed = mergeInto(cursor, *target);     // place all cursor onto slot
        } else {
            std::swap(cursor, *target);
            changed = true;
        }
    } else {                                         // right: half-take / place one
        if (cursor.empty()) {
            if (!target->empty()) {
                const int half = (target->count + 1) / 2;
                cursor = *target;
                cursor.count = static_cast<std::int16_t>(half);
                target->count = static_cast<std::int16_t>(target->count - half);
                if (target->count <= 0) *target = ItemStack::air();
                changed = true;
            }
        } else {
            changed = mergeInto(cursor, *target, 1);
        }
    }
    if (changed && m.type == MenuType::Furnace) io.blockEntityChanged(m.blockKey);
    else if (changed && m.blockKey >= 0 && m.type != MenuType::Crafting)
        io.blockEntityChanged(m.blockKey);
    return changed;
}

bool ClickLogic::quickMove(Menu& m, Player& p, const RecipeManager& recipes,
                           int slot, ItemStack& cursor, MenuIo& io) {
    (void)cursor;
    ItemStack* src = nullptr;
    bool fromPlayer = false;
    if (m.type == MenuType::Crafting) {
        if (slot == 0) {                             // craft result shift-click
            int crafted = 0;
            while (!m.craftResult.empty() && crafted < 64) {
                const ItemStack out = m.craftResult;
                if (!addToPlayerInv(p, out)) break;
                io.itemCrafted(p, out);
                m.consumeCraftIngredients();
                m.refreshCraftResult(recipes);
                ++crafted;
            }
            return crafted > 0;
        }
        if (slot >= 1 && slot < 10) src = &m.craftGrid[slot - 1];
        else if (slot >= 10 && slot < 46) src = playerInvSlot(m, slot, p), fromPlayer = true;
    } else {
        int cont = containerSlotCount(m);
        if (slot >= 0 && slot < cont) {
            src = m.container ? &m.container[slot] : &m.extraSlots[slot];
        } else if (slot >= cont && slot < cont + 36) {
            src = playerInvSlot(m, slot, p);
            fromPlayer = true;
        }
    }
    if (!src || src->empty()) return false;

    // destination ranges
    bool moved = false;
    if (fromPlayer) {
        // into container (or furnace special slots)
        if (m.type == MenuType::Furnace) {
            const bool smeltable =
                recipes.findSmelting(src->itemId) != nullptr;
            const bool isFuel = isFuelItem(src->itemId);
            int dstSlot = smeltable ? FurnaceData::kInput
                          : isFuel ? FurnaceData::kFuel : -1;
            if (dstSlot >= 0) {
                ItemStack* dst = m.container ? &m.container[dstSlot] : &m.extraSlots[dstSlot];
                if (mergeInto(*src, *dst)) { moved = true; }
            }
        }
        int cont = containerSlotCount(m);
        ItemStack* contPtr = m.container ? m.container : m.extraSlots;
        // first pass: merge into existing same item
        for (int i = 0; !moved && i < cont; ++i) {
            if (contPtr && &contPtr[i] == src) continue;
            if (contPtr && !contPtr[i].empty() && sameItem(*src, contPtr[i])) {
                moved = mergeInto(*src, contPtr[i]);
            }
        }
        // second pass: any same item (already handled) or any slot
        for (int i = 0; !moved && i < cont; ++i) {
            if (contPtr && &contPtr[i] == src) continue;
            moved = mergeInto(*src, contPtr[i]);
            if (moved) break;
        }
        if (!moved && !src->empty()) {
            // second pass: empty slots
            for (int i = 0; i < cont; ++i) {
                if (contPtr[i].empty()) { contPtr[i] = *src; *src = ItemStack::air(); moved = true; break; }
            }
        }
    } else {
        // container/craft grid -> player inventory (hotbar first like vanilla)
        auto tryRange = [&](std::initializer_list<int> idxs) {
            for (int pass = 0; pass < 2 && !src->empty(); ++pass)
                for (int i : idxs) {
                    auto& dst = p.inv[i];
                    if (pass == 0) { if (mergeInto(*src, dst)) { moved = true; if (src->empty()) return; } }
                    else if (dst.empty()) { dst = *src; *src = ItemStack::air(); moved = true; return; }
                }
        };
        tryRange({36,37,38,39,40,41,42,43,44});
        tryRange({9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,
                  27,28,29,30,31,32,33,34,35});
    }
    if (moved && m.type != MenuType::Crafting) io.blockEntityChanged(m.blockKey);
    return moved;
}

bool ClickLogic::swapWithHotbar(Menu& m, Player& p, int slot, int button,
                                 ItemStack& cursor, MenuIo& io) {
    (void)cursor;
    if (button < 0 || button > 8) return false;
    ItemStack* hotbar = &p.inv[36 + button];
    ItemStack* target = nullptr;
    int cont = containerSlotCount(m);
    if (slot >=0 && slot < cont) {
        if (m.type == MenuType::Crafting) {
            if (slot >=1 && slot <10) target = &m.craftGrid[slot - 1];
        } else {
            target = m.container ? &m.container[slot] : &m.extraSlots[slot];
        }
    } else {
        return false;
    }
    if (!target || isTakeOnlySlot(m, slot)) return false;
    std::swap(*hotbar, *target);
    if (m.type != MenuType::Crafting) io.blockEntityChanged(m.blockKey);
    return true;
}

bool ClickLogic::throwSlot(Menu& m, Player& p, int slot, int button,
                            ItemStack& cursor, MenuIo& io) {
    (void)p;
    ItemStack dropped;
    if (slot == -999) {
        if (cursor.empty()) return false;
        dropped = cursor;
        cursor = ItemStack::air();
    } else {
        ItemStack* src = nullptr;
        int cont = containerSlotCount(m);
        if (slot >=0 && slot < cont) {
            if (m.type == MenuType::Crafting) {
                if (slot==0) return false;
                if (slot>=1 && slot<10) src = &m.craftGrid[slot-1];
            } else {
                src = m.container ? &m.container[slot] : &m.extraSlots[slot];
            }
        } else if (slot >= cont && slot < cont+36) {
            src = m.slotAt(slot, p.inv.data());
        }
        if (!src || src->empty() || isTakeOnlySlot(m, slot)) return false;
        dropped = *src;
        if (button == 1) *src = ItemStack::air();
        else {
            dropped.count = 1;
            if (--src->count <= 0) *src = ItemStack::air();
        }
        if (m.blockKey >= 0) io.blockEntityChanged(m.blockKey);
    }
    io.dropFromPlayer(p, dropped, true);
    return !dropped.empty();
}

bool ClickLogic::pickupAll(Menu& m, Player& p, int slot, ItemStack& cursor,
                           MenuIo& io) {
    (void)p;
    if (cursor.empty()) return false;
    int collected = 0;
    auto gather = [&](ItemStack* arr, int count) {
        for (int i = 0; i < count; ++i) {
            if (sameItem(cursor, arr[i])) {
                while (arr[i].count > 0 &&
                       cursor.count < maxStackFor(cursor)) {
                    --arr[i].count; ++cursor.count; ++collected;
                    if (arr[i].count <= 0) arr[i] = ItemStack::air();
                }
            }
        }
    };
    gather(m.container, m.containerCount);
    for (auto& s : m.craftGrid) (void)s;
    for (auto& s : m.craftGrid) {
        if (sameItem(cursor, s)) {
            while (s.count > 0 && cursor.count < maxStackFor(cursor)) {
                --s.count; ++cursor.count; ++collected;
                if (s.count <= 0) s = ItemStack::air();
            }
        }
    }
    if (collected) io.blockEntityChanged(m.blockKey);
    return collected > 0;
}

bool ClickLogic::apply(Menu& m, Player& p, const RecipeManager& recipes,
                       int clickedSlot, int button, int mode,
                       ItemStack& cursor, MenuIo& io) {
    switch (mode) {
    case 0: {                                        // pickup / place
        // crafting result slot: taking crafts once
        if (m.type == MenuType::Crafting && clickedSlot == 0) {
            if (m.craftResult.empty()) return false;
            if (button == 0 && cursor.empty()) {
                cursor = m.craftResult;
                io.itemCrafted(p, cursor);
                craftTaken(m, recipes);
                return true;
            }
            if (button == 0 && sameItem(cursor, m.craftResult)) {
                if (mergeInto(m.craftResult, cursor)) { io.itemCrafted(p, cursor); craftTaken(m, recipes); return true; }
                return false;
            }
            if (button == 1) {
                if (mergeInto(m.craftResult, cursor, 1)) { io.itemCrafted(p, cursor); craftTaken(m, recipes); return true; }
                return false;
            }
            return false;
        }
        if (clickedSlot == -999) return throwSlot(m, p, clickedSlot, button, cursor, io);
        // furnace output take-only path
        if (m.type == MenuType::Furnace && clickedSlot == FurnaceData::kOutput) {
            ItemStack* out = &m.container[FurnaceData::kOutput];
            if (out->empty()) return false;
            bool took = false;
            if (cursor.empty()) { cursor = *out; *out = ItemStack::air(); took = true; }
            else if (sameItem(cursor, *out)) { took = mergeInto(*out, cursor); }
            else return false;
            if (took) { io.itemSmelted(p, cursor); io.blockEntityChanged(m.blockKey); }
            return took;
        }
        // player-inv region slots route through the same logic using inv array
        ItemStack* invTarget = playerInvSlot(m, clickedSlot, p);
        if (invTarget) {
            bool changed = false;
            if (button == 0) {
                if (sameItem(cursor, *invTarget)) changed = mergeInto(cursor, *invTarget);
                else { std::swap(cursor, *invTarget); changed = true; }
            } else {
                if (cursor.empty()) {
                    if (!invTarget->empty()) {
                        const int half = (invTarget->count + 1) / 2;
                        cursor = *invTarget;
                        cursor.count = static_cast<std::int16_t>(half);
                        invTarget->count = static_cast<std::int16_t>(invTarget->count - half);
                        if (invTarget->count <= 0) *invTarget = ItemStack::air();
                        changed = true;
                    }
                } else changed = mergeInto(cursor, *invTarget, 1);
            }
            return changed;
        }
        return pickupPlace(m, p, clickedSlot, button, cursor, io);
    }
    case 1: return quickMove(m, p, recipes, clickedSlot, cursor, io);
    case 2: return swapWithHotbar(m, p, clickedSlot, button, cursor, io);
    case 3: return false;                            // creative clone: ignore
    case 4: return throwSlot(m, p, clickedSlot, button, cursor, io);
    case 5: { // drag paint: 0/4 start, 1/5 addSlot, 2/6 end
        if (button==0 || button==4) {
            if (cursor.empty()) return false;
            m.dragButton = button;
            m.dragSlots.clear();
            return true;
        } else if (button==1 || button==5) {
            if (m.dragButton==-1) return false;
            if (clickedSlot<0 || clickedSlot>=m.totalSlots()) return false;
            if (isTakeOnlySlot(m, clickedSlot)) return false;
            if (std::find(m.dragSlots.begin(), m.dragSlots.end(), clickedSlot)!=m.dragSlots.end()) return false;
            ItemStack* tgt = m.slotAt(clickedSlot, p.inv.data());
            if (!tgt) tgt = playerInvSlot(m, clickedSlot, p);
            if (!tgt) return false;
            if (!tgt->empty() && !sameItem(cursor, *tgt)) return false;
            if (!tgt->empty() && tgt->count >= maxStackFor(*tgt)) return false;
            m.dragSlots.push_back(clickedSlot);
            return true;
        } else if (button==2 || button==6) {
            if (m.dragButton==-1 || m.dragSlots.empty()) { m.dragButton=-1; m.dragSlots.clear(); return false; }
            bool isRight = (m.dragButton==4);
            bool changed=false;
            bool anyContainerChanged=false;
            if (!isRight) {
                int n = (int)m.dragSlots.size();
                int total = cursor.count;
                int per = total / n;
                int rem = total % n;
                for (int idx : m.dragSlots) {
                    ItemStack* tgt = m.slotAt(idx, p.inv.data());
                    if (!tgt) tgt = playerInvSlot(m, idx, p);
                    if (!tgt) continue;
                    if (!tgt->empty() && !sameItem(cursor, *tgt)) continue;
                    int limit = maxStackFor(cursor);
                    int canPlace = limit - (tgt->empty()?0:tgt->count);
                    if (canPlace<=0) continue;
                    int want = per + (rem>0?1:0);
                    if (rem>0) rem--;
                    want = std::min(want, canPlace);
                    if (want<=0) continue;
                    if (tgt->empty()) { *tgt = cursor; tgt->count = static_cast<int16_t>(want); }
                    else tgt->count = static_cast<int16_t>(tgt->count + want);
                    cursor.count = static_cast<int16_t>(cursor.count - want);
                    if (cursor.count<=0) cursor = ItemStack::air();
                    changed=true;
                    if (idx < m.totalSlots()-36) anyContainerChanged=true;
                }
            } else {
                for (int idx : m.dragSlots) {
                    if (cursor.empty()) break;
                    ItemStack* tgt = m.slotAt(idx, p.inv.data());
                    if (!tgt) tgt = playerInvSlot(m, idx, p);
                    if (!tgt) continue;
                    if (!tgt->empty() && !sameItem(cursor, *tgt)) continue;
                    int limit = maxStackFor(cursor);
                    if (!tgt->empty() && tgt->count >= limit) continue;
                    if (tgt->empty()){ *tgt=cursor; tgt->count=1; cursor.count--; }
                    else { tgt->count++; cursor.count--; }
                    if(cursor.count<=0) cursor = ItemStack::air();
                    changed=true;
                    if (idx < m.totalSlots()-36) anyContainerChanged=true;
                }
            }
            m.dragButton=-1;
            m.dragSlots.clear();
            if (anyContainerChanged) io.blockEntityChanged(m.blockKey);
            return changed;
        }
        return false;
    }
    case 6: return pickupAll(m, p, clickedSlot, cursor, io);
    default: return false;
    }
}

} // namespace cppfm

// ---------------------------------------------------------------- fuel data
namespace cppfm {
bool isFuelItem(std::uint32_t itemId) {
    static const std::unordered_set<std::uint32_t> fuels = [] {
        std::unordered_set<std::uint32_t> s;
        const char* names[] = {
            "minecraft:coal", "minecraft:charcoal",
            "minecraft:coal_block", "minecraft:lava_bucket",
            "minecraft:blaze_rod", "minecraft:dried_kelp_block",
            "minecraft:oak_planks", "minecraft:spruce_planks",
            "minecraft:birch_planks", "minecraft:jungle_planks",
            "minecraft:acacia_planks", "minecraft:dark_oak_planks",
            "minecraft:mangrove_planks", "minecraft:cherry_planks",
            "minecraft:pale_oak_planks", "minecraft:bamboo_planks",
            "minecraft:oak_log", "minecraft:spruce_log", "minecraft:birch_log",
            "minecraft:jungle_log", "minecraft:acacia_log",
            "minecraft:dark_oak_log", "minecraft:stick",
            "minecraft:oak_slab", "minecraft:crafting_table",
            "minecraft:bookshelf", "minecraft:ladder"};
        for (auto* n : names) {
            auto it = gen::itemIdByName().find(n);
            if (it != gen::itemIdByName().end()) s.insert(it->second);
        }
        return s;
    }();
    return fuels.count(itemId) != 0;
}
int furnaceFuelTicks(std::uint32_t itemId) {
    if (itemId == 0) return 0;
    static thread_local int coalId = static_cast<int>(
        gen::itemIdByName().at("minecraft:coal"));
    static thread_local int charcoalId = static_cast<int>(
        gen::itemIdByName().at("minecraft:charcoal"));
    static thread_local int coalBlockId = static_cast<int>(
        gen::itemIdByName().at("minecraft:coal_block"));
    static thread_local int lavaBucketId = static_cast<int>(
        gen::itemIdByName().at("minecraft:lava_bucket"));
    static thread_local int blazeRodId = static_cast<int>(
        gen::itemIdByName().at("minecraft:blaze_rod"));
    static thread_local int stickId = static_cast<int>(
        gen::itemIdByName().at("minecraft:stick"));
    static thread_local int plankTag = 0; (void)plankTag;
    static thread_local int driedKelpId = static_cast<int>(
        gen::itemIdByName().at("minecraft:dried_kelp_block"));
    if (itemId == coalId || itemId == charcoalId) return 1600;
    if (itemId == coalBlockId) return 16000;
    if (itemId == lavaBucketId) return 20000;
    if (itemId == blazeRodId) return 2400;
    if (itemId == driedKelpId) return 4000;
    if (itemId == stickId) return 100;
    // wood-ish items default to 300 ticks
    if (isFuelItem(itemId)) return 300;
    return 0;
}
} // namespace cppfm
