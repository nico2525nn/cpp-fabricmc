package net.minecraft.util.math;

import java.util.Objects;

/** Immutable block coordinate used by the cppfm shadow API. */
public class BlockPos {
    protected int x;
    protected int y;
    protected int z;

    public BlockPos(int x, int y, int z) {
        this.x = x;
        this.y = y;
        this.z = z;
    }

    public int getX() { return x; }
    public int getY() { return y; }
    public int getZ() { return z; }
    public long asLong() { return asLong(x, y, z); }
    public static long asLong(int x, int y, int z) {
        return ((long) (x & 0x3ffffff) << 38) | ((long) (z & 0x3ffffff) << 12) | (y & 0xfffL);
    }
    public static int unpackLongX(long value) { return (int) (value >> 38); }
    public static int unpackLongY(long value) { return (int) (value << 52 >> 52); }
    public static int unpackLongZ(long value) { return (int) (value << 26 >> 38); }
    public static BlockPos fromLong(long value) { return new BlockPos(unpackLongX(value), unpackLongY(value), unpackLongZ(value)); }
    public static BlockPos ofFloored(double x, double y, double z) {
        return new BlockPos((int) Math.floor(x), (int) Math.floor(y), (int) Math.floor(z));
    }
    public BlockPos toImmutable() { return this; }
    public BlockPos multiply(int factor) { return new BlockPos(x * factor, y * factor, z * factor); }
    public BlockPos subtract(BlockPos other) {
        return other == null ? this : new BlockPos(x - other.x, y - other.y, z - other.z);
    }
    public BlockPos add(int dx, int dy, int dz) { return new BlockPos(x + dx, y + dy, z + dz); }
    public BlockPos offset(net.minecraft.util.math.Direction direction) {
        return add(direction.getOffsetX(), direction.getOffsetY(), direction.getOffsetZ());
    }
    public BlockPos up() { return offset(Direction.UP); }
    public BlockPos down() { return offset(Direction.DOWN); }
    public BlockPos north() { return offset(Direction.NORTH); }
    public BlockPos south() { return offset(Direction.SOUTH); }
    public BlockPos east() { return offset(Direction.EAST); }
    public BlockPos west() { return offset(Direction.WEST); }

    @Override public boolean equals(Object other) {
        if (!(other instanceof BlockPos pos)) return false;
        return x == pos.x && y == pos.y && z == pos.z;
    }
    @Override public int hashCode() { return Objects.hash(x, y, z); }
    @Override public String toString() { return "BlockPos{" + x + "," + y + "," + z + "}"; }

    public static class Mutable extends BlockPos {
        public Mutable() { this(0, 0, 0); }
        public Mutable(int x, int y, int z) { super(x, y, z); }
        public Mutable set(int x, int y, int z) { this.x = x; this.y = y; this.z = z; return this; }
        public Mutable set(BlockPos pos) { return pos == null ? set(0, 0, 0) : set(pos.getX(), pos.getY(), pos.getZ()); }
        @Override public BlockPos toImmutable() { return new BlockPos(x, y, z); }
    }
}
