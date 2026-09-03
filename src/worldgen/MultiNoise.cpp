// MultiNoise authored biome climate table (clean-room approximations of the
// vanilla overworld layout: hot/dry -> desert/savanna, cold -> snowy,
// continentalness drives oceans vs inland, weirdness splits variants).
#include "MultiNoise.hpp"

namespace cppfm::worldgen {

void MultiNoiseBiomeSource::buildDefaultTable() {
    auto add = [&](const char* k, double t, double h, double c, double e,
                   double d, double w) {
        entries_.push_back({k, {t, h, c, e, d, w}});
        NoiseHypercube cube;
        auto r = [&](double v){ return ParameterRange{float(v-0.05), float(v+0.05)}; };
        cube.temperature = r(t); cube.humidity = r(h); cube.continentalness = r(c);
        cube.erosion = r(e); cube.depth = r(d); cube.weirdness = r(w);
        entriesCube_.push_back({k, cube});
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
    // ---- plan42 R2 (E-10): vanilla 1.21.4 biome parity 31->54 (registry order
    // EmbeddedData minecraft:worldgen/biome 65; all keys below verified present).
    // oceans (temperature-split variants)
    add("minecraft:lukewarm_ocean", 0.5, 0.1, -0.28, -0.10, 0.20, 0.1);
    add("minecraft:cold_ocean",  -0.5,  0.1, -0.28, -0.10, 0.20,-0.1);
    add("minecraft:deep_lukewarm_ocean", 0.5, 0.1, -0.45, -0.20, 0.35, 0.1);
    add("minecraft:deep_cold_ocean", -0.5, 0.1, -0.45, -0.20, 0.35,-0.1);
    add("minecraft:deep_frozen_ocean", -0.85, 0.1, -0.45, -0.20, 0.35,-0.3);
    add("minecraft:mushroom_fields", 0.2, 0.8, -0.35, -0.10, 0.10, 0.6);
    // jungle variants
    add("minecraft:bamboo_jungle", 0.9, 0.9, 0.10, -0.10, 0.00, 0.5);
    add("minecraft:sparse_jungle", 0.9, 0.7, 0.12, 0.10, 0.00,-0.2);
    // forest old-growth + flower variants
    add("minecraft:flower_forest", 0.1, 0.6, 0.12, 0.10, 0.00, 0.7);
    add("minecraft:old_growth_birch_forest", -0.1, 0.7, 0.14, 0.05, 0.00, 0.6);
    add("minecraft:old_growth_pine_taiga", -0.5, 0.7, 0.16, 0.00, 0.00, 0.3);
    add("minecraft:old_growth_spruce_taiga", -0.5, 0.7, 0.16, 0.00, 0.00,-0.3);
    add("minecraft:windswept_forest", 0.1,-0.1, 0.20, -0.40,-0.10, 0.3);
    // savanna plateau variants
    add("minecraft:savanna_plateau", 0.8,-0.2, 0.16, -0.30,-0.20, 0.2);
    add("minecraft:windswept_savanna", 0.8,-0.2, 0.18, -0.40,-0.10,-0.2);
    // snowy mountain variants
    add("minecraft:snowy_slopes",-0.7,  0.3,  0.20, -0.35,-0.40,-0.2);
    add("minecraft:ice_spikes", -0.9, -0.2,  0.14,  0.20, 0.00, 0.5);
    // swamp variant
    add("minecraft:mangrove_swamp", 0.6, 0.95, 0.02, 0.30,-0.10, 0.3);
    // cave biomes (depth>0 separates from surface points)
    add("minecraft:dripstone_caves", 0.2, 0.2, 0.12, 0.10, 0.30, 0.0);
    add("minecraft:lush_caves", 0.3, 0.8, 0.10, 0.20, 0.30, 0.2);
    add("minecraft:deep_dark",  0.0,  0.2,  0.10,  0.00, 0.50,-0.2);
    // badlands variants
    add("minecraft:eroded_badlands", 1.0,-0.5, 0.10, -0.30, 0.10,-0.2);
    add("minecraft:wooded_badlands", 1.0,-0.4, 0.10, -0.20, 0.10, 0.0);
    // ---- plan45 G-11: vanilla 65-biome table completion (54 -> 65).
    // Missing 11 = nether 5 + end 5 + the_void 1, verified against vanilla
    // 1.21.4 client jar data/minecraft/worldgen/biome/*.json (65 files,
    // piston-meta 1.21.4, sha1-verified download 2026-09-04).
    // Nether/end entries carry a dimension tag: sample() still searches the
    // overworld subset only, so these can never leak into overworld chunks.
    // Emit paths: sampleNether()/sampleEnd(). Climate points are clean-room
    // distinct points in the vanilla nether/end parameter style
    // (temperature/humidity-driven, cf. vanilla NetherBiomeSource).
    // nether (temperature x humidity grid, depth fixed by terrain)
    addNether("minecraft:nether_wastes",    0.0,  0.0, 0.0, 0.0, 0.0, 0.0);
    addNether("minecraft:soul_sand_valley",-0.5,  0.0, 0.0, 0.0, 0.0, 0.0);
    addNether("minecraft:crimson_forest",   0.4,  0.0, 0.0, 0.0, 0.0, 0.0);
    addNether("minecraft:warped_forest",    0.0,  0.5, 0.0, 0.0, 0.0, 0.0);
    addNether("minecraft:basalt_deltas",    0.5,  0.3, 0.0, 0.0, 0.0, 0.0);
    // end (radial layout in fillEnd stays authoritative for terrain;
    // these points back the sampleEnd() emit path for biome queries)
    addEnd("minecraft:the_end",            0.0,  0.0, 0.0, 0.0, 0.0, 0.0);
    addEnd("minecraft:end_barrens",        0.0, -0.5, 0.0, 0.0, 0.0, 0.0);
    addEnd("minecraft:end_highlands",      0.5,  0.0, 0.0, 0.0, 0.0, 0.0);
    addEnd("minecraft:end_midlands",      -0.5,  0.0, 0.0, 0.0, 0.0, 0.0);
    addEnd("minecraft:small_end_islands",  0.0,  0.5, 0.0, 0.0, 0.0, 0.0);
    // the_void: registry presence only (vanilla default for Y<-64 /
    // ungenerated chunks). Excluded from all sampling by dimension filter.
    addSpecial("minecraft:the_void");
}

} // namespace cppfm::worldgen
