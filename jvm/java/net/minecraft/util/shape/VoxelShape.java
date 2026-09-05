package net.minecraft.util.shape;

import net.minecraft.util.math.Box;

/** Conservative collision-shape representation for the shadow world. */
public class VoxelShape {
    private final Box box;
    public VoxelShape(Box box) { this.box = box; }
    public boolean isEmpty() { return box == null; }
    public Box getBoundingBox() { return box; }
    public double getMin(int axis) { return box == null ? 0.0 : axis == 0 ? box.minX : axis == 1 ? box.minY : box.minZ; }
    public double getMax(int axis) { return box == null ? 0.0 : axis == 0 ? box.maxX : axis == 1 ? box.maxY : box.maxZ; }
    public VoxelShape offset(double x, double y, double z) { return box == null ? this : new VoxelShape(box.offset(x, y, z)); }
}
