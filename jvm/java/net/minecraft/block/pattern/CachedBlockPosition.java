package net.minecraft.block.pattern;

import java.util.function.Predicate;
import net.minecraft.block.BlockState;
import net.minecraft.block.entity.BlockEntity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.BlockView;

/** Lightweight named counterpart of vanilla's cached block-position probe. */
public final class CachedBlockPosition {
    private final BlockView world;
    private final BlockPos pos;
    private final boolean forceLoad;
    private BlockState state;
    private BlockEntity blockEntity;

    public CachedBlockPosition(BlockView world, BlockPos pos, boolean forceLoad) {
        this.world = world;
        this.pos = pos == null ? new BlockPos(0, 0, 0) : pos;
        this.forceLoad = forceLoad;
    }

    public BlockPos getBlockPos() { return pos; }
    public BlockState getBlockState() {
        if (state == null && world != null) state = world.getBlockState(pos);
        return state;
    }
    public BlockEntity getBlockEntity() { return blockEntity; }
    public boolean hasWorld() { return world != null; }
    public boolean isForceLoad() { return forceLoad; }
    public boolean test(Predicate<CachedBlockPosition> predicate) { return predicate != null && predicate.test(this); }
    public static Predicate<CachedBlockPosition> matchesBlockState(Predicate<BlockState> predicate) {
        return position -> position != null && predicate != null && predicate.test(position.getBlockState());
    }
}
