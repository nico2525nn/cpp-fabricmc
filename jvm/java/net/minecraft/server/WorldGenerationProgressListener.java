package net.minecraft.server;

/** World-generation progress callback marker. */
public interface WorldGenerationProgressListener {
    default void start( int chunkX, int chunkZ) { }
    default void stop() { }
}
