package net.minecraft.world.dimension;

/** Canonical 1.21.4 dimension descriptor view. */
public class DimensionType extends net.minecraft.world.DimensionType {
    public static final DimensionType OVERWORLD = new DimensionType(0, 384, true);
    public static final DimensionType THE_NETHER = new DimensionType(0, 256, false);
    public static final DimensionType THE_END = new DimensionType(0, 256, false);

    public DimensionType(int minY, int height, boolean skylight) {
        super(minY, height, skylight);
    }

    public int minY() { return super.minY(); }
    public int height() { return super.height(); }
    public int logicalHeight() { return super.logicalHeight(); }
    public boolean hasSkyLight() { return super.hasSkyLight(); }
}
