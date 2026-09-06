package net.minecraft.world;

import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;

/** 1.21.4 redstone query surface shared by world implementations. */
public interface RedstoneView {
    default boolean isEmittingRedstonePower(BlockPos pos, Direction direction) {
        return getEmittedRedstonePower(pos, direction) > 0;
    }
    default int getStrongRedstonePower(BlockPos pos, Direction direction) { return 0; }
    default int getEmittedRedstonePower(BlockPos pos, Direction direction) { return 0; }
    default boolean isReceivingRedstonePower(BlockPos pos) { return false; }
}
