#include "MobSpawner.hpp"
#include "GameServer.hpp"
#include "../generated/ItemIds.hpp"

namespace cppfm {

std::string MobSpawner::eggToMobName(const std::string& egg) {
    std::string n = egg;
    auto pos = n.find("_spawn_egg");
    if (pos != std::string::npos) n = n.substr(0, pos);
    // egg item is minecraft:zombie_spawn_egg -> mob is minecraft:zombie
    return n;
}

bool MobSpawner::spawnByName(const std::string& name, double x, double y, double z) {
    return srv_.spawnMobByTypeName(name, x, y, z);
}

bool MobSpawner::spawnFromEgg(const std::string& eggItemName, double x, double y, double z) {
    if (eggItemName.find("_spawn_egg")==std::string::npos) return false;
    std::string mobName = eggToMobName(eggItemName);
    // ensure minecraft: prefix
    if (mobName.find(':')==std::string::npos) mobName = "minecraft:" + mobName;
    return spawnByName(mobName, x, y, z);
}

bool MobSpawner::spawnFromEggStack(const ItemStack& eggStack, double x, double y, double z, bool& consumed) {
    consumed = false;
    if (eggStack.empty()) return false;
    std::string n = eggStack.name();
    if (n.find("_spawn_egg")==std::string::npos) return false;
    if (spawnFromEgg(n, x, y, z)) {
        consumed = true;
        return true;
    }
    return false;
}

bool MobSpawner::spawnFromDispenser(const std::string& eggName, int x, int y, int z, const std::string& facing) {
    double dx=0, dy=0, dz=0;
    if (facing=="north") dz=-1;
    else if (facing=="south") dz=1;
    else if (facing=="west") dx=-1;
    else if (facing=="east") dx=1;
    else if (facing=="up") dy=1;
    else if (facing=="down") dy=-1;
    double sx = x + 0.5 + dx*0.6;
    double sy = y + 0.5 + dy*0.6;
    double sz = z + 0.5 + dz*0.6;
    return spawnFromEgg(eggName, sx, sy, sz);
}

} // namespace cppfm
