package net.minecraft.block.entity;

import net.minecraft.util.math.BlockPos;
import net.minecraft.block.BlockState;
import net.minecraft.world.World;

public class BlockEntity {
    private final BlockPos pos;
    private World world;
    private BlockState cachedState;
    public BlockEntity() { this(new BlockPos(0, 0, 0)); }
    public BlockEntity(BlockPos pos) { this(pos, new BlockState(0)); }
    public BlockEntity(BlockPos pos, BlockState state) {
        this.pos = pos == null ? new BlockPos(0, 0, 0) : pos;
        this.cachedState = state == null ? new BlockState(0) : state;
    }
    public BlockEntity(BlockEntityType<?> type, BlockPos pos, BlockState state) { this(pos, state); }
    public BlockPos getPos() { return pos; }
    public World getWorld() { return world; }
    public void setWorld(World world) { this.world = world; }
    public BlockState getCachedState() { return cachedState; }
    public void setCachedState(BlockState state) {
        cachedState = state == null ? new BlockState(0) : state;
    }
    /** Vanilla lifecycle hook used when a block entity leaves its chunk. */
    public void markRemoved() { }
    public void markDirty() { }
}
