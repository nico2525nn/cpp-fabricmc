package net.minecraft.block;

import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;
import net.minecraft.world.block.WireOrientation;

/** C++-backed redstone wire surface used by vanilla redstone mixins. */
public class RedstoneWireBlock extends Block {
    private final net.minecraft.world.RedstoneController redstoneController;
    /** Canonical 1.21.4 field name used by Carpet's Accessor mixin. */
    private boolean shouldSignal = true;
    /** Mojang-mapped field spelling used by Carpet's Accessor mixin. */
    private boolean wiresGivePower = true;

    public RedstoneWireBlock(AbstractBlock.Settings settings) {
        super(settings);
        this.redstoneController = new net.minecraft.world.RedstoneController(this);
    }

    public RedstoneWireBlock() { this(AbstractBlock.Settings.create()); }

    public void update(World world, BlockPos pos, BlockState state, WireOrientation orientation, boolean blockAdded) {
        redstoneController.update(world, pos, state, orientation, blockAdded);
    }
    /** Compatibility hook for redstone update mixins. */
    public void updateNeighbors(World world, BlockPos pos) {
        update(world, pos, world == null ? null : world.getBlockState(pos), null, false);
    }

    public BlockState updateLogicPublic(World world, BlockPos pos, BlockState state) { return state; }
    public void fastUpdate(World world, BlockPos pos, BlockState state, WireOrientation orientation, boolean blockAdded) {
        update(world, pos, state, orientation, blockAdded);
    }
    public boolean getWiresGivePower() { return wiresGivePower; }
    public void setWiresGivePower(boolean value) { shouldSignal = value; wiresGivePower = value; }
    public void onStateReplaced(BlockState state, World world, BlockPos pos,
                                 BlockState replacement, boolean moved) {
        if (world != null && pos != null && replacement != null)
            redstoneController.update(world, pos, replacement, null, moved);
    }
    public void onBlockAdded(BlockState state, World world, BlockPos pos,
                             BlockState oldState, boolean notify) {
        updateNeighbors(world, pos);
    }
    public void neighborUpdate(BlockState state, World world, BlockPos pos,
                               Block block, WireOrientation orientation, boolean notify) {
        update(world, pos, state, orientation, false);
    }
    public boolean connectsTo(BlockState state) { return state != null && !state.isAir(); }
    public int getStrongPower(World world, BlockPos pos) { return redstoneController.getStrongPowerAt(world, pos); }
}
