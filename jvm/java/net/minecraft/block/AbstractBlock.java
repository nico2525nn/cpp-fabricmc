package net.minecraft.block;

import java.util.function.BiPredicate;
import java.util.function.Function;
import java.util.function.ToIntFunction;

/** Mutable construction settings; the native world remains authoritative after registration. */
public abstract class AbstractBlock {
    /** The construction settings exposed to block subclasses and access wideners. */
    protected final Settings settings;

    protected AbstractBlock(Settings settings) {
        this.settings = settings == null ? Settings.create() : settings;
    }

    /** Compatibility constructor for older shadow subclasses. */
    @Deprecated
    protected AbstractBlock() { this(Settings.create()); }

    public Settings getSettings() { return settings; }

    public static class Settings {
        private float hardness = 1.0f;
        private float resistance = 1.0f;
        private int luminance;
        private boolean requiresTool, opaque = true, collidable = true, dropsNothing;
        private boolean randomTicks, burnable;
        private float slipperiness = 0.6f, velocityMultiplier = 1.0f;
        private BlockSoundGroup sounds = BlockSoundGroup.STONE;
        private MapColor mapColor = MapColor.CLEAR;
        private PistonBehavior pistonBehavior = PistonBehavior.NORMAL;
        protected Settings() {}
        public static Settings create() { return new Settings(); }
        public Settings strength(float hardness) { return strength(hardness, hardness); }
        public Settings strength(float hardness, float resistance) { this.hardness = hardness; this.resistance = resistance; return this; }
        public Settings breakInstantly() { hardness = 0.0f; resistance = 0.0f; return this; }
        public Settings requiresTool() { requiresTool = true; return this; }
        public Settings nonOpaque() { opaque = false; return this; }
        public Settings opaque() { opaque = true; return this; }
        public Settings noCollision() { collidable = false; return this; }
        public Settings collidable(boolean value) { collidable = value; return this; }
        public Settings luminance(int value) { luminance = Math.max(0, Math.min(15, value)); return this; }
        public Settings luminance(ToIntFunction<BlockState> value) { luminance = value == null ? 0 : 15; return this; }
        public Settings dropsNothing() { dropsNothing = true; return this; }
        public Settings ticksRandomly() { randomTicks = true; return this; }
        public Settings burnable() { burnable = true; return this; }
        public Settings sounds(BlockSoundGroup value) { sounds = value == null ? BlockSoundGroup.STONE : value; return this; }
        /** Canonical 1.21.4 package overload; the block-package alias remains source compatible. */
        public Settings sounds(net.minecraft.sound.BlockSoundGroup value) {
            sounds = value instanceof BlockSoundGroup legacy
                ? legacy : new BlockSoundGroup(value == null ? "stone" : value.name());
            return this;
        }
        public Settings mapColor(MapColor value) { mapColor = value == null ? MapColor.CLEAR : value; return this; }
        public Settings mapColor(Function<BlockState, MapColor> value) { return mapColor(MapColor.CLEAR); }
        public Settings slipperiness(float value) { slipperiness = value; return this; }
        public Settings velocityMultiplier(float value) { velocityMultiplier = value; return this; }
        public Settings jumpVelocityMultiplier(float value) { return this; }
        public Settings pistonBehavior(PistonBehavior value) { pistonBehavior = value == null ? PistonBehavior.NORMAL : value; return this; }
        /** Canonical 1.21.4 package overload; the block-package alias remains source compatible. */
        public Settings pistonBehavior(net.minecraft.block.piston.PistonBehavior value) {
            pistonBehavior = value == null ? PistonBehavior.NORMAL : PistonBehavior.valueOf(value.name());
            return this;
        }
        public Settings solid() { return this; }
        public Settings suffocates(BiPredicate<BlockState, net.minecraft.world.World> value) { return this; }
        public Settings blocksVision(BiPredicate<BlockState, net.minecraft.world.World> value) { return this; }
        public Settings allowsSpawning(BiPredicate<BlockState, net.minecraft.world.World> value) { return this; }
        public Settings emissiveLighting(BiPredicate<BlockState, net.minecraft.world.World> value) { return this; }
        public Settings offset(Object type, float maxX, float maxZ) { return this; }
        public float hardness() { return hardness; }
        public float resistance() { return resistance; }
        public int luminance() { return luminance; }
        public boolean requiresToolValue() { return requiresTool; }
        public boolean opaqueValue() { return opaque; }
        public boolean collidableValue() { return collidable; }
        public boolean dropsNothingValue() { return dropsNothing; }
        public boolean randomTicks() { return randomTicks; }
        public boolean isBurnable() { return burnable; }
        public float slipperiness() { return slipperiness; }
        public float velocityMultiplier() { return velocityMultiplier; }
        public BlockSoundGroup sounds() { return sounds; }
        public MapColor mapColor() { return mapColor; }
        public PistonBehavior pistonBehavior() { return pistonBehavior; }
    }
}
