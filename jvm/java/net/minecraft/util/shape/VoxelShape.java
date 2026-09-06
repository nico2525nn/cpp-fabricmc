package net.minecraft.util.shape;

import net.minecraft.util.math.Box;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.Vec3d;

/** Conservative collision-shape representation for the shadow world. */
public class VoxelShape {
    private final Box box;
    /** Cached directional shapes exposed by Lithium's Accessor mixin. */
    private VoxelShape[] shapeCache;
    public final VoxelSet voxels;
    /** Vanilla/Lithium field alias for the backing voxel set. */
    public final VoxelSet shape;
    public VoxelShape(Box box) {
        this.box = box;
        this.voxels = box == null ? new VoxelSet(0, 0, 0) : filledUnitSet();
        this.shape = this.voxels;
        this.shapeCache = new VoxelShape[0];
    }
    public VoxelShape(VoxelSet voxels) {
        this.voxels = voxels == null ? new VoxelSet(0, 0, 0) : voxels;
        this.box = this.voxels.isEmpty() ? null : new Box(
            this.voxels.getMin(Direction.Axis.X), this.voxels.getMin(Direction.Axis.Y), this.voxels.getMin(Direction.Axis.Z),
            this.voxels.getMax(Direction.Axis.X), this.voxels.getMax(Direction.Axis.Y), this.voxels.getMax(Direction.Axis.Z));
        this.shape = this.voxels;
        this.shapeCache = new VoxelShape[0];
    }
    private static VoxelSet filledUnitSet() {
        VoxelSet result = new VoxelSet(1, 1, 1);
        result.set(0, 0, 0);
        return result;
    }
    public boolean isEmpty() { return box == null; }
    public Box getBoundingBox() { return box; }
    public double getMin(int axis) { return box == null ? 0.0 : axis == 0 ? box.minX : axis == 1 ? box.minY : box.minZ; }
    public double getMax(int axis) { return box == null ? 0.0 : axis == 0 ? box.maxX : axis == 1 ? box.maxY : box.maxZ; }
    public double getMin(Direction.Axis axis) { return getMin(axis == Direction.Axis.X ? 0 : axis == Direction.Axis.Y ? 1 : 2); }
    public double getMax(Direction.Axis axis) { return getMax(axis == Direction.Axis.X ? 0 : axis == Direction.Axis.Y ? 1 : 2); }
    public double getPointPosition(Direction.Axis axis, int index) {
        int size = axis == Direction.Axis.X ? voxels.getXSize() : axis == Direction.Axis.Y ? voxels.getYSize() : voxels.getZSize();
        if (size <= 0 || box == null) return 0.0;
        double min = getMin(axis), max = getMax(axis);
        return min + (max - min) * index / size;
    }
    public boolean isCube() { return box != null && box.minX == 0 && box.minY == 0 && box.minZ == 0
        && box.maxX == 1 && box.maxY == 1 && box.maxZ == 1; }
    public int getCoordIndex(Direction.Axis axis, double coordinate) {
        int size = axis == Direction.Axis.X ? voxels.getXSize()
            : axis == Direction.Axis.Y ? voxels.getYSize() : voxels.getZSize();
        if (size <= 0 || box == null) return 0;
        double min = getMin(axis), max = getMax(axis);
        if (max <= min) return 0;
        return Math.max(0, Math.min(size - 1,
            (int) Math.floor((coordinate - min) * size / (max - min))));
    }
    public VoxelShape offset(double x, double y, double z) { return box == null ? this : new VoxelShape(box.offset(x, y, z)); }
    public VoxelShape offset(Vec3d offset) { return offset(offset == null ? 0 : offset.x, offset == null ? 0 : offset.y, offset == null ? 0 : offset.z); }
    public double calculateMaxDistance(net.minecraft.util.math.AxisCycleDirection axisCycle,
                                       Box collisionBox, double maxDistance) {
        if (collisionBox == null || box == null || maxDistance == 0.0) return maxDistance;
        Direction.Axis axis = axisCycle == null ? Direction.Axis.X
            : axisCycle.cycle(Direction.Axis.X);
        double shapeMin = getMin(axis), shapeMax = getMax(axis);
        double boxMin = axis == Direction.Axis.X ? collisionBox.minX
            : axis == Direction.Axis.Y ? collisionBox.minY : collisionBox.minZ;
        double boxMax = axis == Direction.Axis.X ? collisionBox.maxX
            : axis == Direction.Axis.Y ? collisionBox.maxY : collisionBox.maxZ;
        if (maxDistance > 0.0 && boxMax <= shapeMin) return Math.min(maxDistance, shapeMin - boxMax);
        if (maxDistance < 0.0 && boxMin >= shapeMax) return Math.max(maxDistance, shapeMax - boxMin);
        return 0.0;
    }
}
