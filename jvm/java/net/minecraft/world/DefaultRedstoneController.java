package net.minecraft.world;

import net.minecraft.block.RedstoneWireBlock;
import net.minecraft.util.math.BlockPos;

/** Vanilla default redstone evaluator surface. */
public class DefaultRedstoneController extends RedstoneController {
    public DefaultRedstoneController(RedstoneWireBlock wire) { super(wire); }
    public DefaultRedstoneController() { super(); }

    public int calculateTotalPowerAt(World world, BlockPos pos) {
        return calculateWirePowerAt(world, pos);
    }
}
