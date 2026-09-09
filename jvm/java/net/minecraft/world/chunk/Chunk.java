package net.minecraft.world.chunk;

import java.util.Collections;
import java.util.Map;
import net.minecraft.block.BlockState;
import net.minecraft.block.Blocks;
import net.minecraft.block.entity.BlockEntity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.ChunkPos;
import net.minecraft.world.BlockView;
import net.minecraft.world.HeightLimitView;

/** Minimal native-backed chunk ABI for server-side mixin class loading. */
public class Chunk implements BlockView, HeightLimitView,
        net.fabricmc.fabric.api.attachment.v1.AttachmentTarget {
    private final ChunkPos pos;

    public Chunk() { this(new ChunkPos(0, 0)); }
    public Chunk(ChunkPos pos) { this.pos = pos == null ? new ChunkPos(0, 0) : pos; }

    public ChunkPos getPos() { return pos; }
    @Override public int getBottomY() { return -64; }
    @Override public int getHeight() { return 384; }
    @Override public BlockState getBlockState(BlockPos blockPos) {
        return Blocks.AIR.getDefaultState();
    }
    public boolean isSectionEmpty(int sectionCoord) { return true; }
    public boolean needsSaving() { return false; }
    public boolean isLightOn() { return true; }
    public void markNeedsSaving() {}
    public Map<BlockPos, BlockEntity> getBlockEntities() { return Collections.emptyMap(); }
}
