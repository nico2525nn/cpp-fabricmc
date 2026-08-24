// Entities: item drops + mobs (plan.md Phase 3/4 MVP).
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

enum class MobKind { Pig, Cow, Sheep, Chicken, Zombie };

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
    // broadcast tracking
    double sentX=0, sentY=0, sentZ=0; float sentYaw=0; bool hasSent=false;
    std::int64_t lastSeenMs = 0;

    static const char* kindName(MobKind k) {
        switch (k) {
        case MobKind::Pig: return "minecraft:pig";
        case MobKind::Cow: return "minecraft:cow";
        case MobKind::Sheep: return "minecraft:sheep";
        case MobKind::Chicken: return "minecraft:chicken";
        case MobKind::Zombie: return "minecraft:zombie";
        }
        return "minecraft:pig";
    }
    static std::uint32_t typeId(MobKind k) {
        const auto& m = gen::entityTypeIdByName();
        auto it = m.find(kindName(k));
        return it != m.end() ? it->second : 0;
    }
    struct Drop { std::uint32_t itemId; std::uint8_t count; };
    static Drop dropFor(MobKind k) {
        const auto& items = gen::itemIdByName();
        auto get = [&](const char* n) -> std::uint32_t {
            auto it = items.find(n);
            return it != items.end() ? it->second : 0;
        };
        switch (k) {
        case MobKind::Pig:     return {get("minecraft:porkchop"), 1};
        case MobKind::Cow:     return {get("minecraft:beef"), 1};
        case MobKind::Sheep:   return {get("minecraft:mutton"), 1};
        case MobKind::Chicken: return {get("minecraft:feather"), 1};
        case MobKind::Zombie:  return {get("minecraft:rotten_flesh"), 1};
        }
        return {0, 1};
    }
};

} // namespace cppfm
