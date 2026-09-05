package net.minecraft.util.hit;

import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.Vec3d;

public class BlockHitResult extends HitResult {
    private final BlockPos blockPos;
    private final Direction side;
    private final boolean insideBlock;

    public BlockHitResult(BlockPos blockPos) {
        this(new Vec3d(blockPos.getX() + 0.5, blockPos.getY() + 0.5, blockPos.getZ() + 0.5),
             Direction.UP, blockPos, false);
    }
    public BlockHitResult(BlockPos blockPos, Direction side) {
        this(new Vec3d(blockPos.getX() + 0.5, blockPos.getY() + 0.5, blockPos.getZ() + 0.5),
             side, blockPos, false);
    }
    public BlockHitResult(Vec3d pos, Direction side, BlockPos blockPos, boolean insideBlock) {
        super(pos);
        this.blockPos = blockPos == null ? new BlockPos(0, 0, 0) : blockPos;
        this.side = side == null ? Direction.UP : side;
        this.insideBlock = insideBlock;
    }
    private BlockHitResult(Vec3d pos, BlockPos blockPos, Direction side, boolean insideBlock) {
        this(pos, side, blockPos, insideBlock);
    }
    public Vec3d getPos() { return pos; }
    public BlockPos getBlockPos() { return blockPos; }
    public Direction getSide() { return side; }
    public boolean isInsideBlock() { return insideBlock; }
    public BlockHitResult withSide(Direction value) {
        return new BlockHitResult(pos, value, blockPos, insideBlock);
    }
    public BlockHitResult withBlockPos(BlockPos value) {
        return new BlockHitResult(pos, side, value, insideBlock);
    }
    @Override public Type getType() { return Type.BLOCK; }
}
