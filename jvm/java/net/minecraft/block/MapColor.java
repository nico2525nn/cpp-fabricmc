package net.minecraft.block;

public final class MapColor {
    public static final MapColor CLEAR = new MapColor(0);
    public static final MapColor STONE_GRAY = new MapColor(11);
    public static final MapColor GRASS = new MapColor(7);
    public static final MapColor WOOD = new MapColor(13);
    private final int id;
    public MapColor(int id) { this.id = id; }
    public int getId() { return id; }
}
