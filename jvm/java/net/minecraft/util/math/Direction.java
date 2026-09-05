package net.minecraft.util.math;

import net.minecraft.util.StringIdentifiable;

public enum Direction {
    DOWN(0, -1, 0, 0), UP(1, 1, 0, 0), NORTH(2, 0, 0, -1),
    SOUTH(3, 0, 0, 1), WEST(4, 0, -1, 0), EAST(5, 0, 1, 0);

    private final int id;
    private final int y;
    private final int x;
    private final int z;

    Direction(int id, int y, int x, int z) {
        this.id = id; this.y = y; this.x = x; this.z = z;
    }
    public int getId() { return id; }
    public int getOffsetX() { return x; }
    public int getOffsetY() { return y; }
    public int getOffsetZ() { return z; }
    public Axis getAxis() { return y != 0 ? Axis.Y : x != 0 ? Axis.X : Axis.Z; }
    public AxisDirection getDirection() { return (y + x + z) < 0 ? AxisDirection.NEGATIVE : AxisDirection.POSITIVE; }
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
