package net.fabricmc.fabric.api.registry;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;

/** Registry of shovel-style block flattening transitions. */
public final class FlattenableBlockRegistry {
    private static final Map<Block, BlockState> VALUES = new ConcurrentHashMap<>();
    private FlattenableBlockRegistry() { }
    public static void register(Block block, BlockState flattenedState) {
        if (block == null || flattenedState == null) throw new NullPointerException("block/state");
        VALUES.put(block, flattenedState);
    }
    public static BlockState get(Block block) { return block == null ? null : VALUES.get(block); }
    public static void clear() { VALUES.clear(); }
}
