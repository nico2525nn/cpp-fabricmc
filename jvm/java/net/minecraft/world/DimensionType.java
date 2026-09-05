package net.minecraft.world;

public final class DimensionType {
    public static final DimensionType OVERWORLD = new DimensionType(0, 256, true);
    public static final DimensionType THE_NETHER = new DimensionType(-64, 384, false);
    public static final DimensionType THE_END = new DimensionType(0, 256, false);
    private final int minY, height;
    private final boolean skylight;
    public DimensionType(int minY, int height, boolean skylight) { this.minY = minY; this.height = height; this.skylight = skylight; }
    public int minY() { return minY; }
    public int height() { return height; }
    public int logicalHeight() { return height; }
    public boolean hasSkyLight() { return skylight; }
}
