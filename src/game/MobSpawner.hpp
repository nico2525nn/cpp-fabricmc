// spawning logic to allow Item/Command/Dispenser to share.
#pragma once
#include <string>
#include <cstdint>
#include "Entities.hpp"

namespace cppfm {

class GameServer;

class MobSpawner {
public:
    explicit MobSpawner(GameServer& srv) : srv_(srv) {}

    // Spawn by full minecraft:xxx name at position. Returns true if spawned.
    bool spawnByName(const std::string& name, double x, double y, double z);

    // Spawn from a spawn egg item name (e.g., "minecraft:zombie_spawn_egg"). Extracts mob id by stripping "_spawn_egg" suffix.
    bool spawnFromEgg(const std::string& eggItemName, double x, double y, double z);

    // Spawn from an ItemStack that is a spawn egg. Returns true if consumed (spawned).
    bool spawnFromEggStack(const ItemStack& eggStack, double x, double y, double z, bool& consumed);

    // Dispenser spawn: uses facing to offset position.
    bool spawnFromDispenser(const std::string& eggName, int x, int y, int z, const std::string& facing);

private:
    GameServer& srv_;
    static std::string eggToMobName(const std::string& egg);
};

} // namespace cppfm
