package net.fabricmc.fabric.api.tag.convention.v2;

import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.util.Identifier;
import net.minecraft.world.biome.Biome;

/** Generated from the Fabric v2 convention-tags 1.21.4 public catalog. */
public final class ConventionalBiomeTags {
    private ConventionalBiomeTags() { }

    private static TagKey<Biome> register(String path) {
        return TagKey.of(RegistryKeys.BIOME, Identifier.of("c", path));
    }

    public static final TagKey<Biome> NO_DEFAULT_MONSTERS = register("no_default_monsters");
    public static final TagKey<Biome> HIDDEN_FROM_LOCATOR_SELECTION = register("hidden_from_locator_selection");
    public static final TagKey<Biome> IS_VOID = register("is_void");
    public static final TagKey<Biome> IS_OVERWORLD = register("is_overworld");
    public static final TagKey<Biome> IS_HOT = register("is_hot");
    public static final TagKey<Biome> IS_HOT_OVERWORLD = register("is_hot/overworld");
    public static final TagKey<Biome> IS_HOT_NETHER = register("is_hot/nether");
    public static final TagKey<Biome> IS_HOT_END = register("is_hot/end");
    public static final TagKey<Biome> IS_TEMPERATE = register("is_temperate");
    public static final TagKey<Biome> IS_TEMPERATE_OVERWORLD = register("is_temperate/overworld");
    public static final TagKey<Biome> IS_TEMPERATE_NETHER = register("is_temperate/nether");
    public static final TagKey<Biome> IS_TEMPERATE_END = register("is_temperate/end");
    public static final TagKey<Biome> IS_COLD = register("is_cold");
    public static final TagKey<Biome> IS_COLD_OVERWORLD = register("is_cold/overworld");
    public static final TagKey<Biome> IS_COLD_NETHER = register("is_cold/nether");
    public static final TagKey<Biome> IS_COLD_END = register("is_cold/end");
    public static final TagKey<Biome> IS_WET = register("is_wet");
    public static final TagKey<Biome> IS_WET_OVERWORLD = register("is_wet/overworld");
    public static final TagKey<Biome> IS_WET_NETHER = register("is_wet/nether");
    public static final TagKey<Biome> IS_WET_END = register("is_wet/end");
    public static final TagKey<Biome> IS_DRY = register("is_dry");
    public static final TagKey<Biome> IS_DRY_OVERWORLD = register("is_dry/overworld");
    public static final TagKey<Biome> IS_DRY_NETHER = register("is_dry/nether");
    public static final TagKey<Biome> IS_DRY_END = register("is_dry/end");
    public static final TagKey<Biome> IS_VEGETATION_SPARSE = register("is_sparse_vegetation");
    public static final TagKey<Biome> IS_VEGETATION_SPARSE_OVERWORLD = register("is_sparse_vegetation/overworld");
    public static final TagKey<Biome> IS_VEGETATION_SPARSE_NETHER = register("is_sparse_vegetation/nether");
    public static final TagKey<Biome> IS_VEGETATION_SPARSE_END = register("is_sparse_vegetation/end");
    public static final TagKey<Biome> IS_VEGETATION_DENSE = register("is_dense_vegetation");
    public static final TagKey<Biome> IS_VEGETATION_DENSE_OVERWORLD = register("is_dense_vegetation/overworld");
    public static final TagKey<Biome> IS_VEGETATION_DENSE_NETHER = register("is_dense_vegetation/nether");
    public static final TagKey<Biome> IS_VEGETATION_DENSE_END = register("is_dense_vegetation/end");
    public static final TagKey<Biome> IS_CONIFEROUS_TREE = register("is_tree/coniferous");
    public static final TagKey<Biome> IS_SAVANNA_TREE = register("is_tree/savanna");
    public static final TagKey<Biome> IS_JUNGLE_TREE = register("is_tree/jungle");
    public static final TagKey<Biome> IS_DECIDUOUS_TREE = register("is_tree/deciduous");
    public static final TagKey<Biome> IS_MOUNTAIN = register("is_mountain");
    public static final TagKey<Biome> IS_MOUNTAIN_PEAK = register("is_mountain_peak");
    public static final TagKey<Biome> IS_MOUNTAIN_SLOPE = register("is_mountain_slope");
    public static final TagKey<Biome> IS_PLAINS = register("is_plains");
    public static final TagKey<Biome> IS_SNOWY_PLAINS = register("is_snowy_plains");
    public static final TagKey<Biome> IS_FOREST = register("is_forest");
    public static final TagKey<Biome> IS_BIRCH_FOREST = register("is_birch_forest");
    public static final TagKey<Biome> IS_DARK_FOREST = register("is_dark_forest");
    public static final TagKey<Biome> IS_FLOWER_FOREST = register("is_flower_forest");
    public static final TagKey<Biome> IS_TAIGA = register("is_taiga");
    public static final TagKey<Biome> IS_OLD_GROWTH = register("is_old_growth");
    public static final TagKey<Biome> IS_HILL = register("is_hill");
    public static final TagKey<Biome> IS_WINDSWEPT = register("is_windswept");
    public static final TagKey<Biome> IS_JUNGLE = register("is_jungle");
    public static final TagKey<Biome> IS_SAVANNA = register("is_savanna");
    public static final TagKey<Biome> IS_SWAMP = register("is_swamp");
    public static final TagKey<Biome> IS_DESERT = register("is_desert");
    public static final TagKey<Biome> IS_BADLANDS = register("is_badlands");
    public static final TagKey<Biome> IS_BEACH = register("is_beach");
    public static final TagKey<Biome> IS_STONY_SHORES = register("is_stony_shores");
    public static final TagKey<Biome> IS_MUSHROOM = register("is_mushroom");
    public static final TagKey<Biome> IS_RIVER = register("is_river");
    public static final TagKey<Biome> IS_OCEAN = register("is_ocean");
    public static final TagKey<Biome> IS_DEEP_OCEAN = register("is_deep_ocean");
    public static final TagKey<Biome> IS_SHALLOW_OCEAN = register("is_shallow_ocean");
    public static final TagKey<Biome> IS_UNDERGROUND = register("is_underground");
    public static final TagKey<Biome> IS_CAVE = register("is_cave");
    public static final TagKey<Biome> IS_WASTELAND = register("is_wasteland");
    public static final TagKey<Biome> IS_DEAD = register("is_dead");
    public static final TagKey<Biome> IS_LUSH = register("is_lush");
    public static final TagKey<Biome> IS_MAGICAL = register("is_magical");
    public static final TagKey<Biome> IS_RARE = register("is_rare");
    public static final TagKey<Biome> IS_PLATEAU = register("is_plateau");
    public static final TagKey<Biome> IS_SPOOKY = register("is_spooky");
    public static final TagKey<Biome> IS_FLORAL = register("is_floral");
    public static final TagKey<Biome> IS_SANDY = register("is_sandy");
    public static final TagKey<Biome> IS_SNOWY = register("is_snowy");
    public static final TagKey<Biome> IS_ICY = register("is_icy");
    public static final TagKey<Biome> IS_AQUATIC = register("is_aquatic");
    public static final TagKey<Biome> IS_AQUATIC_ICY = register("is_aquatic_icy");
    public static final TagKey<Biome> IS_NETHER = register("is_nether");
    public static final TagKey<Biome> IS_NETHER_FOREST = register("is_nether_forest");
    public static final TagKey<Biome> IS_END = register("is_end");
    public static final TagKey<Biome> IS_OUTER_END_ISLAND = register("is_outer_end_island");
}
