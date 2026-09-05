package net.minecraft.block.entity;

import net.minecraft.util.math.BlockPos;

public class BlockEntity {
    private final BlockPos pos;
    public BlockEntity() { this(new BlockPos(0, 0, 0)); }
    public BlockEntity(BlockPos pos) { this.pos = pos; }
    public BlockPos getPos() { return pos; }
}
