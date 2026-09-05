package net.minecraft.util.math;

import java.util.Objects;

public final class ChunkPos {
    public final int x;
    public final int z;
    public ChunkPos(int x, int z) { this.x = x; this.z = z; }
    public ChunkPos(BlockPos pos) { this(pos.getX() >> 4, pos.getZ() >> 4); }
    public long toLong() { return asLong(x, z); }
    public static long asLong(int x, int z) { return ((long) x & 0xffffffffL) | ((long) z << 32); }
    public static int getX(long value) { return (int) value; }
    public static int getZ(long value) { return (int) (value >>> 32); }
    public int getStartX() { return x << 4; }
    public int getStartZ() { return z << 4; }
    public int getEndX() { return (x << 4) + 15; }
    public int getEndZ() { return (z << 4) + 15; }
    public BlockPos getCenterAtY(int y) { return new BlockPos((x << 4) + 8, y, (z << 4) + 8); }
    @Override public boolean equals(Object other) { return other instanceof ChunkPos p && x == p.x && z == p.z; }
    @Override public int hashCode() { return Objects.hash(x, z); }
    @Override public String toString() { return "[" + x + ", " + z + "]"; }
}
