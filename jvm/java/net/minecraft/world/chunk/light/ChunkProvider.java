package net.minecraft.world.chunk.light;

import net.minecraft.util.math.ChunkSectionPos;
import net.minecraft.world.LightType;
import net.minecraft.world.BlockView;

/** Lighting's world/chunk callback surface. */
public interface ChunkProvider {
    default void onLightUpdate(LightType type, ChunkSectionPos pos) {}
    default BlockView getWorld() { return null; }
    default LightSourceView getChunk(int chunkX, int chunkZ) { return null; }
}
