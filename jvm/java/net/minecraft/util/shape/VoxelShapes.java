package net.minecraft.util.shape;

import net.minecraft.util.math.Box;

public final class VoxelShapes {
    private VoxelShapes() {}
    public static final VoxelShape EMPTY = new VoxelShape(null);
    public static final VoxelShape FULL_CUBE = new VoxelShape(new Box(0, 0, 0, 1, 1, 1));
    public static VoxelShape empty() { return EMPTY; }
    public static VoxelShape fullCube() { return FULL_CUBE; }
    public static VoxelShape cuboid(double minX, double minY, double minZ, double maxX, double maxY, double maxZ) {
        return new VoxelShape(new Box(minX / 16.0, minY / 16.0, minZ / 16.0,
                                      maxX / 16.0, maxY / 16.0, maxZ / 16.0));
    }
}
