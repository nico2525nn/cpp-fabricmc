package net.minecraft.util.math;

import java.util.Objects;

/** Immutable integer vector base shared by BlockPos and chunk coordinates. */
public class Vec3i implements Comparable<Vec3i> {
    protected int x;
    protected int y;
    protected int z;
    public static final Vec3i ZERO = new Vec3i(0, 0, 0);

    public Vec3i(int x, int y, int z) { this.x = x; this.y = y; this.z = z; }
    public int getX() { return x; }
    public int getY() { return y; }
    public int getZ() { return z; }
    public Vec3i add(int dx, int dy, int dz) { return new Vec3i(x + dx, y + dy, z + dz); }
    public Vec3i add(Vec3i other) { return other == null ? this : add(other.x, other.y, other.z); }
    public Vec3i subtract(Vec3i other) {
        return other == null ? this : new Vec3i(x - other.x, y - other.y, z - other.z);
    }
    public Vec3i multiply(int factor) { return new Vec3i(x * factor, y * factor, z * factor); }
    public Vec3i offset(Direction direction) {
        return direction == null ? this : add(direction.getOffsetX(), direction.getOffsetY(), direction.getOffsetZ());
    }
    public Vec3i offset(Direction direction, int distance) {
        return direction == null ? this : add(direction.getOffsetX() * distance, direction.getOffsetY() * distance,
            direction.getOffsetZ() * distance);
    }
    public Vec3i up() { return offset(Direction.UP); }
    public Vec3i up(int distance) { return offset(Direction.UP, distance); }
    public Vec3i down() { return offset(Direction.DOWN); }
    public Vec3i down(int distance) { return offset(Direction.DOWN, distance); }
    public Vec3i north() { return offset(Direction.NORTH); }
    public Vec3i north(int distance) { return offset(Direction.NORTH, distance); }
    public Vec3i south() { return offset(Direction.SOUTH); }
    public Vec3i south(int distance) { return offset(Direction.SOUTH, distance); }
    public Vec3i east() { return offset(Direction.EAST); }
    public Vec3i east(int distance) { return offset(Direction.EAST, distance); }
    public Vec3i west() { return offset(Direction.WEST); }
    public Vec3i west(int distance) { return offset(Direction.WEST, distance); }
    public int getManhattanDistance(Vec3i other) {
        return other == null ? Math.abs(x) + Math.abs(y) + Math.abs(z)
            : Math.abs(x - other.x) + Math.abs(y - other.y) + Math.abs(z - other.z);
    }
    public int getChebyshevDistance(Vec3i other) {
        return other == null ? Math.max(Math.max(Math.abs(x), Math.abs(y)), Math.abs(z))
            : Math.max(Math.max(Math.abs(x - other.x), Math.abs(y - other.y)), Math.abs(z - other.z));
    }
    public double getSquaredDistance(Vec3i other) {
        if (other == null) return x * (double) x + y * (double) y + z * (double) z;
        double dx = x - other.x, dy = y - other.y, dz = z - other.z;
        return dx * dx + dy * dy + dz * dz;
    }
    public double getSquaredDistance(double x, double y, double z) {
        double dx = this.x - x, dy = this.y - y, dz = this.z - z;
        return dx * dx + dy * dy + dz * dz;
    }
    public boolean isWithinDistance(Vec3i other, double distance) {
        return other != null && getSquaredDistance(other) < distance * distance;
    }
    public Vec3i crossProduct(Vec3i other) {
        return other == null ? ZERO : new Vec3i(y * other.z - z * other.y, z * other.x - x * other.z,
            x * other.y - y * other.x);
    }
    public String toShortString() { return x + "," + y + "," + z; }
    public int getComponentAlongAxis(Direction.Axis axis) {
        return axis == Direction.Axis.X ? x : axis == Direction.Axis.Y ? y : z;
    }
    @Override public int compareTo(Vec3i other) {
        int byY = Integer.compare(y, other.y);
        return byY != 0 ? byY : (Integer.compare(z, other.z) != 0 ? Integer.compare(z, other.z) : Integer.compare(x, other.x));
    }
    @Override public boolean equals(Object other) {
        return other instanceof Vec3i value && x == value.x && y == value.y && z == value.z;
    }
    @Override public int hashCode() { return Objects.hash(x, y, z); }
    @Override public String toString() { return "Vec3i{" + x + "," + y + "," + z + "}"; }
}
