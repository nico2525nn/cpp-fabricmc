package net.minecraft.util.shape;

import net.minecraft.util.math.Direction;
import net.minecraft.util.math.AxisCycleDirection;

/** Discrete voxel occupancy grid used by the 1.21.4 shape hierarchy. */
public class VoxelSet {
    protected static final Direction.Axis[] AXES = Direction.Axis.values();
    protected final int sizeX;
    protected final int sizeY;
    protected final int sizeZ;
    /** Mojang field aliases used by Lithium's Accessor mixins. */
    protected final int xSize;
    protected final int ySize;
    protected final int zSize;
    private final boolean[] cells;

    public VoxelSet(int sizeX, int sizeY, int sizeZ) {
        this.sizeX = Math.max(0, sizeX);
        this.sizeY = Math.max(0, sizeY);
        this.sizeZ = Math.max(0, sizeZ);
        this.xSize = this.sizeX;
        this.ySize = this.sizeY;
        this.zSize = this.sizeZ;
        long count = (long) this.sizeX * this.sizeY * this.sizeZ;
        this.cells = count > Integer.MAX_VALUE ? new boolean[0] : new boolean[(int) count];
    }

    public int getXSize() { return sizeX; }
    public int getYSize() { return sizeY; }
    public int getZSize() { return sizeZ; }
    public int getSize(Direction.Axis axis) { return size(axis); }

    public boolean contains(int x, int y, int z) {
        return inBoundsAndContains(x, y, z);
    }

    public boolean inBoundsAndContains(int x, int y, int z) {
        return x >= 0 && x < sizeX && y >= 0 && y < sizeY && z >= 0 && z < sizeZ
            && cells[index(x, y, z)];
    }

    public boolean inBoundsAndContains(AxisCycleDirection cycle, int x, int y, int z) {
        if (cycle != null) {
            int[] coordinates = { x, y, z };
            int first = coordinates[cycle.firstAxis()];
            coordinates[cycle.firstAxis()] = coordinates[cycle.secondAxis()];
            coordinates[cycle.secondAxis()] = first;
            x = coordinates[0]; y = coordinates[1]; z = coordinates[2];
        }
        return inBoundsAndContains(x, y, z);
    }

    public void set(int x, int y, int z) {
        if (x >= 0 && x < sizeX && y >= 0 && y < sizeY && z >= 0 && z < sizeZ)
            cells[index(x, y, z)] = true;
    }

    public boolean isEmpty() {
        for (boolean cell : cells) if (cell) return false;
        return true;
    }

    public int getMin(Direction.Axis axis) { return 0; }
    public int getMax(Direction.Axis axis) { return size(axis); }
    public int getStartingAxisCoord(Direction.Axis axis, int from, int to) { return from; }
    public int getEndingAxisCoord(Direction.Axis axis, int from, int to) { return to; }

    public void forEachDirection(PositionConsumer consumer) { }
    public void forEachDirection(PositionBiConsumer consumer) { }
    public void forEachEdge(PositionConsumer consumer, boolean coalesce) { }
    public void forEachEdge(PositionBiConsumer consumer, boolean coalesce) { }
    public void forEachBox(PositionBiConsumer consumer, boolean coalesce) {
        if (!isEmpty() && consumer != null) consumer.consume(0, 0, 0, sizeX, sizeY, sizeZ);
    }

    private int size(Direction.Axis axis) {
        return axis == Direction.Axis.X ? sizeX : axis == Direction.Axis.Y ? sizeY : sizeZ;
    }

    private int index(int x, int y, int z) { return (y * sizeZ + z) * sizeX + x; }

    public interface PositionConsumer {
        void consume(Direction direction, int x, int y, int z);
    }

    public interface PositionBiConsumer {
        void consume(int x1, int y1, int z1, int x2, int y2, int z2);
    }
}
