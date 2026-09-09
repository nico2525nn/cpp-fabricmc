package net.fabricmc.fabric.api.registry;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.block.Block;

/** Mutable copper oxidation and waxing pairs. */
public final class OxidizableBlocksRegistry {
    private static final Map<Block, Block> OXIDATION = new ConcurrentHashMap<>();
    private static final Map<Block, Block> WAXING = new ConcurrentHashMap<>();
    private OxidizableBlocksRegistry() { }
    public static void registerOxidizableBlockPair(Block lessOxidized, Block moreOxidized) {
        if (lessOxidized == null || moreOxidized == null) throw new NullPointerException("block pair");
        OXIDATION.put(lessOxidized, moreOxidized);
    }
    public static void registerWaxableBlockPair(Block unwaxed, Block waxed) {
        if (unwaxed == null || waxed == null) throw new NullPointerException("block pair");
        WAXING.put(unwaxed, waxed);
    }
    public static Block getOxidized(Block block) { return OXIDATION.get(block); }
    public static Block getWaxed(Block block) { return WAXING.get(block); }
}
