package net.minecraft.util.math;

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
    public static Direction byId(int id) {
        for (Direction direction : values()) if (direction.id == id) return direction;
        return DOWN;
    }
}
