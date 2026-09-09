package net.fabricmc.fabric.api.tag.convention.v1;

import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.util.Identifier;
import net.minecraft.block.Block;

/** Generated from the Fabric v1 convention-tags 1.21.4 public catalog. */
public final class ConventionalBlockTags {
    private ConventionalBlockTags() { }

    private static TagKey<Block> register(String path) {
        return TagKey.of(RegistryKeys.BLOCK, Identifier.of("c", path));
    }

    public static final TagKey<Block> ORES = register("ores");
    public static final TagKey<Block> QUARTZ_ORES = register("quartz_ores");
    public static final TagKey<Block> BOOKSHELVES = register("bookshelves");
    public static final TagKey<Block> CHESTS = register("chests");
    public static final TagKey<Block> GLASS_BLOCKS = register("glass_blocks");
    public static final TagKey<Block> GLASS_PANES = register("glass_panes");
    public static final TagKey<Block> SHULKER_BOXES = register("shulker_boxes");
    public static final TagKey<Block> WOODEN_BARRELS = register("wooden_barrels");
    public static final TagKey<Block> BUDDING_BLOCKS = register("budding_blocks");
    public static final TagKey<Block> BUDS = register("buds");
    public static final TagKey<Block> CLUSTERS = register("clusters");
    public static final TagKey<Block> VILLAGER_JOB_SITES = register("villager_job_sites");
    public static final TagKey<Block> SANDSTONE_BLOCKS = register("sandstone_blocks");
    public static final TagKey<Block> SANDSTONE_SLABS = register("sandstone_slabs");
    public static final TagKey<Block> SANDSTONE_STAIRS = register("sandstone_stairs");
    public static final TagKey<Block> RED_SANDSTONE_BLOCKS = register("red_sandstone_blocks");
    public static final TagKey<Block> RED_SANDSTONE_SLABS = register("red_sandstone_slabs");
    public static final TagKey<Block> RED_SANDSTONE_STAIRS = register("red_sandstone_stairs");
    public static final TagKey<Block> UNCOLORED_SANDSTONE_BLOCKS = register("uncolored_sandstone_blocks");
    public static final TagKey<Block> UNCOLORED_SANDSTONE_SLABS = register("uncolored_sandstone_slabs");
    public static final TagKey<Block> UNCOLORED_SANDSTONE_STAIRS = register("uncolored_sandstone_stairs");
    public static final TagKey<Block> MOVEMENT_RESTRICTED = register("movement_restricted");
}
