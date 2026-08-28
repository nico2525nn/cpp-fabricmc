// MultiNoise authored biome climate table (clean-room approximations of the
// vanilla overworld layout: hot/dry -> desert/savanna, cold -> snowy,
// continentalness drives oceans vs inland, weirdness splits variants).
#include "MultiNoise.hpp"

namespace cppfm::worldgen {

void MultiNoiseBiomeSource::buildDefaultTable() {
    auto add = [&](const char* k, double t, double h, double c, double e,
                   double d, double w) {
        entries_.push_back({k, {t, h, c, e, d, w}});
    };
    // ---- oceans (low continentalness)
    add("minecraft:deep_ocean",   0.0,  0.0, -0.45, -0.2,  0.35, 0.0);
    add("minecraft:ocean",        0.0,  0.1, -0.28, -0.1,  0.20, 0.0);
    add("minecraft:warm_ocean",   0.6,  0.1, -0.30, -0.1,  0.20, 0.2);
    add("minecraft:frozen_ocean",-0.8,  0.1, -0.30, -0.1,  0.20,-0.2);
    // ---- coasts
    add("minecraft:beach",        0.1,  0.0,  0.02,  0.25, 0.05, 0.0);
    add("minecraft:snowy_beach", -0.7,  0.0,  0.02,  0.25, 0.05,-0.3);
    add("minecraft:stony_shore",  0.0,  0.0,  0.06,  0.05, 0.15, 0.4);
    // ---- midlands
    add("minecraft:plains",       0.1,  0.0,  0.12,  0.35, 0.00, 0.0);
    add("minecraft:sunflower_plains", 0.1, 0.0, 0.12, 0.35, 0.00, 0.6);
    add("minecraft:forest",       0.1,  0.5,  0.12,  0.10, 0.00, 0.0);
    add("minecraft:birch_forest", 0.0,  0.6,  0.12,  0.10, 0.00, 0.4);
    add("minecraft:dark_forest",  0.2,  0.7,  0.12,  0.10, 0.00,-0.4);
    add("minecraft:pale_garden",  0.15, 0.65, 0.12,  0.10, 0.00,-0.55);
    add("minecraft:swamp",        0.3,  0.85, 0.02,  0.40,-0.10,-0.3);
    add("minecraft:jungle",       0.9,  0.9,  0.10,  0.00, 0.00, 0.2);
    add("minecraft:savanna",      0.8, -0.2,  0.14,  0.20, 0.00, 0.0);
    add("minecraft:desert",       0.9, -0.7,  0.12,  0.15, 0.00, 0.0);
    add("minecraft:badlands",     1.0, -0.5,  0.10, -0.20, 0.10, 0.4);
    // ---- highlands
    add("minecraft:windswept_hills", 0.1,-0.1, 0.20, -0.35, -0.10, 0.0);
    add("minecraft:taiga",       -0.4,  0.6,  0.16,  0.05, 0.00, 0.0);
    add("minecraft:snowy_taiga", -0.8,  0.5,  0.16,  0.05, 0.00,-0.2);
    add("minecraft:snowy_plains",-0.9,  0.0,  0.14,  0.30, 0.00, 0.0);
    add("minecraft:grove",       -0.6,  0.6,  0.18, -0.30,-0.30, 0.0);
    add("minecraft:windswept_gravelly_hills", 0.0,-0.2,0.22,-0.50,-0.20,0.5);
    add("minecraft:jagged_peaks",-0.5, -0.1,  0.24, -0.60,-0.55, 0.0);
    add("minecraft:frozen_peaks",-0.8, -0.1,  0.24, -0.60,-0.55,-0.5);
    add("minecraft:stony_peaks",  0.5, -0.3,  0.24, -0.55,-0.50, 0.4);
    add("minecraft:meadow",      -0.2,  0.3,  0.20, -0.25,-0.35, 0.3);
    add("minecraft:cherry_grove", 0.2,  0.5,  0.16, -0.15,-0.25,-0.5);
    // rivers
    add("minecraft:river",        0.0,  0.0, -0.10,  0.35,-0.15, 0.0);
    add("minecraft:frozen_river",-0.8,  0.0, -0.10,  0.35,-0.15, 0.0);
}

} // namespace cppfm::worldgen
