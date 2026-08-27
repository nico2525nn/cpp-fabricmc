#pragma once
#include <cstdint>
#include "../game/Entities.hpp"
#include "../game/World.hpp"
namespace cppfm {
struct BlockPosI { std::int32_t x=0, y=0, z=0; };
struct Player; // forward
struct ItemUseContext {
    Player* player = nullptr;
    World* world = nullptr;
    BlockPosI hitPos{};
    BlockPosI placePos{};
    int face = 0;
    Vec3 cursor{0,0,0};
    float yaw = 0;
    bool isSneaking = false;
};
} // namespace cppfm
