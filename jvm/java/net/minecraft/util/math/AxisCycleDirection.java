package net.minecraft.util.math;

/** Axis permutation used by voxel and collision iteration code. */
public enum AxisCycleDirection {
    NONE(0, 1, 2),
    FORWARD(1, 2, 0),
    BACKWARD(2, 0, 1);

    private final int first;
    private final int second;
    private final int third;

    AxisCycleDirection(int first, int second, int third) {
        this.first = first; this.second = second; this.third = third;
    }

    public Direction.Axis cycle(Direction.Axis axis) {
        if (axis == null || this == NONE) return axis;
        Direction.Axis[] axes = Direction.Axis.values();
        return axes[axes.length == 0 ? 0 : axis.ordinal() == first ? second : axis.ordinal() == second ? third : first];
    }

    public int choose(int x, int y, int z, Direction.Axis axis) {
        return axis == Direction.Axis.X ? x : axis == Direction.Axis.Y ? y : z;
    }

    public int firstAxis() { return first; }
    public int secondAxis() { return second; }
    public static AxisCycleDirection between(Direction.Axis from, Direction.Axis to) {
        if (from == to) return NONE;
        if (from == Direction.Axis.X && to == Direction.Axis.Y
            || from == Direction.Axis.Y && to == Direction.Axis.Z
            || from == Direction.Axis.Z && to == Direction.Axis.X) return FORWARD;
        return BACKWARD;
    }
    public AxisCycleDirection opposite() {
        return this == FORWARD ? BACKWARD : this == BACKWARD ? FORWARD : NONE;
    }
}
