package net.fabricmc.fabric.api.block.v1;

import net.minecraft.block.BlockState;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.world.BlockRenderView;

/** Fabric's appearance override hook injected into {@code Block}. */
public interface FabricBlock {
    default BlockState getAppearance(BlockState state, BlockRenderView renderView,
                                     BlockPos pos, Direction side,
                                     BlockState sourceState, BlockPos sourcePos) {
        return state;
    }
}
