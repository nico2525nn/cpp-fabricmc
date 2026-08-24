// Block mining data (hardness/tool) for survival dig timing (Phase 3).
#pragma once
#include <string>
#include <unordered_map>

namespace cppfm {

struct BlockMineInfo {
    float hardness;
    bool requiresPickaxe;   // won't drop without pickaxe
};

inline const BlockMineInfo* mineInfo(const std::string& blockName) {
    static const std::unordered_map<std::string, BlockMineInfo> table = {
        {"minecraft:dirt",          {0.5f, false}},
        {"minecraft:grass_block",   {0.6f, false}},
        {"minecraft:sand",          {0.5f, false}},
        {"minecraft:gravel",        {0.6f, false}},
        {"minecraft:stone",         {1.5f, true}},
        {"minecraft:cobblestone",   {2.0f, true}},
        {"minecraft:coal_ore",      {3.0f, true}},
        {"minecraft:iron_ore",      {3.0f, true}},
        {"minecraft:oak_log",       {2.0f, false}},
        {"minecraft:oak_leaves",    {0.2f, false}},
        {"minecraft:oak_planks",    {2.0f, false}},
        {"minecraft:glass",         {0.3f, false}},
        {"minecraft:torch",         {0.0f, false}},
        {"minecraft:bedrock",       {-1.0f, true}},
    };
    auto it = table.find(blockName);
    return it != table.end() ? &it->second : nullptr;
}

inline float toolSpeed(const std::string& itemName, bool /*isPickaxeContext*/) {
    if (itemName == "minecraft:iron_pickaxe") return 6.f;
    if (itemName == "minecraft:iron_axe")     return 7.f;
    if (itemName == "minecraft:iron_sword")   return 1.5f;
    return 1.f;
}

} // namespace cppfm
