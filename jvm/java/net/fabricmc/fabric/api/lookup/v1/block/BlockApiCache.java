package net.fabricmc.fabric.api.lookup.v1.block;

import net.minecraft.block.BlockState;
import net.minecraft.block.entity.BlockEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;

public interface BlockApiCache<A, C> {
    default A find(C context) { return find(getWorld().getBlockState(getPos()), context); }
    A find(BlockState state, C context);
    BlockEntity getBlockEntity();
    BlockApiLookup<A, C> getLookup();
    ServerWorld getWorld();
    BlockPos getPos();

    static <A, C> BlockApiCache<A, C> create(BlockApiLookup<A, C> lookup,
                                             ServerWorld world, BlockPos pos) {
        return new Impl<>(lookup, world, pos);
    }

    final class Impl<A, C> implements BlockApiCache<A, C> {
        private final BlockApiLookup<A, C> lookup;
        private final ServerWorld world;
        private final BlockPos pos;
        private Impl(BlockApiLookup<A, C> lookup, ServerWorld world, BlockPos pos) {
            this.lookup = lookup; this.world = world; this.pos = pos;
        }
        @Override public A find(BlockState state, C context) {
            return lookup == null ? null : lookup.find(world, pos, state, getBlockEntity(), context);
        }
        @Override public BlockEntity getBlockEntity() { return world == null ? null : world.getBlockEntity(pos); }
        @Override public BlockApiLookup<A, C> getLookup() { return lookup; }
        @Override public ServerWorld getWorld() { return world; }
        @Override public BlockPos getPos() { return pos; }
    }
}
