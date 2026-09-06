package net.minecraft.world.chunk;

/** Common ticking handle exposed by WorldChunk block-entity containers. */
public interface BlockEntityTickInvoker {
    default void tick() { }
    default boolean isRemoved() { return false; }
    default net.minecraft.util.math.BlockPos getPos() { return net.minecraft.util.math.BlockPos.ORIGIN; }
}
