package net.minecraft.world.border;

import net.minecraft.entity.Entity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.ChunkPos;
import net.minecraft.util.math.Vec3d;
import net.minecraft.util.shape.VoxelShape;
import net.minecraft.util.shape.VoxelShapes;

/** Canonical 1.21.4 world-border view over the shadow border state. */
public class WorldBorder extends net.minecraft.world.WorldBorder {
    public static final double MAX_CENTER_COORDINATES = 2.9999984E7;

    public WorldBorder() { super(); }

    public boolean contains(BlockPos pos) {
        return pos != null && contains(pos.getX() + 0.5, pos.getZ() + 0.5);
    }

    public boolean contains(Vec3d pos) {
        return pos != null && contains(pos.x, pos.z);
    }

    public boolean contains(ChunkPos pos) {
        return pos != null && contains(
            pos.getStartX(), pos.getStartZ(), pos.getEndX() + 1.0, pos.getEndZ() + 1.0);
    }

    public boolean contains(Box box) {
        return box != null && contains(box.minX, box.minZ, box.maxX, box.maxZ);
    }

    public boolean contains(double minX, double minZ, double maxX, double maxZ) {
        return contains(minX, minZ) && contains(maxX, maxZ);
    }

    public boolean contains(double x, double z) { return super.contains(x, z); }

    public BlockPos clampFloored(BlockPos pos) {
        if (pos == null) return null;
        return clampFloored(pos.getX(), pos.getY(), pos.getZ());
    }

    public BlockPos clampFloored(double x, double y, double z) {
        double half = getSize() / 2.0;
        int clampedX = (int) Math.floor(Math.max(getCenterX() - half,
            Math.min(getCenterX() + half, x)));
        int clampedZ = (int) Math.floor(Math.max(getCenterZ() - half,
            Math.min(getCenterZ() + half, z)));
        return new BlockPos(clampedX, (int) Math.floor(y), clampedZ);
    }

    public Vec3d clamp(Vec3d pos) { return pos == null ? null : clamp(pos.x, pos.y, pos.z); }

    public Vec3d clamp(double x, double y, double z) {
        double half = getSize() / 2.0;
        return new Vec3d(
            Math.max(getCenterX() - half, Math.min(getCenterX() + half, x)),
            y,
            Math.max(getCenterZ() - half, Math.min(getCenterZ() + half, z)));
    }

    public double getDistanceInsideBorder(double x, double z) {
        double half = getSize() / 2.0;
        return Math.min(half - Math.abs(x - getCenterX()), half - Math.abs(z - getCenterZ()));
    }

    public double getDistanceInsideBorder(Entity entity) {
        return entity == null ? 0.0 : getDistanceInsideBorder(entity.getX(), entity.getZ());
    }

    public double getSafeZone() { return getSize() / 2.0; }
    public void setSafeZone(double value) { }
    public double getDamagePerBlock() { return 0.0; }
    public void setDamagePerBlock(double value) { }
    public int getWarningBlocks() { return 5; }
    public void setWarningBlocks(int value) { }
    public int getWarningTime() { return 15; }
    public void setWarningTime(int value) { }
    public int getMaxRadius() { return (int) MAX_CENTER_COORDINATES; }
    public double getBoundWest() { return getCenterX() - getSize() / 2.0; }
    public double getBoundEast() { return getCenterX() + getSize() / 2.0; }
    public double getBoundNorth() { return getCenterZ() - getSize() / 2.0; }
    public double getBoundSouth() { return getCenterZ() + getSize() / 2.0; }
    public VoxelShape asVoxelShape() {
        return VoxelShapes.cuboid(getBoundWest(), -3.0E7, getBoundNorth(),
            getBoundEast(), 3.0E7, getBoundSouth());
    }

    public WorldBorderStage getStage() { return WorldBorderStage.STATIONARY; }
}
