package net.minecraft.world;

import net.minecraft.block.BlockState;
import net.minecraft.block.RedstoneWireBlock;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.block.WireOrientation;

/** Vanilla 1.21.4 redstone evaluator ABI; native redstone remains authoritative. */
public class RedstoneController {
    protected final RedstoneWireBlock wire;

    public RedstoneController(RedstoneWireBlock wire) { this.wire = wire; }
    protected RedstoneController() { this(null); }

    public int getStrongPowerAt(World world, BlockPos pos) {
        return calculateWirePowerAt(world, pos);
    }

    public int calculateWirePowerAt(World world, BlockPos pos) {
        return 0;
    }

    public void update(World world, BlockPos pos, BlockState state,
                       WireOrientation orientation, boolean blockAdded) {
        // The C++ redstone engine performs the actual propagation.
    }

    public int getWirePowerAt(BlockPos pos, BlockState state) {
        return 0;
    }
}
