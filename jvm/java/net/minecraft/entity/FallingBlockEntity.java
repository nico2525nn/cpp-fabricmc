package net.minecraft.entity;

import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;

/** Handle-free falling block entity used by server-side extensions. */
public class FallingBlockEntity extends Entity {
    private BlockState block = new BlockState(0);
    private BlockPos fallingBlockPos = BlockPos.ORIGIN;
    private boolean destroyedOnLanding;
    private boolean dropItem = true;
    private boolean hurtEntities;
    private int timeFalling;

    /** Canonical EntityType/World constructor used by server mixins. */
    public FallingBlockEntity(EntityType<?> type, World world) {
        super(type == null ? EntityType.UNKNOWN : type, world);
    }

    public FallingBlockEntity(World world, double x, double y, double z, BlockState state) {
        super(EntityType.UNKNOWN, world);
        setPosition(x, y, z);
        block = state == null ? new BlockState(0) : state;
        fallingBlockPos = BlockPos.ofFloored(x, y, z);
    }

    public BlockState getBlockState() { return block; }
    public BlockPos getFallingBlockPos() { return fallingBlockPos; }
    public void setFallingBlockPos(BlockPos pos) { fallingBlockPos = pos == null ? BlockPos.ORIGIN : pos; }
    public void setDestroyedOnLanding() { destroyedOnLanding = true; }
    public void setHurtEntities(float amount, int max) { hurtEntities = true; }
    public static FallingBlockEntity spawnFromBlock(World world, BlockPos pos, BlockState state) {
        BlockPos origin = pos == null ? BlockPos.ORIGIN : pos;
        return new FallingBlockEntity(world, origin.getX() + 0.5, origin.getY(), origin.getZ() + 0.5, state);
    }
    public void onDestroyedOnLanding(Block block, BlockPos pos) { destroyedOnLanding = true; }
    @Override public void tick() { super.tick(); timeFalling++; }
}
