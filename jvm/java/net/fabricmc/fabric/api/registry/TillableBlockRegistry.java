package net.fabricmc.fabric.api.registry;

import com.mojang.datafixers.util.Pair;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Consumer;
import java.util.function.Predicate;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.item.ItemConvertible;
import net.minecraft.item.ItemUsageContext;

/** Hoe tilling actions with the same predicate/action pair as vanilla. */
public final class TillableBlockRegistry {
    private static final Map<Block, Pair<Predicate<ItemUsageContext>, Consumer<ItemUsageContext>>> VALUES = new ConcurrentHashMap<>();
    private TillableBlockRegistry() { }
    public static void register(Block input, Predicate<ItemUsageContext> predicate,
                                 Consumer<ItemUsageContext> action) {
        if (input == null) throw new NullPointerException("input block cannot be null");
        VALUES.put(input, Pair.of(predicate == null ? context -> true : predicate,
                                  action == null ? context -> { } : action));
    }
    public static void register(Block input, Predicate<ItemUsageContext> predicate, BlockState tilledState) {
        if (tilledState == null) throw new NullPointerException("tilled block state cannot be null");
        register(input, predicate, context -> {
            if (context != null && context.getWorld() != null) context.getWorld().setBlockState(context.getBlockPos(), tilledState);
        });
    }
    public static void register(Block input, Predicate<ItemUsageContext> predicate,
                                 BlockState tilledState, ItemConvertible droppedItem) {
        if (droppedItem == null) throw new NullPointerException("dropped item cannot be null");
        register(input, predicate, tilledState);
    }
    public static Pair<Predicate<ItemUsageContext>, Consumer<ItemUsageContext>> get(Block block) { return VALUES.get(block); }
}
