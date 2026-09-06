package net.minecraft.world.chunk;

/** Minimal identity object for the 1.21.4 chunk-status ABI. */
public final class ChunkStatus {
    public static final ChunkStatus EMPTY = new ChunkStatus("empty");
    public static final ChunkStatus FULL = new ChunkStatus("full");
    private final String id;
    public ChunkStatus(String id) { this.id = id == null ? "" : id; }
    public String getId() { return id; }
    @Override public String toString() { return id; }
}
