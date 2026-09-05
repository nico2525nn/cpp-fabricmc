package net.minecraft.util.math;

import java.util.Objects;

/** Immutable block coordinate used by the cppfm shadow API. */
public class BlockPos {
    protected final int x;
    protected final int y;
    protected final int z;

    public BlockPos(int x, int y, int z) {
        this.x = x;
        this.y = y;
        this.z = z;
    }

    public int getX() { return x; }
    public int getY() { return y; }
    public int getZ() { return z; }
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
}
