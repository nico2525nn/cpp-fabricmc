package net.fabricmc.fabric.api.registry;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.block.Block;

/** Axe stripping transitions. */
public final class StrippableBlockRegistry {
    private static final Map<Block, Block> VALUES = new ConcurrentHashMap<>();
    private StrippableBlockRegistry() { }
    public static void register(Block input, Block stripped) {
        if (input == null || stripped == null) throw new NullPointerException("block pair");
        VALUES.put(input, stripped);
    }
    public static Block get(Block input) { return VALUES.get(input); }
}
