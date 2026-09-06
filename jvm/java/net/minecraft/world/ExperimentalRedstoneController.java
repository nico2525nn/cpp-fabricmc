package net.minecraft.world;

import net.minecraft.block.BlockState;
import net.minecraft.block.RedstoneWireBlock;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.world.block.WireOrientation;

/** Experimental redstone evaluator ABI retained for 1.21.4 mixin targets. */
public class ExperimentalRedstoneController extends RedstoneController {
    public ExperimentalRedstoneController(RedstoneWireBlock wire) { super(wire); }
    public ExperimentalRedstoneController() { super(); }

    public void spreadPowerUpdateToNeighbors(World world, BlockPos pos, int power,
                                             WireOrientation orientation, boolean canIncreasePower) { }
    public void spreadPowerUpdateTo(World world, BlockPos neighborPos, int power,
                                    WireOrientation orientation, boolean canIncreasePower) { }
    public void propagatePowerUpdates(World world, BlockPos pos, WireOrientation orientation) { }
    public void update(World world) { }
    public int unpackPower(int packed) { return packed & 15; }
    public WireOrientation unpackOrientation(int packed) { return WireOrientation.fromOrdinal(packed >>> 4); }
    public boolean canProvidePowerTo(BlockState wireState, Direction direction) { return true; }
    public void updatePowerAt(BlockPos pos, int power, WireOrientation defaultOrientation) { }
    public int packOrientationAndPower(WireOrientation orientation, int power) {
        return ((orientation == null ? 0 : orientation.ordinal()) << 4) | (power & 15);
    }
    public WireOrientation tweakOrientation(World world, WireOrientation orientation) { return orientation; }
}
