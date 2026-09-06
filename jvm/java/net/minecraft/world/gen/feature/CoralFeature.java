package net.minecraft.world.gen.feature;

import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.random.Random;
import net.minecraft.world.WorldAccess;

/** 1.21.4 coral feature ABI used by Carpet's renewable-coral mixin. */
public class CoralFeature {
    protected void method_40029(WorldAccess world, BlockPos pos, Block block) { }
    protected void method_40030(Direction direction, WorldAccess world, BlockPos pos, Block block) { }

    protected boolean generateCoral(WorldAccess world, Random random, BlockPos pos, BlockState state) {
        return generateCoralPiece(world, random, pos, state);
    }

    protected boolean generateCoralPiece(WorldAccess world, Random random, BlockPos pos, BlockState state) {
        return true;
    }
}
