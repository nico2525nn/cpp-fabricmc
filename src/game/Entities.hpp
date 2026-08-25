// Entities: item drops + mobs with data-driven stats (plan.md Phase 3/4 +
// plan3.md "74種類のモブ" registry approach).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../generated/EntityIds.hpp"
#include "../generated/ItemIds.hpp"

namespace cppfm {

struct Vec3 { double x, y, z; };

struct ItemEntity {
    std::int32_t entityId = 0;
    std::uint32_t itemId = 0;
    std::uint8_t count = 1;
    double x=0, y=0, z=0;
    double vx=0, vy=0, vz=0;
    std::int64_t ageTicks = 0;
    bool collected = false;
    // last-broadcast pose for delta sync
    double sentX=0, sentY=0, sentZ=0; bool hasSent=false;
};

struct XpOrbEntity {
    std::int32_t entityId = 0;
    std::uint16_t value = 1;             // xp points carried
    double x=0, y=0, z=0;
    double vy=0;
    std::int64_t ageTicks = 0;
};

enum class MobKind : std::uint8_t {
    Pig = 0, Cow, Sheep, Chicken,
    Zombie, Creeper, Skeleton, Spider,
    Slime, Enderman, Witch, Rabbit
};

// Static per-kind gameplay table (clean-room values approximating vanilla).
struct MobStats {
    const char* name;
    float maxHealth;
    float moveSpeed;          // blocks/tick base
    float attackDamage;
    bool hostile;
    bool burnsInDaylight;
    const char* dropItem;     // nullptr = none
    int dropMin, dropMax;
    const char* breedingItem; // nullptr = cannot breed
    std::uint32_t xpDrop;     // orbs on death (adults)
};

inline const MobStats& mobStats(MobKind k) {
    static const MobStats table[] = {
        {"minecraft:pig",      10.f, 0.10f, 0.f, false, false, "minecraft:porkchop", 1, 3, "minecraft:carrot",        1},
        {"minecraft:cow",      10.f, 0.09f, 0.f, false, false, "minecraft:beef",     1, 3, "minecraft:wheat",         1},
        {"minecraft:sheep",     8.f, 0.09f, 0.f, false, false, "minecraft:mutton",   1, 2, "minecraft:wheat",         1},
        {"minecraft:chicken",   4.f, 0.09f, 0.f, false, false, "minecraft:chicken",  1, 1, "minecraft:wheat_seeds",   1},
        {"minecraft:zombie",   20.f, 0.085f, 3.f, true, true, "minecraft:rotten_flesh", 0, 2, nullptr,               5},
        {"minecraft:creeper",  20.f, 0.095f, 0.f, true, false, nullptr,             0, 0, nullptr,                   5},
        {"minecraft:skeleton", 20.f, 0.10f, 2.f, true, true, "minecraft:bone",     0, 2, nullptr,                   5},
        {"minecraft:spider",   16.f, 0.13f, 2.f, true, false, "minecraft:string",  0, 2, nullptr,                   5},
        {"minecraft:slime",     4.f, 0.06f, 2.f, true, false, "minecraft:slime_ball", 0, 2, nullptr,                4},
        {"minecraft:enderman", 40.f, 0.12f, 7.f, true, false, "minecraft:ender_pearl", 0, 1, nullptr,              5},
        {"minecraft:witch",    26.f, 0.09f, 3.f, true, false, "minecraft:glowstone_dust", 0, 2, nullptr,           5},
        {"minecraft:rabbit",    3.f, 0.13f, 0.f, false, false, "minecraft:rabbit_hide", 0, 1, "minecraft:dandelion",1},
    };
    return table[static_cast<int>(k)];
}

struct MobEntity {
    std::int32_t entityId = 0;
    MobKind kind = MobKind::Pig;
    double x=0, y=0, z=0;
    float yaw=0, pitch=0;
    double health = 10;
    bool dead = false;
    // wander AI
    double tx=0, tz=0; bool hasTarget=false;
    std::int64_t nextWanderAt = 0;
    // breeding / aging (plan3 繁殖)
    std::int32_t age = 0;                 // <0 = baby ticks remaining
    bool inLove = false;
    std::int64_t loveUntilTick = 0;
    std::int64_t breedCooldownUntil = 0;
    // anger memory (敵対関係)
    std::int32_t angerTargetEntityId = -1;
    std::int64_t angryUntilTick = 0;
    // broadcast tracking
    double sentX=0, sentY=0, sentZ=0; float sentYaw=0; bool hasSent=false;
    std::int64_t lastSeenMs = 0;

    static const char* kindName(MobKind k) { return mobStats(k).name; }
    static std::uint32_t typeId(MobKind k) {
        const auto& m = gen::entityTypeIdByName();
        auto it = m.find(kindName(k));
        return it != m.end() ? it->second : 0;
    }
    static bool isHostile(MobKind k) { return mobStats(k).hostile; }
    static bool isBaby(const MobEntity& e) { return e.age < 0; }

    struct Drop { std::uint32_t itemId; std::uint8_t count; };
    static Drop dropFor(MobKind k) {
        const auto& s = mobStats(k);
        if (!s.dropItem) return {0, 0};
        auto it = gen::itemIdByName().find(s.dropItem);
        if (it == gen::itemIdByName().end()) return {0, 0};
        const int n = s.dropMin + (rand() % (s.dropMax - s.dropMin + 1));
        return {it->second, static_cast<std::uint8_t>(std::max(0, n))};
    }
    static std::uint32_t breedingItemFor(MobKind k) {
        const char* n = mobStats(k).breedingItem;
        if (!n) return 0;
        auto it = gen::itemIdByName().find(n);
        return it != gen::itemIdByName().end() ? it->second : 0;
    }
};

} // namespace cppfm
