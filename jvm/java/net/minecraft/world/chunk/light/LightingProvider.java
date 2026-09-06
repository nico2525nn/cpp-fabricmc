package net.minecraft.world.chunk.light;

import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.ChunkSectionPos;
import net.minecraft.world.LightType;

/**
 * Server lighting facade.  The native engine remains authoritative; this
 * class supplies the 1.21.4 linkage and a conservative empty-light result.
 */
public class LightingProvider implements ChunkLightingView {
    protected final ChunkLightProvider skyLightProvider;
    protected final ChunkLightProvider blockLightProvider;
    public static final LightingProvider DEFAULT = new LightingProvider(null, false, false);
    protected final net.minecraft.world.World world;

    public LightingProvider(ChunkProvider chunkProvider, boolean hasBlockLight, boolean hasSkyLight) {
        this.world = null;
        this.blockLightProvider = hasBlockLight
            ? new ChunkLightProvider(chunkProvider, new LightStorage(LightType.BLOCK, chunkProvider, null)) : null;
        this.skyLightProvider = hasSkyLight
            ? new ChunkLightProvider(chunkProvider, new LightStorage(LightType.SKY, chunkProvider, null)) : null;
    }
    public void setRetainData(net.minecraft.util.math.ChunkPos pos, boolean retainData) {}
    public String displaySectionLevel(LightType lightType, ChunkSectionPos pos) { return "0"; }
    public int getLight(BlockPos pos, int ambientDarkness) { return 0; }
    public LightStorage.Status getStatus(LightType lightType, ChunkSectionPos pos) {
        ChunkLightProvider provider = getProvider(lightType);
        return provider == null ? LightStorage.Status.NOT_READY : provider.getStatus(pos.asLong());
    }
    public ChunkLightingView get(LightType lightType) {
        ChunkLightProvider provider = getProvider(lightType);
        return provider == null ? Empty.INSTANCE : provider;
    }
    public void enqueueSectionData(LightType lightType, ChunkSectionPos pos,
                                   net.minecraft.world.chunk.ChunkNibbleArray nibbles) {}
    public int getTopY() { return 320; }
    public int getHeight() { return 384; }
    public int getBottomY() { return -64; }
    public boolean isLightingEnabled(long sectionPos) { return true; }
    @Override public int getLightLevel(BlockPos pos) { return getLight(pos, 0); }
    @Override public net.minecraft.world.chunk.ChunkNibbleArray getLightSection(ChunkSectionPos pos) {
        return new net.minecraft.world.chunk.ChunkNibbleArray();
    }
    private ChunkLightProvider getProvider(LightType type) {
        return type == LightType.SKY ? skyLightProvider : blockLightProvider;
    }
    private enum Empty implements ChunkLightingView {
        INSTANCE;
        @Override public int getLightLevel(BlockPos pos) { return 0; }
        @Override public net.minecraft.world.chunk.ChunkNibbleArray getLightSection(ChunkSectionPos pos) {
            return new net.minecraft.world.chunk.ChunkNibbleArray();
        }
    }
}
