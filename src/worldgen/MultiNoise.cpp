// MultiNoise authored biome climate table (clean-room approximations of the vanilla overworld layout: hot/dry -> desert/savanna, cold ->
// snowy, continentalness drives oceans vs inland, weirdness splits variants).
#include "MultiNoise.hpp"

namespace cppfm::worldgen {

void MultiNoiseBiomeSource::buildDefaultTable() {
    // Default overworld biome points (name + 6 climate params). Single truth (was 54 positional add() calls); values untouched.
    struct BiomePoint { const char* key; double temp, humid, continent, erosion, depth, weird; };
    static constexpr BiomePoint kPoints[] = {
        {"minecraft:deep_ocean", 0.0, 0.0, -0.45, -0.2, 0.35, 0.0},
        {"minecraft:ocean", 0.0, 0.1, -0.28, -0.1, 0.20, 0.0},
        {"minecraft:warm_ocean", 0.6, 0.1, -0.30, -0.1, 0.20, 0.2},
        {"minecraft:frozen_ocean", -0.8, 0.1, -0.30, -0.1, 0.20, -0.2},
        // ---- coasts
        {"minecraft:beach", 0.1, 0.0, 0.02, 0.25, 0.05, 0.0},
        {"minecraft:snowy_beach", -0.7, 0.0, 0.02, 0.25, 0.05, -0.3},
        {"minecraft:stony_shore", 0.0, 0.0, 0.06, 0.05, 0.15, 0.4},
        // ---- midlands
        {"minecraft:plains", 0.1, 0.0, 0.12, 0.35, 0.00, 0.0},
        {"minecraft:sunflower_plains", 0.1, 0.0, 0.12, 0.35, 0.00, 0.6},
        {"minecraft:forest", 0.1, 0.5, 0.12, 0.10, 0.00, 0.0},
        {"minecraft:birch_forest", 0.0, 0.6, 0.12, 0.10, 0.00, 0.4},
        {"minecraft:dark_forest", 0.2, 0.7, 0.12, 0.10, 0.00, -0.4},
        {"minecraft:pale_garden", 0.15, 0.65, 0.12, 0.10, 0.00, -0.55},
        {"minecraft:swamp", 0.3, 0.85, 0.02, 0.40, -0.10, -0.3},
        {"minecraft:jungle", 0.9, 0.9, 0.10, 0.00, 0.00, 0.2},
        {"minecraft:savanna", 0.8, -0.2, 0.14, 0.20, 0.00, 0.0},
        {"minecraft:desert", 0.9, -0.7, 0.12, 0.15, 0.00, 0.0},
        {"minecraft:badlands", 1.0, -0.5, 0.10, -0.20, 0.10, 0.4},
        // ---- highlands
        {"minecraft:windswept_hills", 0.1, -0.1, 0.20, -0.35, -0.10, 0.0},
        {"minecraft:taiga", -0.4, 0.6, 0.16, 0.05, 0.00, 0.0},
        {"minecraft:snowy_taiga", -0.8, 0.5, 0.16, 0.05, 0.00, -0.2},
        {"minecraft:snowy_plains", -0.9, 0.0, 0.14, 0.30, 0.00, 0.0},
        {"minecraft:grove", -0.6, 0.6, 0.18, -0.30, -0.30, 0.0},
        {"minecraft:windswept_gravelly_hills", 0.0, -0.2, 0.22, -0.50, -0.20, 0.5},
        {"minecraft:jagged_peaks", -0.5, -0.1, 0.24, -0.60, -0.55, 0.0},
        {"minecraft:frozen_peaks", -0.8, -0.1, 0.24, -0.60, -0.55, -0.5},
        {"minecraft:stony_peaks", 0.5, -0.3, 0.24, -0.55, -0.50, 0.4},
        {"minecraft:meadow", -0.2, 0.3, 0.20, -0.25, -0.35, 0.3},
        {"minecraft:cherry_grove", 0.2, 0.5, 0.16, -0.15, -0.25, -0.5},
        // rivers
        {"minecraft:river", 0.0, 0.0, -0.10, 0.35, -0.15, 0.0},
        {"minecraft:frozen_river", -0.8, 0.0, -0.10, 0.35, -0.15, 0.0},
        // ---- plan42 R2 (E-10): vanilla 1.21.4 biome parity 31->54 (registry order EmbeddedData minecraft:worldgen/biome 65; all keys
        // below verified present). oceans (temperature-split variants)
        {"minecraft:lukewarm_ocean", 0.5, 0.1, -0.28, -0.10, 0.20, 0.1},
        {"minecraft:cold_ocean", -0.5, 0.1, -0.28, -0.10, 0.20, -0.1},
        {"minecraft:deep_lukewarm_ocean", 0.5, 0.1, -0.45, -0.20, 0.35, 0.1},
        {"minecraft:deep_cold_ocean", -0.5, 0.1, -0.45, -0.20, 0.35, -0.1},
        {"minecraft:deep_frozen_ocean", -0.85, 0.1, -0.45, -0.20, 0.35, -0.3},
        {"minecraft:mushroom_fields", 0.2, 0.8, -0.35, -0.10, 0.10, 0.6},
        // jungle variants
        {"minecraft:bamboo_jungle", 0.9, 0.9, 0.10, -0.10, 0.00, 0.5},
        {"minecraft:sparse_jungle", 0.9, 0.7, 0.12, 0.10, 0.00, -0.2},
        // forest old-growth + flower variants
        {"minecraft:flower_forest", 0.1, 0.6, 0.12, 0.10, 0.00, 0.7},
        {"minecraft:old_growth_birch_forest", -0.1, 0.7, 0.14, 0.05, 0.00, 0.6},
        {"minecraft:old_growth_pine_taiga", -0.5, 0.7, 0.16, 0.00, 0.00, 0.3},
        {"minecraft:old_growth_spruce_taiga", -0.5, 0.7, 0.16, 0.00, 0.00, -0.3},
        {"minecraft:windswept_forest", 0.1, -0.1, 0.20, -0.40, -0.10, 0.3},
        // savanna plateau variants
        {"minecraft:savanna_plateau", 0.8, -0.2, 0.16, -0.30, -0.20, 0.2},
        {"minecraft:windswept_savanna", 0.8, -0.2, 0.18, -0.40, -0.10, -0.2},
        // snowy mountain variants
        {"minecraft:snowy_slopes", -0.7, 0.3, 0.20, -0.35, -0.40, -0.2},
        {"minecraft:ice_spikes", -0.9, -0.2, 0.14, 0.20, 0.00, 0.5},
        // swamp variant
        {"minecraft:mangrove_swamp", 0.6, 0.95, 0.02, 0.30, -0.10, 0.3},
        // cave biomes (depth>0 separates from surface points)
        {"minecraft:dripstone_caves", 0.2, 0.2, 0.12, 0.10, 0.30, 0.0},
        {"minecraft:lush_caves", 0.3, 0.8, 0.10, 0.20, 0.30, 0.2},
        {"minecraft:deep_dark", 0.0, 0.2, 0.10, 0.00, 0.50, -0.2},
        // badlands variants
        {"minecraft:eroded_badlands", 1.0, -0.5, 0.10, -0.30, 0.10, -0.2},
        {"minecraft:wooded_badlands", 1.0, -0.4, 0.10, -0.20, 0.10, 0.0},
        // ---- plan45 G-11: vanilla 65-biome table completion (54 -> 65). Missing 11 = nether 5 + end 5 + the_void 1, verified against
        // vanilla 1.21.4 client jar data/minecraft/worldgen/biome/*.json (65 files, piston-meta 1.21.4, sha1-verified download 2026-09-04).
        // Nether/end entries carry a dimension tag: sample() still searches the overworld subset only, so these can never leak into
        // overworld chunks. Emit paths: sampleNether()/sampleEnd(). Climate points are clean-room distinct points in the vanilla nether/end
        // parameter style (temperature/humidity-driven, cf. vanilla NetherBiomeSource).
    };
    for (const auto& bp : kPoints) {
        entries_.push_back({bp.key, {bp.temp, bp.humid, bp.continent, bp.erosion, bp.depth, bp.weird}});
        NoiseHypercube cube;
        auto r = [&](double v){ return ParameterRange{float(v-0.05), float(v+0.05)}; };
        cube.temperature = r(bp.temp); cube.humidity = r(bp.humid); cube.continentalness = r(bp.continent);
        cube.erosion = r(bp.erosion); cube.depth = r(bp.depth); cube.weirdness = r(bp.weird);
        entriesCube_.push_back({bp.key, cube});
    }
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
    // the_void: registry presence only (vanilla default for Y<-64 / ungenerated chunks). Excluded from all sampling by dimension filter.
    addSpecial("minecraft:the_void");
}

} // namespace cppfm::worldgen
