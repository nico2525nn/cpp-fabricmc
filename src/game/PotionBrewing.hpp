// awkward + sugar -> mundane etc is stub Full mapping: awkward + <effect ingredient> -> effect potion, awkward + fermented -> weakness,
// awkw + gunpowder -> splash This helper centralizes transform logic for brewingTick and tests.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include "../generated/ItemIds.hpp"

namespace cppfm {

struct PotionBrewing {
    // Registry-aware potion ids (minecraft:potion 45 entries, 1.21.4): water 0, mundane 1, thick 2, awkward 3, night_vision 4, ...
    // wind_charged 42 etc. Full map duplicated from Items::potionIds for standalone header.
    static const std::unordered_map<std::string,int>& potionIds() {
        static const std::unordered_map<std::string,int> m{
            {"minecraft:water",0},{"minecraft:mundane",1},{"minecraft:thick",2},{"minecraft:awkward",3},
            {"minecraft:night_vision",4},{"minecraft:long_night_vision",5},{"minecraft:invisibility",6},{"minecraft:long_invisibility",7},
            {"minecraft:leaping",8},{"minecraft:long_leaping",9},{"minecraft:strong_leaping",10},{"minecraft:fire_resistance",11},{"minecraft:long_fire_resistance",12},
            {"minecraft:swiftness",13},{"minecraft:long_swiftness",14},{"minecraft:strong_swiftness",15},{"minecraft:slowness",16},{"minecraft:long_slowness",17},{"minecraft:strong_slowness",18},
            {"minecraft:water_breathing",19},{"minecraft:long_water_breathing",20},{"minecraft:healing",21},{"minecraft:strong_healing",22},
            {"minecraft:harming",23},{"minecraft:strong_harming",24},{"minecraft:poison",25},{"minecraft:long_poison",26},{"minecraft:strong_poison",27},
            {"minecraft:regeneration",28},{"minecraft:long_regeneration",29},{"minecraft:strong_regeneration",30},{"minecraft:strength",31},{"minecraft:long_strength",32},{"minecraft:strong_strength",33},
            {"minecraft:weakness",34},{"minecraft:long_weakness",35},{"minecraft:luck",36},{"minecraft:turtle_master",37},{"minecraft:long_turtle_master",38},{"minecraft:strong_turtle_master",39},
            {"minecraft:slow_falling",40},{"minecraft:long_slow_falling",41},{"minecraft:wind_charged",42},{"minecraft:weaving",43},{"minecraft:oozing",44},{"minecraft:infested",45}
        };
        return m;
    }
    static int potionIdByName(const std::string& n) {
        auto& mm = potionIds();
        auto it = mm.find(n);
        if (it != mm.end()) return it->second;
        std::string q = n.find(':')==std::string::npos ? std::string("minecraft:")+n : n;
        auto it2 = mm.find(q);
        return it2 != mm.end() ? it2->second : 0;
    }
    static std::string potionNameById(int id) {
        for (auto &kv : potionIds()) if (kv.second==id) return kv.first;
        return "minecraft:water";
    }
    static int mix(int curId, bool hasPotionContents, std::uint32_t ingredientId) {
        int waterId = potionIdByName("minecraft:water");
        int awkwardId = potionIdByName("minecraft:awkward");
        bool isWater = !hasPotionContents || curId == waterId;
        auto idOf = [&](const char* n)->std::uint32_t{
            auto it = gen::itemIdByName().find(n);
            return it != gen::itemIdByName().end() ? it->second : 0;
        };
        std::uint32_t wartId = idOf("minecraft:nether_wart");
        std::uint32_t sugarId = idOf("minecraft:sugar");
        std::uint32_t spiderEyeId = idOf("minecraft:spider_eye");
        std::uint32_t ghastTearId = idOf("minecraft:ghast_tear");
        std::uint32_t blazePowderId = idOf("minecraft:blaze_powder");
        std::uint32_t magmaCreamId = idOf("minecraft:magma_cream");
        std::uint32_t glisteringMelonId = idOf("minecraft:glistering_melon_slice");
        std::uint32_t goldenCarrotId = idOf("minecraft:golden_carrot");
        std::uint32_t rabbitFootId = idOf("minecraft:rabbit_foot");
        std::uint32_t fermentedEyeId = idOf("minecraft:fermented_spider_eye");
        std::uint32_t pufferfishId = idOf("minecraft:pufferfish");
        std::uint32_t phantomMembraneId = idOf("minecraft:phantom_membrane");
        std::uint32_t redstoneId = idOf("minecraft:redstone");
        std::uint32_t glowstoneId = idOf("minecraft:glowstone_dust");
        if (ingredientId == wartId && isWater) return awkwardId; // water -> awkward (3) per D30
        if (curId == awkwardId) {
            if (ingredientId == sugarId) return potionIdByName("minecraft:swiftness");
            else if (ingredientId == spiderEyeId) return potionIdByName("minecraft:poison");
            else if (ingredientId == ghastTearId) return potionIdByName("minecraft:regeneration");
            else if (ingredientId == blazePowderId) return potionIdByName("minecraft:strength");
            else if (ingredientId == magmaCreamId) return potionIdByName("minecraft:fire_resistance");
            else if (ingredientId == glisteringMelonId) return potionIdByName("minecraft:healing");
            else if (ingredientId == goldenCarrotId) return potionIdByName("minecraft:night_vision");
            else if (ingredientId == rabbitFootId) return potionIdByName("minecraft:leaping");
            else if (ingredientId == fermentedEyeId) return potionIdByName("minecraft:weakness");
            else if (ingredientId == pufferfishId) return potionIdByName("minecraft:water_breathing");
            else if (ingredientId == phantomMembraneId) return potionIdByName("minecraft:slow_falling");
        } else {
            // redstone -> long, glowstone -> strong for any base potion
            if (ingredientId == redstoneId) {
                std::string name = potionNameById(curId);
                if (name.rfind("minecraft:long_",0)==0 || name.rfind("minecraft:strong_",0)==0) return -1;
                std::string base = name.substr(name.find(':')+1);
                std::string longName = "minecraft:long_" + base;
                auto it = potionIds().find(longName);
                if (it != potionIds().end()) return it->second;
                return -1;
            } else if (ingredientId == glowstoneId) {
                std::string name = potionNameById(curId);
                if (name.rfind("minecraft:long_",0)==0 || name.rfind("minecraft:strong_",0)==0) return -1;
                std::string base = name.substr(name.find(':')+1);
                std::string strongName = "minecraft:strong_" + base;
                auto it = potionIds().find(strongName);
                if (it != potionIds().end()) return it->second;
                return -1;
            }
        }
        return -1; // no transform
    }
    // Helper for brewing stand itemId gunpowder/dragon breath transform (splash/lingering)
    static bool isGunpowder(std::uint32_t id){
        auto it = gen::itemIdByName().find("minecraft:gunpowder");
        return it != gen::itemIdByName().end() && it->second == id;
    }
    static bool isDragonBreath(std::uint32_t id){
        auto it = gen::itemIdByName().find("minecraft:dragon_breath");
        return it != gen::itemIdByName().end() && it->second == id;
    }
};

} // namespace cppfm
