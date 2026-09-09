package net.minecraft.block.entity;

import net.minecraft.block.BlockState;
import net.minecraft.util.math.BlockPos;

/** Minimal named sign entity required by the FakePlayer interaction ABI. */
public class SignBlockEntity extends BlockEntity {
    public SignBlockEntity() { super(); }
    public SignBlockEntity(BlockPos pos, BlockState state) { super(pos, state); }
}
