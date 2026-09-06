package net.minecraft.world.chunk.light;

import net.minecraft.util.math.ChunkSectionPos;
import net.minecraft.world.LightType;
import net.minecraft.world.chunk.ChunkNibbleArray;

/** Shared storage shell used by LightingProvider and Mixin shadow fields. */
public class LightStorage implements ChunkLightingView {
    public enum Status { LIGHT, NOT_READY, LIGHTED }
    protected final LightType lightType;

    public LightStorage(LightType lightType, ChunkProvider chunkProvider, Object lightData) {
        this.lightType = lightType;
    }
    @Override public int getLightLevel(net.minecraft.util.math.BlockPos pos) { return 0; }
    @Override public ChunkNibbleArray getLightSection(ChunkSectionPos pos) { return new ChunkNibbleArray(); }
    public ChunkNibbleArray getLightSection(long sectionPos) { return new ChunkNibbleArray(); }
    public ChunkNibbleArray getLightSection(long sectionPos, boolean cached) { return getLightSection(sectionPos); }
    public Status getStatus(long sectionPos) { return Status.LIGHT; }
    public boolean hasSection(long sectionPos) { return false; }
    public boolean hasLightUpdates() { return false; }
    public int get(long blockPos) { return 0; }
    public int getLight(long blockPos) { return get(blockPos); }
    public void set(long blockPos, int value) {}
    public void notifyChanges() {}
    public void onLoadSection(long sectionPos) {}
    public void onUnloadSection(long sectionPos) {}
}
