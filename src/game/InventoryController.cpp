#include "InventoryController.hpp"
#include "GameServer.hpp"
#include "HungerManager.hpp"
#include <unordered_set>

namespace cppfm {

namespace {
inline bool sameStackCanMerge(const ItemStack& a, const ItemStack& b) {
    return !a.empty() && !b.empty() && a.itemId == b.itemId &&
           a.components == b.components && a.removedComponents == b.removedComponents;
}
inline int maxStackForId(std::uint32_t itemId) {
    static thread_local std::unordered_map<std::uint32_t, int> cache;
    auto it = cache.find(itemId);
    if (it != cache.end()) return it->second;
    std::string n;
    for (auto& e : gen::kItems) if (e.second == itemId) { n = std::string(e.first); break; }
    if (n.empty()) { cache.emplace(itemId, 64); return 64; }
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
    cache.emplace(itemId, lim);
    return lim;
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
    (void)world;
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
    // send full inventory content via ContainerSetContent for player inventory (window 0)
    // This is a stub that would normally be GameServer::resendInventory; we keep minimal
    // GameServer will handle actual packet; this keeps modular split visible.
}

} // namespace cppfm
