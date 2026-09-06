package net.minecraft.world.chunk.light;

import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.ChunkSectionPos;
import net.minecraft.world.chunk.ChunkNibbleArray;

public interface ChunkLightingView {
    int getLightLevel(BlockPos pos);
    ChunkNibbleArray getLightSection(ChunkSectionPos pos);
}
