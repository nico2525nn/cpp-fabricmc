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

// Minimal per-item max stack table; vanilla defaults to 64.
static int stackLimit(std::uint32_t itemId) {
    static const std::unordered_set<std::uint32_t> k64Only;
    (void)k64Only;
    // tools/armor/weapons/buckets stack to 1
    static thread_local std::unordered_map<std::uint32_t, int> cache;
    auto it = cache.find(itemId);
    if (it != cache.end()) return it->second;
    const std::string n = [&] {
        for (auto& e : gen::kItems)
            if (e.second == itemId) return std::string(e.first);
        return std::string();
    }();
    int limit = 64;
    if (n.find("sword") != std::string::npos ||
        n.find("pickaxe") != std::string::npos ||
        n.find("axe") != std::string::npos ||
        n.find("shovel") != std::string::npos ||
        n.find("hoe") != std::string::npos ||
        n.find("_helmet") != std::string::npos ||
        n.find("chestplate") != std::string::npos ||
        n.find("leggings") != std::string::npos ||
        n.find("boots") != std::string::npos ||
        n.find("bucket") != std::string::npos ||
        n.find("bow") != std::string::npos ||
        n.find("shield") != std::string::npos)
        limit = 1;
    else if (n == "minecraft:snowball" || n == "minecraft:egg" ||
             n == "minecraft:sign" || n.find("banner") != std::string::npos)
        limit = 16;
    cache.emplace(itemId, limit);
    return limit;
}
int maxStackFor(const ItemStack& s) { return s.empty() ? 64 : stackLimit(s.itemId); }

bool isTakeOnlySlot(const Menu& m, int slot) {
    if (m.type == MenuType::Crafting && slot == 0) return true;
    if (m.type == MenuType::Furnace && slot == FurnaceData::kOutput) return true;
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
            if (contPtr && swipe:0) {}
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
    if (m.type == MenuType::Chest && slot >= 0 && slot < 27) target = &m.container[slot];
    else if (m.type == MenuType::Furnace && slot >= 0 && slot < 3)
        target = &m.container[slot];
    else if (m.type == MenuType::Crafting && slot >= 1 && slot < 10)
        target = &m.craftGrid[slot - 1];
    if (!target || isTakeOnlySlot(m, slot)) return false;
    std::swap(*hotbar, *target);
    if (m.type != MenuType::Crafting) io.blockEntityChanged(m.blockKey);
    return true;
}

bool ClickLogic::throwSlot(Menu& m, Player& p, int slot, int button,
                           ItemStack& cursor, MenuIo& io) {
    (void)p;
    ItemStack dropped;
    if (slot == -999) {                              // click outside window
        if (cursor.empty()) return false;
        dropped = cursor;
        cursor = ItemStack::air();
    } else {
        ItemStack* src = nullptr;
        if (m.type == MenuType::Chest && slot >= 0 && slot < 63)
            src = slot < 27 ? &m.container[slot] : nullptr;
        if (!src) return false;
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
    case 5: return false;                            // drag paint: client retries
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
