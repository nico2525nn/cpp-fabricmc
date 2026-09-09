package net.fabricmc.fabric.api.tag.convention.v1;

import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.util.Identifier;
import net.minecraft.item.Item;

/** Generated from the Fabric v1 convention-tags 1.21.4 public catalog. */
public final class ConventionalItemTags {
    private ConventionalItemTags() { }

    private static TagKey<Item> register(String path) {
        return TagKey.of(RegistryKeys.ITEM, Identifier.of("c", path));
    }

    public static final TagKey<Item> SHEARS = register("shears");
    public static final TagKey<Item> SPEARS = register("spears");
    public static final TagKey<Item> BOWS = register("bows");
    public static final TagKey<Item> SHIELDS = register("shields");
    public static final TagKey<Item> DUSTS = register("dusts");
    public static final TagKey<Item> GEMS = register("gems");
    public static final TagKey<Item> INGOTS = register("ingots");
    public static final TagKey<Item> NUGGETS = register("nuggets");
    public static final TagKey<Item> ORES = register("ores");
    public static final TagKey<Item> RAW_ORES = register("raw_ores");
    public static final TagKey<Item> IRON_INGOTS = register("iron_ingots");
    public static final TagKey<Item> RAW_IRON_ORES = register("raw_iron_ores");
    public static final TagKey<Item> RAW_IRON_BLOCKS = register("raw_iron_blocks");
    public static final TagKey<Item> RAW_GOLD_ORES = register("raw_gold_ores");
    public static final TagKey<Item> RAW_GOLD_BLOCKS = register("raw_gold_blocks");
    public static final TagKey<Item> GOLD_INGOTS = register("gold_ingots");
    public static final TagKey<Item> REDSTONE_DUSTS = register("redstone_dusts");
    public static final TagKey<Item> COPPER_INGOTS = register("copper_ingots");
    public static final TagKey<Item> RAW_COPPER_ORES = register("raw_copper_ores");
    public static final TagKey<Item> RAW_COPPER_BLOCKS = register("raw_copper_blocks");
    public static final TagKey<Item> NETHERITE_INGOTS = register("netherite_ingots");
    public static final TagKey<Item> QUARTZ_ORES = register("quartz_ores");
    public static final TagKey<Item> QUARTZ = register("quartz");
    public static final TagKey<Item> LAPIS = register("lapis");
    public static final TagKey<Item> DIAMONDS = register("diamonds");
    public static final TagKey<Item> EMERALDS = register("emeralds");
    public static final TagKey<Item> COAL = register("coal");
    public static final TagKey<Item> FOODS = register("foods");
    public static final TagKey<Item> POTIONS = register("potions");
    public static final TagKey<Item> WATER_BUCKETS = register("water_buckets");
    public static final TagKey<Item> ENTITY_WATER_BUCKETS = register("entity_water_buckets");
    public static final TagKey<Item> LAVA_BUCKETS = register("lava_buckets");
    public static final TagKey<Item> MILK_BUCKETS = register("milk_buckets");
    public static final TagKey<Item> EMPTY_BUCKETS = register("empty_buckets");
    public static final TagKey<Item> BOOKSHELVES = register("bookshelves");
    public static final TagKey<Item> CHESTS = register("chests");
    public static final TagKey<Item> GLASS_BLOCKS = register("glass_blocks");
    public static final TagKey<Item> GLASS_PANES = register("glass_panes");
    public static final TagKey<Item> SHULKER_BOXES = register("shulker_boxes");
    public static final TagKey<Item> WOODEN_BARRELS = register("wooden_barrels");
    public static final TagKey<Item> BUDDING_BLOCKS = register("budding_blocks");
    public static final TagKey<Item> BUDS = register("buds");
    public static final TagKey<Item> CLUSTERS = register("clusters");
    public static final TagKey<Item> VILLAGER_JOB_SITES = register("villager_job_sites");
    public static final TagKey<Item> SANDSTONE_BLOCKS = register("sandstone_blocks");
    public static final TagKey<Item> SANDSTONE_SLABS = register("sandstone_slabs");
    public static final TagKey<Item> SANDSTONE_STAIRS = register("sandstone_stairs");
    public static final TagKey<Item> RED_SANDSTONE_BLOCKS = register("red_sandstone_blocks");
    public static final TagKey<Item> RED_SANDSTONE_SLABS = register("red_sandstone_slabs");
    public static final TagKey<Item> RED_SANDSTONE_STAIRS = register("red_sandstone_stairs");
    public static final TagKey<Item> UNCOLORED_SANDSTONE_BLOCKS = register("uncolored_sandstone_blocks");
    public static final TagKey<Item> UNCOLORED_SANDSTONE_SLABS = register("uncolored_sandstone_slabs");
    public static final TagKey<Item> UNCOLORED_SANDSTONE_STAIRS = register("uncolored_sandstone_stairs");
    public static final TagKey<Item> DYES = register("dyes");
    public static final TagKey<Item> BLACK_DYES = register("black_dyes");
    public static final TagKey<Item> BLUE_DYES = register("blue_dyes");
    public static final TagKey<Item> BROWN_DYES = register("brown_dyes");
    public static final TagKey<Item> CYAN_DYES = register("cyan_dyes");
    public static final TagKey<Item> GRAY_DYES = register("gray_dyes");
    public static final TagKey<Item> GREEN_DYES = register("green_dyes");
    public static final TagKey<Item> LIGHT_BLUE_DYES = register("light_blue_dyes");
    public static final TagKey<Item> LIGHT_GRAY_DYES = register("light_gray_dyes");
    public static final TagKey<Item> LIME_DYES = register("lime_dyes");
    public static final TagKey<Item> MAGENTA_DYES = register("magenta_dyes");
    public static final TagKey<Item> ORANGE_DYES = register("orange_dyes");
    public static final TagKey<Item> PINK_DYES = register("pink_dyes");
    public static final TagKey<Item> PURPLE_DYES = register("purple_dyes");
    public static final TagKey<Item> RED_DYES = register("red_dyes");
    public static final TagKey<Item> WHITE_DYES = register("white_dyes");
    public static final TagKey<Item> YELLOW_DYES = register("yellow_dyes");
    public static final TagKey<Item> WATER_BUCKET = register("water_bucket");
    public static final TagKey<Item> LAVA_BUCKET = register("lava_bucket");
    public static final TagKey<Item> MILK_BUCKET = register("milk_bucket");
    public static final TagKey<Item> EMPTY_BUCKET = register("empty_bucket");
    public static final TagKey<Item> PICKAXES = register("pickaxes");
    public static final TagKey<Item> SHOVELS = register("shovels");
    public static final TagKey<Item> HOES = register("hoes");
    public static final TagKey<Item> AXES = register("axes");
    public static final TagKey<Item> SWORDS = register("swords");
}
