package net.minecraft.util.math;

public final class Vec3d {
    public final double x, y, z;
    public Vec3d(double x, double y, double z) { this.x = x; this.y = y; this.z = z; }
    public Vec3d add(double dx, double dy, double dz) { return new Vec3d(x + dx, y + dy, z + dz); }
    public double distanceTo(Vec3d other) {
        double dx = x - other.x, dy = y - other.y, dz = z - other.z;
        return Math.sqrt(dx * dx + dy * dy + dz * dz);
    }
    @Override public String toString() { return "Vec3d{" + x + "," + y + "," + z + "}"; }
}
