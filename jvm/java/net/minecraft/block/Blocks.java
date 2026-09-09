package net.minecraft.block;

import net.minecraft.registry.RegistryKey;

public final class Blocks {
    private Blocks() {}

    /** Yarn 1.21.4 registry factory used by vanilla bootstrap mixins. */
    public static Block register(RegistryKey<Block> key, AbstractBlock.Settings settings) {
        return new Block(settings);
    }

    /** Yarn 1.21.4 registry factory overload used by Fabric's transitive AW. */
    public static Block register(RegistryKey<Block> key,
            java.util.function.Function<AbstractBlock.Settings, Block> factory,
            AbstractBlock.Settings settings) {
        return factory == null ? new Block(settings) : factory.apply(settings);
    }

    /** Vanilla's block-state luminance function factory. */
    public static java.util.function.ToIntFunction<BlockState> createLightLevelFromLitBlockState(int level) {
        return state -> level;
    }

    /** Context predicate helpers exposed by the 1.21.4 named ABI. */
    public static boolean always(BlockState state, net.minecraft.world.BlockView world,
            net.minecraft.util.math.BlockPos pos) { return true; }

    public static boolean never(BlockState state, net.minecraft.world.BlockView world,
            net.minecraft.util.math.BlockPos pos) { return false; }

    public static Boolean always(BlockState state, net.minecraft.world.BlockView world,
            net.minecraft.util.math.BlockPos pos, net.minecraft.entity.EntityType<?> type) {
        return Boolean.TRUE;
    }

    public static Boolean never(BlockState state, net.minecraft.world.BlockView world,
            net.minecraft.util.math.BlockPos pos, net.minecraft.entity.EntityType<?> type) {
        return Boolean.FALSE;
    }

    public static Boolean canSpawnOnLeaves(BlockState state, net.minecraft.world.BlockView world,
            net.minecraft.util.math.BlockPos pos, net.minecraft.entity.EntityType<?> type) {
        return Boolean.TRUE;
    }

    /**
     * Yarn 1.21.4 intermediary ABI used by Fabric's block access wideners.
     * Vanilla uses this helper when constructing log blocks; keeping the
     * mapping in the settings object is sufficient for the Java compatibility
     * layer while the native registry owns the resulting block state.
     */
    public static AbstractBlock.Settings createLogSettings(
            MapColor topMapColor, MapColor sideMapColor,
            net.minecraft.sound.BlockSoundGroup sounds) {
        return AbstractBlock.Settings.create()
            .mapColor(sideMapColor)
            .sounds(sounds);
    }

    /** Yarn 1.21.4 helper used by the transitive Fabric access widener. */
    public static AbstractBlock.Settings createNetherStemSettings(MapColor mapColor) {
        return AbstractBlock.Settings.create().mapColor(mapColor);
    }

    /** Yarn 1.21.4 helper used by the transitive Fabric access widener. */
    public static AbstractBlock.Settings createLeavesSettings(
            net.minecraft.sound.BlockSoundGroup sounds) {
        return AbstractBlock.Settings.create().sounds(sounds).nonOpaque();
    }

    /** Yarn 1.21.4 helper used by the transitive Fabric access widener. */
    public static AbstractBlock.Settings createFlowerPotSettings() {
        return AbstractBlock.Settings.create().nonOpaque().noCollision();
    }

    /** Yarn 1.21.4 helper used by the transitive Fabric access widener. */
    public static AbstractBlock.Settings createButtonSettings() {
        return AbstractBlock.Settings.create().noCollision();
    }

