package net.fabricmc.fabric.api.transfer.v1.fluid;

/** Canonical Fabric fluid units: one bucket is 81,000 droplets. */
public final class FluidConstants {
    private FluidConstants() { }
    public static final long BUCKET = 81000;
    public static final long BOTTLE = 27000;
    public static final long BOWL = 27000;
    public static final long BLOCK = 81000;
    public static final long INGOT = 9000;
    public static final long NUGGET = 1000;
    public static final long DROPLET = 1;
    public static final int WATER_TEMPERATURE = 300;
    public static final int LAVA_TEMPERATURE = 1300;
    public static final int WATER_VISCOSITY = 1000;
    public static final int LAVA_VISCOSITY = 6000;
    public static final int LAVA_VISCOSITY_NETHER = 2000;
    public static final int VISCOSITY_RATIO = 200;
    public static long fromBucketFraction(long numerator, long denominator) {
        if (denominator == 0 || numerator * BUCKET % denominator != 0)
            throw new IllegalArgumentException("Not a valid number of droplets");
        return numerator * BUCKET / denominator;
    }
}
