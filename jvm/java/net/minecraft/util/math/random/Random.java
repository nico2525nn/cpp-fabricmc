package net.minecraft.util.math.random;

/**
 * 1.21.4 random ABI.  The native server controls gameplay RNG; this class
 * provides the Java-side type and the deterministic Java-compatible helpers
 * used by mod method descriptors.
 */
public class Random extends java.util.Random {
    public Random() { super(); }
    public Random(long seed) { super(seed); }

    public static Random create() { return new Random(); }
    public static Random create(long seed) { return new Random(seed); }
    public static Random createLocal() { return create(); }
    public static Random createThreadSafe() { return create(); }

    public int nextBetweenExclusive(int min, int max) {
        if (max <= min) return min;
        return min + nextInt(max - min);
    }

    public int nextBetween(int min, int max) {
        if (max <= min) return min;
        return min + nextInt(max - min + 1);
    }

    public float nextTriangular(float mode, float deviation) {
        return mode + deviation * (nextFloat() - nextFloat());
    }

    public double nextTriangular(double mode, double deviation) {
        return mode + deviation * (nextDouble() - nextDouble());
    }

    public void skip(int count) {
        for (int i = 0; i < Math.max(0, count); ++i) nextLong();
    }

    public Random split() { return new Random(nextLong()); }
}
