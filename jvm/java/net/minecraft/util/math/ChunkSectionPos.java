package net.minecraft.util.math;

/** Packed chunk-section coordinates used by the lighting ABI. */
public final class ChunkSectionPos {
    private final int x;
    private final int y;
    private final int z;

    public ChunkSectionPos(int x, int y, int z) {
        this.x = x; this.y = y; this.z = z;
    }

    public static ChunkSectionPos from(BlockPos pos) {
        return new ChunkSectionPos(pos.getX() >> 4, pos.getY() >> 4, pos.getZ() >> 4);
    }
    public static ChunkSectionPos from(ChunkPos pos, int sectionY) {
        return new ChunkSectionPos(pos.x, sectionY, pos.z);
    }
    public static ChunkSectionPos from(long packed) {
        return new ChunkSectionPos(unpackX(packed), unpackY(packed), unpackZ(packed));
    }
    public static long asLong(int x, int y, int z) {
        return ((long) x & 0x3fffffL) << 42
            | ((long) z & 0x3fffffL) << 20
            | ((long) y & 0xfffffL);
    }
    public long asLong() { return asLong(x, y, z); }
    public static int unpackX(long packed) { return (int) (packed >> 42); }
    public static int unpackY(long packed) { return (int) (packed << 44 >> 44); }
    public static int unpackZ(long packed) { return (int) (packed << 22 >> 42); }
    public int getSectionX() { return x; }
    public int getSectionY() { return y; }
    public int getSectionZ() { return z; }
    public int getMinX() { return x << 4; }
    public int getMinY() { return y << 4; }
    public int getMinZ() { return z << 4; }
    public int getMaxX() { return getMinX() + 15; }
    public int getMaxY() { return getMinY() + 15; }
    public int getMaxZ() { return getMinZ() + 15; }
    public BlockPos getMinPos() { return new BlockPos(getMinX(), getMinY(), getMinZ()); }
    public BlockPos unpackBlockPos(short packed) {
        return new BlockPos(getMinX() + ((packed >> 8) & 15),
            getMinY() + (packed & 15), getMinZ() + ((packed >> 4) & 15));
    }
    public static ChunkSectionPos from(ChunkPos pos) { return from(pos, 0); }
    public static int getSectionCoord(double coord) { return (int) Math.floor(coord) >> 4; }
    public static int getBlockCoord(int sectionCoord) { return sectionCoord << 4; }

    @Override public boolean equals(Object other) {
        return other instanceof ChunkSectionPos value && x == value.x && y == value.y && z == value.z;
    }
    @Override public int hashCode() { return (int) asLong() ^ (int) (asLong() >>> 32); }
    @Override public String toString() { return "[" + x + ", " + y + ", " + z + "]"; }
}
