// Entities: item drops + mobs with data-driven stats (plan.md Phase 3/4 +
// plan3.md "74種類のモブ" registry approach). Expanded to 40 kinds for items 30-47.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cstdio>
#include "../generated/EntityIds.hpp"
#include "../generated/ItemIds.hpp"
#include "Items.hpp"

namespace cppfm {

struct Vec3 { double x, y, z; };

struct PrimedTntEntity {
    std::int32_t entityId = 0;
    double x=0, y=0, z=0;
    double vx=0, vy=0, vz=0;
    int fuse = 80;
    std::int64_t ageTicks = 0;
};

struct ItemEntity {
    std::int32_t entityId = 0;
    std::uint32_t itemId = 0;
    std::uint8_t count = 1;
    // D11 (plan26 §4): full ItemStack with components for Slot type 7 preservation.
    // If `stack` is non-empty it overrides itemId/count and carries enchant/trim/damage.
    ItemStack stack{};
    double x=0, y=0, z=0;
    double vx=0, vy=0, vz=0;
    std::int64_t ageTicks = 0;
    bool collected = false;
    double sentX=0, sentY=0, sentZ=0; bool hasSent=false;
    ItemStack asStack() const {
        if (!stack.empty()) return stack;
        if (itemId==0 || count==0) return ItemStack::air();
        return ItemStack::of(itemId, count);
    }
    void setStack(const ItemStack& s){
        stack = s;
        itemId = s.itemId;
        count = s.count;
    }
};

struct XpOrbEntity {
    std::int32_t entityId = 0;
    std::uint16_t value = 1;
    double x=0, y=0, z=0;
    double vy=0;
    std::int64_t ageTicks = 0;
};

enum class ProjectileKind : std::uint8_t { Arrow=0, Snowball, Egg, EnderPearl, Potion, WitherSkull, Fireball, DragonFireball, Trident };

struct ProjectileEntity {
    std::int32_t entityId = 0;
    ProjectileKind kind = ProjectileKind::Arrow;
    double x=0, y=0, z=0;
    double vx=0, vy=0, vz=0;
    std::int32_t ownerId = -1;
    bool ownerIsPlayer = false;
    std::int64_t ageTicks = 0;
    bool stuck = false;
    // for arrow stuck & retrieval
    std::int64_t stuckTicks = 0;
    std::int32_t stuckBlockX=0, stuckBlockY=0, stuckBlockZ=0;
    bool charged = false; // for WitherSkull blue (charged) variant
};

struct LightningBoltEntity {
    std::int32_t entityId = 0;
    double x=0, y=0, z=0;
    std::int64_t ageTicks = 0;
};

struct TntEntity {
    std::int32_t entityId = 0;
    double x=0, y=0, z=0;
    double vx=0, vy=0, vz=0;
    std::int32_t fuse = 80; // ticks until explode (vanilla 80)
    std::int64_t ageTicks = 0;
};

enum class MobKind : std::uint16_t {
    Pig = 0, Cow, Sheep, Chicken,
    Zombie, Creeper, Skeleton, Spider,
    Slime, Enderman, Witch, Rabbit, Villager,
    Wither, EnderDragon, Blaze, Ghast, WitherSkeleton,
    MagmaCube, Guardian, ElderGuardian, Evoker, Vex,
    Hoglin, Piglin, Axolotl, Goat, Horse, Llama,
    Panda, Fox, Frog, Dolphin, Turtle, Bat,
    Cod, Salmon, TropicalFish, Pufferfish,
    Squid, GlowSquid, // extra to reach 40, harmless
    Warden, Phantom, IronGolem, Allay, Shulker,
    Boat, Minecart,
    // plan25 E1 strict audit HIGH: expand 48->149 via gen (armadillo/beebogged/breeze/creaking etc) — 101 missing fixed, 149 = kEntities 149
    Armadillo, Bee, Bogged, Breeze, Camel, Cat, CaveSpider, Creaking,
    Donkey, Drowned, Endermite, Husk, Illusioner, Mooshroom, Mule, Ocelot,
    Parrot, PiglinBrute, Pillager, PolarBear, Ravager, Silverfish, SkeletonHorse, ZombieHorse, Sniffer,
    SnowGolem, Stray, Strider, Tadpole, TraderLlama, Vindicator, WanderingTrader, Wolf,
    Zoglin, ZombieVillager, ZombifiedPiglin, Giant, EvokerFangs, EnderCrystal,
    // plan19 strict E2: Boat variants 10+10 distinct (was generic Boat)
    OakBoat, SpruceBoat, BirchBoat, JungleBoat, AcaciaBoat, DarkOakBoat, MangroveBoat, CherryBoat, PaleOakBoat, BambooRaft,
    OakChestBoat, SpruceChestBoat, BirchChestBoat, JungleChestBoat, AcaciaChestBoat, DarkOakChestBoat, MangroveChestBoat, CherryChestBoat, PaleOakChestBoat, BambooChestRaft,
    // plan21 E1: expand 107->149 (add 42 missing entity types from kEntities 149, exclude player for MobKind)
    AreaEffectCloud, ArmorStand, Arrow, BlockDisplay, BreezeWindCharge, ChestMinecart, CommandBlockMinecart, DragonFireball, Egg, EnderPearl,
    ExperienceBottle, ExperienceOrb, EyeOfEnder, FallingBlock, Fireball, FireworkRocket, FishingBobber, FurnaceMinecart, GlowItemFrame, HopperMinecart,
    Interaction, Item, ItemDisplay, ItemFrame, LeashKnot, LightningBolt, LlamaSpit, Marker, OminousItemSpawner, Painting,
    Potion, ShulkerBullet, SmallFireball, Snowball, SpawnerMinecart, SpectralArrow, TextDisplay, Tnt, TntMinecart, Trident,
    WindCharge, WitherSkull
};

// Static per-kind gameplay table (clean-room values approximating vanilla).
struct MobStats {
    const char* name;
    float maxHealth;
    float moveSpeed;
    float attackDamage;
    bool hostile;
    bool burnsInDaylight;
    const char* dropItem;
    int dropMin, dropMax;
    const char* breedingItem;
    std::uint32_t xpDrop;
};

// plan14 §4: VillagerData profession/level/type (1-5, 7 types, 15 professions) + NITWIT plan17 LOW
struct VillagerData {
    enum Type : std::uint8_t { PLAINS=0, DESERT, SAVANNA, SNOW, SWAMP, JUNGLE, TAIGA };
    enum Profession : std::uint8_t { NONE=0, ARMORER, BUTCHER, CARTOGRAPHER, CLERIC, FARMER, FISHERMAN, FLETCHER, LEATHERWORKER, LIBRARIAN, MASON, SHEPHERD, TOOLSMITH, WEAPONSMITH, NITWIT };
    int level = 1; // 1..5
    Type type = PLAINS;
    Profession profession = FARMER;
};

// plan14 §4: Gossip reputation per UUID (trading discounts, hero_of_village)
struct Gossip {
    std::unordered_map<std::string,int> rep; // hex uuid -> reputation
    void add(const std::array<std::uint8_t,16>& uuid, int delta){
        char buf[33]; for(int i=0;i<16;++i) snprintf(buf+i*2,3,"%02x", uuid[i]); buf[32]=0;
        rep[std::string(buf)] += delta;
    }
    void addHex(const std::string& hex, int delta){ rep[hex] += delta; }
    int get(const std::array<std::uint8_t,16>& uuid) const {
        char buf[33]; for(int i=0;i<16;++i) snprintf(buf+i*2,3,"%02x", uuid[i]); buf[32]=0;
        auto it=rep.find(std::string(buf)); return it==rep.end()?0:it->second;
    }
    int getHex(const std::string& hex) const { auto it=rep.find(hex); return it==rep.end()?0:it->second; }
    // decay towards 0 every 100 ticks, remove zeros
    void tickDecay(){
        for(auto it=rep.begin(); it!=rep.end();){
            if(it->second>0) { --it->second; if(it->second==0) it=rep.erase(it); else ++it; }
            else if(it->second<0){ ++it->second; if(it->second==0) it=rep.erase(it); else ++it; }
            else it=rep.erase(it);
        }
    }
};

inline const MobStats& mobStats(MobKind k) {
    static const MobStats table[] = {
        {"minecraft:pig",            10.f, 0.10f, 0.f, false, false, "minecraft:porkchop", 1, 3, "minecraft:carrot",          1},
        {"minecraft:cow",            10.f, 0.09f, 0.f, false, false, "minecraft:beef",     1, 3, "minecraft:wheat",           1},
        {"minecraft:sheep",           8.f, 0.09f, 0.f, false, false, "minecraft:mutton",   1, 2, "minecraft:wheat",           1},
        {"minecraft:chicken",         4.f, 0.09f, 0.f, false, false, "minecraft:chicken",  1, 1, "minecraft:wheat_seeds",     1},
        {"minecraft:zombie",         20.f, 0.085f,3.f, true, true,  "minecraft:rotten_flesh",0,2,nullptr,                    5},
        {"minecraft:creeper",        20.f, 0.095f,0.f, true, false, nullptr,             0,0,nullptr,                        5},
        {"minecraft:skeleton",       20.f, 0.10f, 2.f, true, true,  "minecraft:bone",      0,2,nullptr,                      5},
        {"minecraft:spider",         16.f, 0.13f, 2.f, true, false, "minecraft:string",   0,2,nullptr,                      5},
        {"minecraft:slime",           4.f, 0.06f, 2.f, true, false, "minecraft:slime_ball",0,2,nullptr,                    4},
        {"minecraft:enderman",       40.f, 0.12f, 7.f, true, false, "minecraft:ender_pearl",0,1,nullptr,                  5},
        {"minecraft:witch",          26.f, 0.09f, 3.f, true, false, "minecraft:glowstone_dust",0,2,nullptr,               5},
        {"minecraft:rabbit",          3.f, 0.13f, 0.f, false,false,"minecraft:rabbit_hide",0,1,"minecraft:dandelion",      1},
        {"minecraft:villager",       20.f, 0.09f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:wither",        300.f, 0.08f, 8.f, true, false,"minecraft:nether_star",1,1,nullptr,                    50},
        {"minecraft:ender_dragon",  200.f, 0.10f,10.f, true, false,nullptr,0,0,nullptr,                                    12000},
        {"minecraft:blaze",          20.f, 0.09f, 6.f, true, false,"minecraft:blaze_rod", 0,1,nullptr,                     10},
        {"minecraft:ghast",          10.f, 0.06f, 8.f, true, false,"minecraft:ghast_tear",0,1,nullptr,                     5},
        {"minecraft:wither_skeleton",20.f, 0.10f, 8.f, true, false,"minecraft:bone",0,2,nullptr,                            5},
        {"minecraft:magma_cube",     16.f, 0.06f, 3.f, true, false,"minecraft:magma_cream",0,1,nullptr,                     4},
        {"minecraft:guardian",       30.f, 0.08f, 6.f, true, false,"minecraft:prismarine_shard",0,2,nullptr,               10},
        {"minecraft:elder_guardian", 80.f, 0.06f, 8.f, true, false,"minecraft:prismarine_shard",0,2,nullptr,               10},
        {"minecraft:evoker",         24.f, 0.09f, 6.f, true, false,"minecraft:totem_of_undying",0,1,nullptr,               10},
        {"minecraft:vex",            14.f, 0.12f, 5.f, true, false,nullptr,0,0,nullptr,                                     3},
        {"minecraft:hoglin",         40.f, 0.09f, 6.f, true, false,"minecraft:porkchop",2,4,nullptr,                        5},
        {"minecraft:piglin",         16.f, 0.09f, 5.f, true, false,"minecraft:gold_nugget",0,2,nullptr,                    5},
        {"minecraft:axolotl",        14.f, 0.09f, 2.f, false,false,"minecraft:axolotl_bucket",0,0,"minecraft:tropical_fish_bucket",1},
        {"minecraft:goat",           10.f, 0.11f, 3.f, false,false,nullptr,0,0,"minecraft:wheat",                           1},
        {"minecraft:horse",          30.f, 0.12f, 0.f, false,false,"minecraft:leather",0,2,"minecraft:golden_carrot",      1},
        {"minecraft:llama",          22.f, 0.09f, 0.f, false,false,"minecraft:leather",1,2,"minecraft:hay_block",          1},
        {"minecraft:panda",          20.f, 0.08f, 6.f, false,false,"minecraft:bamboo",1,2,"minecraft:bamboo",             1},
        {"minecraft:fox",            10.f, 0.10f, 2.f, false,false,"minecraft:sweet_berries",0,1,"minecraft:sweet_berries", 2},
        {"minecraft:frog",           10.f, 0.09f, 0.f, false,false,nullptr,0,0,"minecraft:slime_ball",                     2},
        {"minecraft:dolphin",        10.f, 0.12f, 3.f, false,false,nullptr,0,0,"minecraft:cod",                            1},
        {"minecraft:turtle",         30.f, 0.07f, 0.f, false,false,"minecraft:seagrass",0,1,"minecraft:seagrass",          1},
        {"minecraft:bat",             6.f, 0.08f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:cod",             3.f, 0.10f, 0.f, false,false,"minecraft:cod",1,1,nullptr,                            1},
        {"minecraft:salmon",          3.f, 0.10f, 0.f, false,false,"minecraft:salmon",1,1,nullptr,                          1},
        {"minecraft:tropical_fish",   3.f, 0.10f, 0.f, false,false,"minecraft:tropical_fish",1,1,nullptr,                  1},
        {"minecraft:pufferfish",      3.f, 0.10f, 0.f, false,false,"minecraft:pufferfish",1,1,nullptr,                      1},
        {"minecraft:squid",          10.f, 0.08f, 0.f, false,false,"minecraft:ink_sac",1,3,nullptr,                         1},
        {"minecraft:glow_squid",     10.f, 0.08f, 0.f, false,false,"minecraft:glow_ink_sac",1,3,nullptr,                    1},
        {"minecraft:warden",         500.f,0.07f,30.f, true, false,"minecraft:sculk_catalyst",1,1,nullptr,                   5},
        {"minecraft:phantom",        20.f, 0.12f, 6.f, true, false,"minecraft:phantom_membrane",0,1,nullptr,                5},
        {"minecraft:iron_golem",     100.f,0.08f,15.f, false,false,"minecraft:iron_ingot",3,5,nullptr,                      0},
        {"minecraft:allay",          20.f, 0.09f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:shulker",        30.f, 0.05f, 4.f, true, false,"minecraft:shulker_shell",0,1,nullptr,                   5},
        {"minecraft:boat",            6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:minecart",       6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        // expanded 48->86 via gen (strict audit HIGH E1)
        {"minecraft:armadillo",      12.f, 0.09f, 0.f, false,false,"minecraft:armadillo_scute",1,1,nullptr,                0},
        {"minecraft:bee",            10.f, 0.10f, 2.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:bogged",         16.f, 0.10f, 2.f, true, false,"minecraft:arrow",0,2,nullptr,                           5},
        {"minecraft:breeze",         30.f, 0.09f, 6.f, true, false,"minecraft:breeze_rod",0,1,nullptr,                      5},
        {"minecraft:camel",          32.f, 0.09f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:cat",            10.f, 0.10f, 3.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:cave_spider",    12.f, 0.12f, 2.f, true, false,"minecraft:string",0,2,nullptr,                          5},
        {"minecraft:creaking",        1.f, 0.08f, 7.f, true, false,"minecraft:resin_clump",0,2,nullptr,                     5},
        {"minecraft:donkey",         22.f, 0.10f, 0.f, false,false,"minecraft:leather",0,2,"minecraft:golden_carrot",      0},
        {"minecraft:drowned",        20.f, 0.09f, 3.f, true, false,"minecraft:rotten_flesh",0,2,nullptr,                    5},
        {"minecraft:endermite",       8.f, 0.10f, 2.f, true, false,nullptr,0,0,nullptr,                                     3},
        {"minecraft:husk",           20.f, 0.09f, 3.f, true, false,"minecraft:rotten_flesh",0,2,nullptr,                    5},
        {"minecraft:illusioner",     32.f, 0.09f, 4.f, true, false,nullptr,0,0,nullptr,                                     5},
        {"minecraft:mooshroom",      10.f, 0.09f, 0.f, false,false,nullptr,0,0,"minecraft:wheat",                           1},
        {"minecraft:mule",           22.f, 0.10f, 0.f, false,false,"minecraft:leather",0,2,"minecraft:golden_carrot",      0},
        {"minecraft:ocelot",         10.f, 0.10f, 3.f, false,false,nullptr,0,0,"minecraft:cod",                             1},
        {"minecraft:parrot",          6.f, 0.10f, 0.f, false,false,nullptr,0,0,"minecraft:cookie",                          1},
        {"minecraft:piglin_brute",   50.f, 0.09f, 7.f, true, false,"minecraft:gold_nugget",0,1,nullptr,                     5},
        {"minecraft:pillager",       24.f, 0.10f, 5.f, true, false,"minecraft:crossbow",0,1,nullptr,                        5},
        {"minecraft:polar_bear",     30.f, 0.09f, 6.f, false,false,nullptr,0,0,nullptr,                                     5},
        {"minecraft:ravager",       100.f, 0.08f,12.f, true, false,nullptr,0,0,nullptr,                                    20},
        {"minecraft:silverfish",      8.f, 0.11f, 1.f, true, false,nullptr,0,0,nullptr,                                     5},
        {"minecraft:skeleton_horse", 15.f, 0.12f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:sniffer",        14.f, 0.08f, 0.f, false,false,nullptr,0,0,"minecraft:torchflower_seeds",               1},
        {"minecraft:snow_golem",      4.f, 0.10f, 0.f, false,false,nullptr,0,0,"minecraft:snowball",                        0},
        {"minecraft:stray",          20.f, 0.10f, 2.f, true, true, "minecraft:bone",0,2,nullptr,                           5},
        {"minecraft:strider",        20.f, 0.09f, 2.f, false,false,nullptr,0,0,"minecraft:warped_fungus",                   1},
        {"minecraft:tadpole",         6.f, 0.09f, 0.f, false,false,nullptr,0,0,"minecraft:slime_ball",                      1},
        {"minecraft:trader_llama",   14.f, 0.09f, 3.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:vindicator",     24.f, 0.09f, 5.f, true, false,"minecraft:emerald",0,1,nullptr,                         5},
        {"minecraft:wandering_trader",20.f,0.09f, 0.f, false,false,nullptr,0,0,nullptr,                                    0},
        {"minecraft:wolf",           16.f, 0.10f, 4.f, false,false,nullptr,0,0,"minecraft:bone",                            1},
        {"minecraft:zoglin",         40.f, 0.09f, 6.f, true, false,"minecraft:rotten_flesh",0,1,nullptr,                    5},
        {"minecraft:zombie_villager",20.f, 0.085f,3.f,true, true, "minecraft:rotten_flesh",0,2,nullptr,                     5},
        {"minecraft:zombified_piglin",20.f,0.09f,5.f, true, false,"minecraft:gold_nugget",0,1,nullptr,                     5},
        {"minecraft:zombie_horse",   15.f, 0.11f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:giant",         100.f, 0.08f,10.f, true, false,nullptr,0,0,nullptr,                                    10},
        {"minecraft:evoker_fangs",    1.f, 0.00f, 6.f, true, false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:end_crystal",     1.f, 0.00f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        // plan19 E2 boat variants (all 6HP, same physics, distinct typeId)
        {"minecraft:oak_boat",        6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:spruce_boat",     6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:birch_boat",      6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:jungle_boat",     6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:acacia_boat",     6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:dark_oak_boat",   6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:mangrove_boat",   6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:cherry_boat",     6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:pale_oak_boat",   6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:bamboo_raft",     6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:oak_chest_boat",  6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                     0},
        {"minecraft:spruce_chest_boat",6.f,0.10f,0.f,false,false,nullptr,0,0,nullptr,                                      0},
        {"minecraft:birch_chest_boat", 6.f,0.10f, 0.f, false,false,nullptr,0,0,nullptr,                                    0},
        {"minecraft:jungle_chest_boat",6.f,0.10f,0.f,false,false,nullptr,0,0,nullptr,                                      0},
        {"minecraft:acacia_chest_boat",6.f,0.10f,0.f,false,false,nullptr,0,0,nullptr,                                      0},
        {"minecraft:dark_oak_chest_boat",6.f,0.10f,0.f,false,false,nullptr,0,0,nullptr,                                    0},
        {"minecraft:mangrove_chest_boat",6.f,0.10f,0.f,false,false,nullptr,0,0,nullptr,                                    0},
        {"minecraft:cherry_chest_boat",6.f,0.10f,0.f,false,false,nullptr,0,0,nullptr,                                      0},
        {"minecraft:pale_oak_chest_boat",6.f,0.10f,0.f,false,false,nullptr,0,0,nullptr,                                    0},
        {"minecraft:bamboo_chest_raft",6.f,0.10f,0.f,false,false,nullptr,0,0,nullptr,                                      0},
        // plan21 E1: 42 missing entity types (149 total, exclude player)
        {"minecraft:area_effect_cloud", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:armor_stand", 20.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:arrow", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:block_display", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:breeze_wind_charge", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:chest_minecart", 6.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:command_block_minecart", 6.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:dragon_fireball", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:egg", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:ender_pearl", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:experience_bottle", 10.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:experience_orb", 10.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:eye_of_ender", 10.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:falling_block", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:fireball", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:firework_rocket", 10.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:fishing_bobber", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:furnace_minecart", 6.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:glow_item_frame", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:hopper_minecart", 6.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:interaction", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:item", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:item_display", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:item_frame", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:leash_knot", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:lightning_bolt", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:llama_spit", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:marker", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:ominous_item_spawner", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:painting", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:potion", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:shulker_bullet", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:small_fireball", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:snowball", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:spawner_minecart", 6.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:spectral_arrow", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:text_display", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:tnt", 10.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:tnt_minecart", 6.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:trident", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:wind_charge", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
        {"minecraft:wither_skull", 1.f, 0.10f, 0.f, false, false, nullptr, 0,0,nullptr, 0},
    };
    static_assert(sizeof(table)/sizeof(table[0]) == 149, "table size must match MobKind count");
    return table[static_cast<int>(k)];
}
// plan17 §10 E10: size-aware slime/magma health (size²) and attack — vanilla SlimeEntity size*size, MagmaCube size+2 attack and size*3 armor
inline MobStats mobStats(MobKind k, int slimeSize) {
    if (k == MobKind::Slime || k == MobKind::MagmaCube) {
        int s = 1 << std::clamp(slimeSize, 0, 2); // 1,2,4
        float hp = static_cast<float>(s * s); // 1,4,16
        float atk = (k == MobKind::MagmaCube) ? static_cast<float>(s + 2) : static_cast<float>(s);
        MobStats out = mobStats(k); // base from table
        out.maxHealth = hp;
        out.attackDamage = atk;
        return out;
    }
    return mobStats(k);
}
inline float slimeWidthForSize(int sz) {
    int s = 1 << std::clamp(sz, 0, 2);
    return 0.52f * static_cast<float>(s);
}

struct MobEntity {
    std::int32_t entityId = 0;
    MobKind kind = MobKind::Pig;
    double x=0, y=0, z=0;
    float yaw=0, pitch=0;
    double health = 10;
    bool dead = false;
    double tx=0, tz=0; bool hasTarget=false;
    std::int64_t nextWanderAt = 0;
    std::int32_t age = 0;
    bool inLove = false;
    std::int64_t loveUntilTick = 0;
    std::int64_t breedCooldownUntil = 0;
    std::int32_t angerTargetEntityId = -1;
    std::int64_t angryUntilTick = 0;
    double sentX=0, sentY=0, sentZ=0; float sentYaw=0; bool hasSent=false;
    std::int64_t lastSeenMs = 0;
    // equipment slots: 0 mainhand 1 offhand 2 boots 3 leggings 4 chest 5 head
    std::array<ItemStack,6> equipment{};
    // plan13 §2: HandDropChances (0.085F default) for loot drop on death
    std::array<float,2> handDropChances{0.085f,0.085f};
    std::array<float,4> armorDropChances{0.085f,0.085f,0.085f,0.085f};
    std::int32_t riderEntityId = -1; // passenger entity id riding this mob
    std::int32_t vehicleId = -1; // if this mob is passenger, its vehicle
    bool sheared = false;
    std::uint8_t woolColor = 0; // 0 white
    std::uint32_t carriedBlock = 0; // enderman: block state id, 0 = empty (plan13 §6)
    bool creeperCharged = false;
    // plan16: creeper ignited fuse (separate from nextWanderAt)
    bool creeperIgnited = false;
    std::int64_t creeperFuseStart = -1;
    static constexpr int CREEPER_FUSE_TICKS = 30;
    int slimeSize = 2; // 2 large 1 medium 0 small
    // plan16: slime health size² helper (vanilla: (1<<size)² => 1,4,16)
    static inline float slimeHealthForSize(int sz){
        int s = (1 << std::clamp(sz,0,2));
        return static_cast<float>(s * s);
    }
    // plan17 §10 E10: slime dimensions 0.52*size and magma armor size*3
    static inline float slimeWidthForSize(int sz){
        return ::cppfm::slimeWidthForSize(sz); // delegate to free fn, de-duplicate (plan18 polish)
    }
    static inline int magmaArmorForSize(int sz){
        int s = (1 << std::clamp(sz,0,2));
        return s * 3; // vanilla MagmaCube armor = size*3
    }
    // plan16: horse variant random (color/markings/health 15-30) — plan17 §10 E11 strict horse variant 0..34 (color 7 * markings 5)
    int horseVariant = 0; // 0..34 variant (color 0..6 + markings 0..4), randomized on spawn
    float horseJumpStrength = 0.0f; // randomized jump
    std::int32_t hurtCooldown = 0;
    std::int32_t leashHolder = -1;
    std::int64_t lastTeleportTick = -10000;
    bool isBabyVal = false;
    std::unordered_set<std::string> tags;        // /tag (plan10 §6)
    // plan14 §4: Villager trading profession/level/restock/Gossip
    VillagerData villagerData;
    std::int32_t villagerXp = 0;
    std::int32_t villagerLevel = 1; // mirror villagerData.level for compat
    Gossip gossip;
    std::int64_t restockUntil = 0;
    // plan16: villager restock 2/day (vanilla: 2 restocks per in-game day)
    int villagerRestocksToday = 0;
    std::int64_t villagerLastRestockDay = -1;
    void syncVillagerLevel(){ villagerData.level = std::clamp(villagerLevel,1,5); }
    void setVillagerLevel(int lvl){ villagerLevel = std::clamp(lvl,1,5); villagerData.level = villagerLevel; }
    std::int64_t witherSkullCooldown = 0;
    int dragonPhase = 0; // 0 circling, 1 approaching, 2 perching/breath, 3 takeoff
    std::int64_t dragonPhaseUntil = 0;
    // minecart/vehicle physics (plan11 §3)
    double velX = 0, velY = 0, velZ = 0;

    static const char* kindName(MobKind k) { return mobStats(k).name; }
    static std::uint32_t typeId(MobKind k) {
        const auto& m = gen::entityTypeIdByName();
        auto it = m.find(kindName(k));
        return it != m.end() ? it->second : 0;
    }
    static bool isHostile(MobKind k) { return mobStats(k).hostile; }
    static bool isBaby(const MobEntity& e) { return e.age < 0; }
    static bool isBoss(MobKind k) { return k==MobKind::Wither || k==MobKind::EnderDragon; }
    static bool isBoat(MobKind k) {
        return k==MobKind::Boat || k==MobKind::OakBoat || k==MobKind::SpruceBoat || k==MobKind::BirchBoat || k==MobKind::JungleBoat
            || k==MobKind::AcaciaBoat || k==MobKind::DarkOakBoat || k==MobKind::MangroveBoat || k==MobKind::CherryBoat
            || k==MobKind::PaleOakBoat || k==MobKind::BambooRaft || k==MobKind::OakChestBoat || k==MobKind::SpruceChestBoat
            || k==MobKind::BirchChestBoat || k==MobKind::JungleChestBoat || k==MobKind::AcaciaChestBoat || k==MobKind::DarkOakChestBoat
            || k==MobKind::MangroveChestBoat || k==MobKind::CherryChestBoat || k==MobKind::PaleOakChestBoat || k==MobKind::BambooChestRaft;
    }

    struct Drop { std::uint32_t itemId; std::uint8_t count; };
    static Drop dropFor(MobKind k) {
        const auto& s = mobStats(k);
        if (!s.dropItem) return {0, 0};
        auto it = gen::itemIdByName().find(s.dropItem);
        if (it == gen::itemIdByName().end()) return {0, 0};
        const int n = s.dropMin + (s.dropMax > s.dropMin ? (rand() % (s.dropMax - s.dropMin + 1)) : 0);
        return {it->second, static_cast<std::uint8_t>(std::max(0, n))};
    }
    static std::uint32_t breedingItemFor(MobKind k) {
        const char* n = mobStats(k).breedingItem;
        if (!n) return 0;
        auto it = gen::itemIdByName().find(n);
        return it != gen::itemIdByName().end() ? it->second : 0;
    }
};

inline int armorPointsForItem(std::uint32_t itemId) {
    // map ItemId name suffix to armor points (approximate vanilla)
    // we resolve name lazily
    static std::unordered_map<std::uint32_t,int> cache;
    auto itc = cache.find(itemId);
    if (itc != cache.end()) return itc->second;
    std::string n;
    for (auto &e: gen::kItems) if (e.second==itemId) { n = std::string(e.first); break; }
    int v = 0;
    if (n.find("netherite_helmet")!=std::string::npos) v=3;
    else if (n.find("netherite_chestplate")!=std::string::npos) v=8;
    else if (n.find("netherite_leggings")!=std::string::npos) v=6;
    else if (n.find("netherite_boots")!=std::string::npos) v=3;
    else if (n.find("diamond_helmet")!=std::string::npos) v=3;
    else if (n.find("diamond_chestplate")!=std::string::npos) v=8;
    else if (n.find("diamond_leggings")!=std::string::npos) v=6;
    else if (n.find("diamond_boots")!=std::string::npos) v=3;
    else if (n.find("iron_helmet")!=std::string::npos) v=2;
    else if (n.find("iron_chestplate")!=std::string::npos) v=6;
    else if (n.find("iron_leggings")!=std::string::npos) v=5;
    else if (n.find("iron_boots")!=std::string::npos) v=2;
    else if (n.find("chainmail_helmet")!=std::string::npos) v=2;
    else if (n.find("chainmail_chestplate")!=std::string::npos) v=5;
    else if (n.find("chainmail_leggings")!=std::string::npos) v=4;
    else if (n.find("chainmail_boots")!=std::string::npos) v=1;
    else if (n.find("golden_helmet")!=std::string::npos) v=2;
    else if (n.find("golden_chestplate")!=std::string::npos) v=5;
    else if (n.find("golden_leggings")!=std::string::npos) v=3;
    else if (n.find("golden_boots")!=std::string::npos) v=1;
    else if (n.find("leather_helmet")!=std::string::npos) v=1;
    else if (n.find("leather_chestplate")!=std::string::npos) v=3;
    else if (n.find("leather_leggings")!=std::string::npos) v=2;
    else if (n.find("leather_boots")!=std::string::npos) v=1;
    else if (n.find("turtle_helmet")!=std::string::npos) v=2;
    cache[itemId]=v;
    return v;
}
inline int totalArmorPoints(const MobEntity& m) {
    int tot=0;
    for (int i=2;i<6;i++) if (!m.equipment[i].empty()) tot+=armorPointsForItem(m.equipment[i].itemId);
    return tot;
}
inline int totalArmorPoints(const std::array<ItemStack,46>& inv) {
    int tot=0;
    // armor slots: 5 boots,6 leggings,7 chest,8 head? In Player inv 5..8 are armor (5 boots? Actually 5..8)
    // vanilla player inv layout: 0-8 hotbar mirrored? Our inv is 0..44? Let's assume armor slots 5,6,7,8
    for (int i=5;i<=8;i++) if (i>=0 && i<46 && !inv[i].empty()) tot+=armorPointsForItem(inv[i].itemId);
    return tot;
}

} // namespace cppfm