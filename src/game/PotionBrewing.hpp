// PotionBrewing — plan23 §5 I7 brewing transform (water->awkward etc)
// Vanilla 1.21.4 PotionBrewingRegistry: nether_wart + water -> awkward, awkward + sugar -> mundane etc is stub
// Full mapping: awkward + <effect ingredient> -> effect potion, awkward + fermented -> weakness, awkw + gunpowder -> splash
// This helper centralizes transform logic for brewingTick and tests.
#pragma once
#include <cstdint>
#include <string>
#include "../generated/ItemIds.hpp"

namespace cppfm {

struct PotionBrewing {
    // potion id mapping for test: 0 water, 1 awkward, 2 swiftness, 3 poison, 4 regen, 5 strength, 6 fire_res, 7 healing, 8 night_vision, 9 leaping, 10 weakness, 11 water_breathing, 12 slow_falling
    // extended: 100+ strong, 200+ extended (redstone/glowstone) — vanilla registry uses separate entries but we map offset.
    static int mix(int curId, bool hasPotionContents, std::uint32_t ingredientId) {
        bool isWater = !hasPotionContents || curId == 0;
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
        if (ingredientId == wartId && isWater) return 1; // awkward
        if (curId == 1) {
            if (ingredientId == sugarId) return 2;
            else if (ingredientId == spiderEyeId) return 3;
            else if (ingredientId == ghastTearId) return 4;
            else if (ingredientId == blazePowderId) return 5;
            else if (ingredientId == magmaCreamId) return 6;
            else if (ingredientId == glisteringMelonId) return 7;
            else if (ingredientId == goldenCarrotId) return 8;
            else if (ingredientId == rabbitFootId) return 9;
            else if (ingredientId == fermentedEyeId) return 10;
            else if (ingredientId == pufferfishId) return 11;
            else if (ingredientId == phantomMembraneId) return 12;
        } else if (curId >= 2 && curId <= 12) {
            if (ingredientId == redstoneId) return curId + 100;
            else if (ingredientId == glowstoneId) return curId + 200;
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
