package net.fabricmc.fabric.api.block;

import net.minecraft.block.BlockState;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.util.Hand;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.world.World;

/**
 * Optional block hook invoked when a player attacks a block without breaking
 * it.  The interface is mixed into vanilla block implementations by Fabric;
 * declaring the exact hook here lets the Java compatibility layer dispatch
 * the same callback from a custom block implementation.
 */
@FunctionalInterface
public interface BlockAttackInteractionAware {
    boolean onAttackInteraction(BlockState state, World world, BlockPos pos,
                                PlayerEntity player, Hand hand, Direction face);
}
