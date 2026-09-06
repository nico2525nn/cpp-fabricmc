package net.minecraft.world.chunk.light;

import net.minecraft.block.BlockState;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.ChunkSectionPos;
import net.minecraft.util.math.Direction;
import net.minecraft.world.chunk.ChunkNibbleArray;

/** Base lighting provider ABI exposed by the 1.21.4 shadow layer. */
public class ChunkLightProvider implements ChunkLightingView {
    protected LightSourceView[] cachedChunks = new LightSourceView[0];
    protected final ChunkProvider chunkProvider;
    protected final Direction[] DIRECTIONS = Direction.values();
    protected final Object blockPositionsToCheck = new Object();
    protected final LightStorage lightStorage;
    protected long[] cachedChunkPositions = new long[0];

    public ChunkLightProvider(ChunkProvider chunkProvider, LightStorage lightStorage) {
        this.chunkProvider = chunkProvider;
        this.lightStorage = lightStorage;
    }
    public void clearChunkCache() { cachedChunks = new LightSourceView[0]; cachedChunkPositions = new long[0]; }
    public BlockState getStateForLighting(BlockPos pos) { return new BlockState(0); }
    @Override public int getLightLevel(BlockPos pos) { return lightStorage.getLightLevel(pos); }
    public LightStorage.Status getStatus(long sectionPos) { return lightStorage.getStatus(sectionPos); }
    public int getOpacity(BlockState state) { return state == null ? 0 : state.getOpacity(); }
    @Override public ChunkNibbleArray getLightSection(ChunkSectionPos pos) {
        return lightStorage.getLightSection(pos);
    }
    public int getRealisticOpacity(BlockState state1, BlockState state2, Direction direction, int opacity2) { return opacity2; }
    public void enqueueSectionData(long sectionPos, ChunkNibbleArray lightArray) {}
    public String displaySectionLevel(long sectionPos) { return "0"; }
    public LightSourceView getChunk(int chunkX, int chunkZ) { return chunkProvider.getChunk(chunkX, chunkZ); }
    public void setRetainColumn(net.minecraft.util.math.ChunkPos pos, boolean retainData) {}
    public void checkBlock(long blockPos) {}
    public void checkBlock(long blockPos, long flags) {}
    public boolean isTrivialForLighting(BlockState state) { return true; }
    public boolean needsLightUpdate(BlockState oldState, BlockState newState) { return oldState != newState; }
    public boolean shapesCoverFullCube(BlockState source, BlockState target, Direction direction) { return false; }
    public void propagateLight(long blockPos, long flags) {}
    public void propagateLight(long blockPos, long flags, int lightLevel) {}
}
