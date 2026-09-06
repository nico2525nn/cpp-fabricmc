package net.minecraft.block;

import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.random.Random;
import net.minecraft.world.World;
import net.minecraft.world.WorldView;

/** Stable 1.21.4 crop/plant growth contract used by Carpet and Fabric mods. */
public interface Fertilizable {
    default BlockPos getFertilizeParticlePos(BlockPos pos) { return pos; }
    boolean isFertilizable(WorldView world, BlockPos pos, BlockState state);
    boolean canGrow(World world, Random random, BlockPos pos, BlockState state);
    void grow(ServerWorld world, Random random, BlockPos pos, BlockState state);
    default FertilizableType getFertilizableType() { return FertilizableType.GROWER; }

    enum FertilizableType {
        NEIGHBOR_SPREADER,
        GROWER
    }
}
