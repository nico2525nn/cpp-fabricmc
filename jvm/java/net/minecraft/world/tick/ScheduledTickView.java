package net.minecraft.world.tick;

import net.minecraft.block.Block;
import net.minecraft.util.math.BlockPos;

/**
 * Read-only scheduled-tick view used by the 1.21.4 block neighbour hook.
 * Native scheduling stays in the C++ world; these defaults keep the Java ABI
 * linkable for server mixins.
 */
public interface ScheduledTickView {
    default boolean hasScheduledTick(BlockPos pos, Block block) { return false; }
    default boolean isTicking(BlockPos pos, Block block) { return false; }
}
