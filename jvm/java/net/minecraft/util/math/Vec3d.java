package net.minecraft.util.math;

import java.util.Objects;

public final class Vec3d {
    public static final Vec3d ZERO = new Vec3d(0.0, 0.0, 0.0);
    public final double x, y, z;
    public Vec3d(double x, double y, double z) { this.x = x; this.y = y; this.z = z; }
    public double getX() { return x; }
    public double getY() { return y; }
    public double getZ() { return z; }
    public Vec3d add(Vec3d other) { return other == null ? this : add(other.x, other.y, other.z); }
    public Vec3d add(double dx, double dy, double dz) { return new Vec3d(x + dx, y + dy, z + dz); }
    public Vec3d subtract(Vec3d other) { return other == null ? this : new Vec3d(x - other.x, y - other.y, z - other.z); }
    public Vec3d multiply(double factor) { return new Vec3d(x * factor, y * factor, z * factor); }
    public Vec3d multiply(double x, double y, double z) { return new Vec3d(this.x * x, this.y * y, this.z * z); }
    public Vec3d negate() { return new Vec3d(-x, -y, -z); }
    public double lengthSquared() { return x * x + y * y + z * z; }
    public double length() { return Math.sqrt(lengthSquared()); }
    public double horizontalLengthSquared() { return x * x + z * z; }
    public double horizontalLength() { return Math.sqrt(horizontalLengthSquared()); }
    public double distanceTo(Vec3d other) { return Math.sqrt(squaredDistanceTo(other)); }
    public double squaredDistanceTo(Vec3d other) {
        if (other == null) return Double.POSITIVE_INFINITY;
        double dx = x - other.x, dy = y - other.y, dz = z - other.z;
        return dx * dx + dy * dy + dz * dz;
    }
    public Vec3d normalize() {
        double length = length();
        return length < 1.0E-4 ? ZERO : multiply(1.0 / length);
    }
    public Vec3d rotateX(float angle) {
        double sin = Math.sin(angle), cos = Math.cos(angle);
        return new Vec3d(x, y * cos + z * sin, z * cos - y * sin);
    }
    public Vec3d rotateY(float angle) {
        double sin = Math.sin(angle), cos = Math.cos(angle);
        return new Vec3d(x * cos + z * sin, y, z * cos - x * sin);
    }
    public Vec3d rotateZ(float angle) {
        double sin = Math.sin(angle), cos = Math.cos(angle);
        return new Vec3d(x * cos - y * sin, y * cos + x * sin, z);
    }
    public static Vec3d ofCenter(BlockPos pos) { return new Vec3d(pos.getX() + 0.5, pos.getY() + 0.5, pos.getZ() + 0.5); }
    @Override public boolean equals(Object other) {
        return other instanceof Vec3d value && Double.compare(x, value.x) == 0 && Double.compare(y, value.y) == 0 && Double.compare(z, value.z) == 0;
    }
    @Override public int hashCode() { return Objects.hash(x, y, z); }
    @Override public String toString() { return "Vec3d{" + x + "," + y + "," + z + "}"; }
}
