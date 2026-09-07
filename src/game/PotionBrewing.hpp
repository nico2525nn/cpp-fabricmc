// Brewing transforms and potion registry access used by brewingTick and tests.
#pragma once
#include <cstdint>
#include <string>
#include "Items.hpp"

namespace cppfm {

struct PotionBrewing {
    // ItemStack owns the registry table.  Brewing uses the same view so a new
    // registry entry cannot silently make item serialization and brewing diverge.
    static const auto& potionIds() { return ItemStack::potionIds(); }
    static int potionIdByName(const std::string& name) { return ItemStack::potionIdByName(name); }
    static std::string potionNameById(int id) { return ItemStack::potionNameById(id); }
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