    public static final Block AIR = new Block(0);
    public static final Block VOID_AIR = new Block(0, net.minecraft.util.Identifier.of("minecraft", "void_air"));
    public static final Block STONE = new Block(1, net.minecraft.util.Identifier.of("minecraft", "stone"));
    public static final Block GRANITE = new Block(2, net.minecraft.util.Identifier.of("minecraft", "granite"));
    public static final Block DIORITE = new Block(3, net.minecraft.util.Identifier.of("minecraft", "diorite"));
    public static final Block ANDESITE = new Block(4, net.minecraft.util.Identifier.of("minecraft", "andesite"));
    public static final Block GRASS_BLOCK = new Block(5, net.minecraft.util.Identifier.of("minecraft", "grass_block"));
    public static final Block DIRT = new Block(6, net.minecraft.util.Identifier.of("minecraft", "dirt"));
    public static final Block COBBLESTONE = new Block(7, net.minecraft.util.Identifier.of("minecraft", "cobblestone"));
    public static final Block BEDROCK = new Block(8, net.minecraft.util.Identifier.of("minecraft", "bedrock"));
    public static final Block SAND = new Block(9, net.minecraft.util.Identifier.of("minecraft", "sand"));
    public static final Block GRAVEL = new Block(10, net.minecraft.util.Identifier.of("minecraft", "gravel"));
    public static final Block OAK_LOG = new Block(11, net.minecraft.util.Identifier.of("minecraft", "oak_log"));
    public static final Block OAK_PLANKS = new Block(12, net.minecraft.util.Identifier.of("minecraft", "oak_planks"));
    public static final Block OAK_LEAVES = new Block(13, net.minecraft.util.Identifier.of("minecraft", "oak_leaves"));
    public static final Block GLASS = new Block(14, net.minecraft.util.Identifier.of("minecraft", "glass"));
    public static final Block WATER = new Block(15, net.minecraft.util.Identifier.of("minecraft", "water"));
    public static final Block LAVA = new Block(16, net.minecraft.util.Identifier.of("minecraft", "lava"));
    public static final Block CRAFTING_TABLE = new Block(17, net.minecraft.util.Identifier.of("minecraft", "crafting_table"));
    public static final Block FURNACE = new Block(18, net.minecraft.util.Identifier.of("minecraft", "furnace"));
    public static final Block CHEST = new Block(19, net.minecraft.util.Identifier.of("minecraft", "chest"));
    public static final Block TORCH = new Block(20, net.minecraft.util.Identifier.of("minecraft", "torch"));
    public static final Block REDSTONE_BLOCK = new Block(21, net.minecraft.util.Identifier.of("minecraft", "redstone_block"));
    public static final Block DIAMOND_BLOCK = new Block(22, net.minecraft.util.Identifier.of("minecraft", "diamond_block"));
    public static final Block IRON_BLOCK = new Block(23, net.minecraft.util.Identifier.of("minecraft", "iron_block"));
    public static final Block GOLD_BLOCK = new Block(24, net.minecraft.util.Identifier.of("minecraft", "gold_block"));
    public static final Block EMERALD_BLOCK = new Block(25, net.minecraft.util.Identifier.of("minecraft", "emerald_block"));
    public static final Block OBSIDIAN = new Block(26, net.minecraft.util.Identifier.of("minecraft", "obsidian"));
    public static final Block SNOW_BLOCK = new Block(27, net.minecraft.util.Identifier.of("minecraft", "snow_block"));
    public static final Block WHITE_WOOL = new Block(28, net.minecraft.util.Identifier.of("minecraft", "white_wool"));
    public static final Block NETHERRACK = new Block(29, net.minecraft.util.Identifier.of("minecraft", "netherrack"));
    public static final Block SOUL_SAND = new Block(30, net.minecraft.util.Identifier.of("minecraft", "soul_sand"));
    public static final Block END_STONE = new Block(31, net.minecraft.util.Identifier.of("minecraft", "end_stone"));
    public static final Block BRICKS = new Block(32, net.minecraft.util.Identifier.of("minecraft", "bricks"));
    public static final Block BOOKSHELF = new Block(33, net.minecraft.util.Identifier.of("minecraft", "bookshelf"));
    public static final Block STRUCTURE_VOID = new Block(34, net.minecraft.util.Identifier.of("minecraft", "structure_void"));
    public static final Block PISTON = new PistonBlock(false, AbstractBlock.Settings.create());
    public static final Block PISTON_HEAD = new PistonHeadBlock(AbstractBlock.Settings.create());
    public static Block[] values() { return new Block[] { AIR, STONE, GRANITE, DIORITE, ANDESITE, GRASS_BLOCK, DIRT, COBBLESTONE, BEDROCK, SAND, GRAVEL, OAK_LOG, OAK_PLANKS, OAK_LEAVES, GLASS, WATER, LAVA, CRAFTING_TABLE, FURNACE, CHEST, TORCH, REDSTONE_BLOCK, DIAMOND_BLOCK, IRON_BLOCK, GOLD_BLOCK, EMERALD_BLOCK, OBSIDIAN, SNOW_BLOCK, WHITE_WOOL, NETHERRACK, SOUL_SAND, END_STONE, BRICKS, BOOKSHELF, STRUCTURE_VOID, PISTON, PISTON_HEAD }; }
}
