package net.fabricmc.fabric.api.block.v1;

import net.minecraft.block.AbstractBlock;
import net.minecraft.block.Block;

/**
 * Small builder-compatible settings surface for common server-side blocks.
 * The native block table remains authoritative; these methods only preserve
 * the construction ABI used by the compatibility layer.
 */
public class FabricBlockSettings extends AbstractBlock.Settings {
    public FabricBlockSettings() { super(); }

    public static FabricBlockSettings create() { return new FabricBlockSettings(); }
    public static FabricBlockSettings copyOf(Block block) { return new FabricBlockSettings(); }
    public FabricBlockSettings strength(float hardness) { super.strength(hardness); return this; }
    public FabricBlockSettings strength(float hardness, float resistance) {
        super.strength(hardness, resistance); return this;
    }
    public FabricBlockSettings requiresTool() { super.requiresTool(); return this; }
    public FabricBlockSettings nonOpaque() { super.nonOpaque(); return this; }
    public FabricBlockSettings luminance(int value) { super.luminance(value); return this; }
    public FabricBlockSettings dropsNothing() { super.dropsNothing(); return this; }
    public FabricBlockSettings breakInstantly() { super.breakInstantly(); return this; }
    public FabricBlockSettings noCollision() { super.noCollision(); return this; }
    public FabricBlockSettings opaque() { super.opaque(); return this; }
    public FabricBlockSettings collidable(boolean value) { super.collidable(value); return this; }
    public FabricBlockSettings ticksRandomly() { super.ticksRandomly(); return this; }
    public FabricBlockSettings burnable() { super.burnable(); return this; }
    public FabricBlockSettings sounds(net.minecraft.block.BlockSoundGroup value) { super.sounds(value); return this; }
    public FabricBlockSettings mapColor(net.minecraft.block.MapColor value) { super.mapColor(value); return this; }
    public FabricBlockSettings slipperiness(float value) { super.slipperiness(value); return this; }
    public FabricBlockSettings velocityMultiplier(float value) { super.velocityMultiplier(value); return this; }
    public FabricBlockSettings pistonBehavior(net.minecraft.block.PistonBehavior value) { super.pistonBehavior(value); return this; }
}
