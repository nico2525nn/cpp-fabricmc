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
}
