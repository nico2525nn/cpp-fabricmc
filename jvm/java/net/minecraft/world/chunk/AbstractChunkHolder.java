package net.minecraft.world.chunk;

import java.util.concurrent.atomic.AtomicReferenceArray;
import net.minecraft.util.math.ChunkPos;

/** Common chunk-holder identity required by server chunk status mixins. */
public abstract class AbstractChunkHolder {
    protected final ChunkPos pos;
    /** Per-status futures table exposed by Lithium's Accessor mixin. */
    private final AtomicReferenceArray<Object> chunkFuturesByStatus =
        new AtomicReferenceArray<>(16);

    protected AbstractChunkHolder(ChunkPos pos) { this.pos = pos == null ? new ChunkPos(0, 0) : pos; }
    public ChunkPos getPos() { return pos; }
    /** Accessor/Invoker target used by Lithium's chunk-status scheduler. */
    public boolean cannotBeLoaded(ChunkStatus status) { return false; }
}
