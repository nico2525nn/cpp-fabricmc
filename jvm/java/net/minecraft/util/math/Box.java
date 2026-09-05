package net.minecraft.util.math;

import java.util.Objects;

/** Immutable axis-aligned bounding box used by entity and world queries. */
public final class Box {
    public final double minX, minY, minZ, maxX, maxY, maxZ;

    public Box(double minX, double minY, double minZ, double maxX, double maxY, double maxZ) {
        this.minX = Math.min(minX, maxX); this.minY = Math.min(minY, maxY); this.minZ = Math.min(minZ, maxZ);
        this.maxX = Math.max(minX, maxX); this.maxY = Math.max(minY, maxY); this.maxZ = Math.max(minZ, maxZ);
    }
    public Box(BlockPos pos) { this(pos.getX(), pos.getY(), pos.getZ(), pos.getX() + 1, pos.getY() + 1, pos.getZ() + 1); }
    public Box(Vec3d min, Vec3d max) { this(min.x, min.y, min.z, max.x, max.y, max.z); }
    public double getLengthX() { return maxX - minX; }
    public double getLengthY() { return maxY - minY; }
    public double getLengthZ() { return maxZ - minZ; }
    public Vec3d getCenter() { return new Vec3d((minX + maxX) / 2, (minY + maxY) / 2, (minZ + maxZ) / 2); }
    public Box offset(double x, double y, double z) { return new Box(minX + x, minY + y, minZ + z, maxX + x, maxY + y, maxZ + z); }
    public Box expand(double x, double y, double z) {
        return new Box(x < 0 ? minX + x : minX, y < 0 ? minY + y : minY, z < 0 ? minZ + z : minZ,
                       x > 0 ? maxX + x : maxX, y > 0 ? maxY + y : maxY, z > 0 ? maxZ + z : maxZ);
    }
    public Box stretch(double x, double y, double z) {
        return expand(x, y, z).expand(-x, -y, -z);
    }
    public boolean intersects(Box other) {
        return other != null && maxX > other.minX && minX < other.maxX &&
               maxY > other.minY && minY < other.maxY && maxZ > other.minZ && minZ < other.maxZ;
    }
    public boolean contains(Vec3d point) {
        return point != null && point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY &&
               point.z >= minZ && point.z <= maxZ;
    }
    public Box union(Box other) {
        if (other == null) return this;
        return new Box(Math.min(minX, other.minX), Math.min(minY, other.minY), Math.min(minZ, other.minZ),
                       Math.max(maxX, other.maxX), Math.max(maxY, other.maxY), Math.max(maxZ, other.maxZ));
    }
    @Override public boolean equals(Object other) {
        return other instanceof Box value && Double.compare(minX, value.minX) == 0 && Double.compare(minY, value.minY) == 0 &&
               Double.compare(minZ, value.minZ) == 0 && Double.compare(maxX, value.maxX) == 0 &&
               Double.compare(maxY, value.maxY) == 0 && Double.compare(maxZ, value.maxZ) == 0;
    }
    @Override public int hashCode() { return Objects.hash(minX, minY, minZ, maxX, maxY, maxZ); }
    @Override public String toString() { return "Box{" + minX + "," + minY + "," + minZ + " -> " + maxX + "," + maxY + "," + maxZ + "}"; }
}
