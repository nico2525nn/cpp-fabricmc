package net.minecraft.server.world;

/** Chunk error callback marker used by storage constructors. */
public interface ChunkErrorHandler {
    default void handle(long chunkPos, Throwable error) { }
}
