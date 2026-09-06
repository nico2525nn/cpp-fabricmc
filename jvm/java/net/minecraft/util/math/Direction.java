package net.minecraft.util.math;

import net.minecraft.util.StringIdentifiable;

public enum Direction {
    DOWN(0, 1, -1, "down", AxisDirection.NEGATIVE, Axis.Y, new Vec3i(0, -1, 0)),
    UP(1, 0, -1, "up", AxisDirection.POSITIVE, Axis.Y, new Vec3i(0, 1, 0)),
    NORTH(2, 3, 2, "north", AxisDirection.NEGATIVE, Axis.Z, new Vec3i(0, 0, -1)),
    SOUTH(3, 2, 3, "south", AxisDirection.POSITIVE, Axis.Z, new Vec3i(0, 0, 1)),
    WEST(4, 5, 1, "west", AxisDirection.NEGATIVE, Axis.X, new Vec3i(-1, 0, 0)),
    EAST(5, 4, 0, "east", AxisDirection.POSITIVE, Axis.X, new Vec3i(1, 0, 0));

    private final int id;
    private final int y;
    private final int x;
    private final int z;
    private final String directionName;
    private final AxisDirection axisDirection;
    private final Axis axis;
    private final Vec3i vector;

    Direction(int id, int oppositeId, int horizontalId, String directionName,
              AxisDirection axisDirection, Axis axis, Vec3i vector) {
        this.id = id;
        this.y = vector.getY(); this.x = vector.getX(); this.z = vector.getZ();
        this.directionName = directionName;
        this.axisDirection = axisDirection;
        this.axis = axis;
        this.vector = vector;
    }
    public int getId() { return id; }
    public int getOffsetX() { return x; }
    public int getOffsetY() { return y; }
    public int getOffsetZ() { return z; }
    public String getName() { return directionName; }
    public Axis getAxis() { return axis; }
    public AxisDirection getDirection() { return axisDirection; }
    public Vec3i getVector() { return vector; }
    public Direction getOpposite() {
        return switch (this) { case DOWN -> UP; case UP -> DOWN; case NORTH -> SOUTH; case SOUTH -> NORTH; case WEST -> EAST; case EAST -> WEST; };
    }
    public Direction rotateYClockwise() {
        return switch (this) { case NORTH -> EAST; case EAST -> SOUTH; case SOUTH -> WEST; case WEST -> NORTH; default -> this; };
    }
    public Direction rotateYCounterclockwise() {
        return switch (this) { case NORTH -> WEST; case WEST -> SOUTH; case SOUTH -> EAST; case EAST -> NORTH; default -> this; };
    }
    public static Direction fromVector(int x, int y, int z) {
        Direction best = DOWN; int score = Integer.MIN_VALUE;
        for (Direction direction : values()) {
            int value = direction.x * x + direction.y * y + direction.z * z;
            if (value > score) { score = value; best = direction; }
        }
        return best;
    }
    public static Direction byId(int id) {
        for (Direction direction : values()) if (direction.id == id) return direction;
        return DOWN;
    }
    public static Direction random(net.minecraft.util.math.random.Random random) {
        Direction[] directions = values();
        return directions[(random == null ? new java.util.Random() : random).nextInt(directions.length)];
    }
    public static Direction[] orderedByNearest(net.minecraft.entity.Entity entity) {
        Direction[] directions = values().clone();
        if (entity == null) return directions;
        Vec3d view = entity.getRotationVec(1.0f);
        java.util.Arrays.sort(directions, (left, right) -> {
            double leftDot = view.x * left.x + view.y * left.y + view.z * left.z;
            double rightDot = view.x * right.x + view.y * right.y + view.z * right.z;
            int score = Double.compare(rightDot, leftDot);
            return score != 0 ? score : Integer.compare(left.id, right.id);
        });
        return directions;
    }
    /** Yarn name for the Mojang-mapped orderedByNearest helper. */
    public static Direction[] getEntityFacingOrder(net.minecraft.entity.Entity entity) {
        return orderedByNearest(entity);
    }

    public enum Axis implements StringIdentifiable {
        X, Y, Z;
        public boolean isVertical() { return this == Y; }
        public boolean isHorizontal() { return this != Y; }
        public Direction getPositiveDirection() { return this == X ? EAST : this == Y ? UP : SOUTH; }
        public Direction getNegativeDirection() { return this == X ? WEST : this == Y ? DOWN : NORTH; }
        @Override public String asString() { return name().toLowerCase(java.util.Locale.ROOT); }
    }
    public enum AxisDirection { POSITIVE, NEGATIVE }
}
