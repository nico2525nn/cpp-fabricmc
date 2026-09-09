package net.fabricmc.fabric.api.block.v1;

import net.minecraft.block.BlockState;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.world.BlockRenderView;

/** State-side forwarding hook for Fabric's block appearance API. */
public interface FabricBlockState {
    default BlockState getAppearance(BlockRenderView renderView, BlockPos pos,
                                     Direction side, BlockState sourceState,
                                     BlockPos sourcePos) {
        if (this instanceof BlockState state)
            return state.getBlock().getAppearance(state, renderView, pos, side,
                                                  sourceState, sourcePos);
        return null;
    }
}
