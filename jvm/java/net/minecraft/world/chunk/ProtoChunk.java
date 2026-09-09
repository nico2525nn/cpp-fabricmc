package net.minecraft.world.chunk;

import net.minecraft.util.math.ChunkPos;

/** Minimal proto-chunk identity used by the 1.21.4 WorldChunk ABI. */
public class ProtoChunk extends Chunk {
    public ProtoChunk() { super(); }
    public ProtoChunk(ChunkPos pos) { super(pos); }
}
