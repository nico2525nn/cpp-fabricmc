package net.minecraft.util.math;

import java.util.UUID;
import java.util.function.IntPredicate;
import java.util.stream.IntStream;
import net.minecraft.util.math.random.Random;

/** Deterministic scalar helpers exposed by the 1.21.4 Minecraft ABI. */
public final class MathHelper {
    public static final float PI = (float) Math.PI;
    public static final float HALF_PI = PI / 2.0f;
    public static final float TAU = PI * 2.0f;
    public static final float RADIANS_PER_DEGREE = PI / 180.0f;
    public static final float DEGREES_PER_RADIAN = 180.0f / PI;
    public static final float EPSILON = 1.0E-5f;
    public static final float SQUARE_ROOT_OF_TWO = (float) Math.sqrt(2.0);
    public static final long HALF_PI_RADIANS_SINE_TABLE_INDEX = 16384L;
    public static final Random RANDOM = new Random(0L);
    public static final float[] SINE_TABLE = new float[65536];
    public static final double[] ARCSINE_TABLE = new double[257];
    public static final double[] COSINE_OF_ARCSINE_TABLE = new double[257];
    public static final int[] MULTIPLY_DE_BRUIJN_BIT_POSITION = new int[32];
    public static final int ARCSINE_TABLE_LENGTH = 257;
    public static final double ROUNDER_256THS = 3.0 * 0x1000000;

    static {
        for (int i = 0; i < SINE_TABLE.length; ++i)
            SINE_TABLE[i] = (float) Math.sin(i * Math.PI * 2.0 / SINE_TABLE.length);
        for (int i = 0; i <= 256; ++i) {
            double value = i / 256.0;
            ARCSINE_TABLE[i] = Math.asin(value);
            COSINE_OF_ARCSINE_TABLE[i] = Math.cos(Math.asin(value));
        }
    }

    private MathHelper() {}

    public static float square(float n) { return n * n; }
    public static double square(double n) { return n * n; }
    public static int square(int n) { return n * n; }
    public static long square(long n) { return n * n; }

    public static int clamp(int value, int min, int max) { return Math.max(min, Math.min(max, value)); }
    public static float clamp(float value, float min, float max) { return Math.max(min, Math.min(max, value)); }
    public static double clamp(double value, double min, double max) { return Math.max(min, Math.min(max, value)); }
    public static long clamp(long value, long min, long max) { return Math.max(min, Math.min(max, value)); }

    public static double getLerpProgress(double value, double start, double end) {
        return end == start ? 0.0 : (value - start) / (end - start);
    }
    public static double lerp(double delta, double start, double end) { return start + delta * (end - start); }
    public static float lerp(float delta, float start, float end) { return start + delta * (end - start); }
    public static double lerp2(double dx, double dy, double x0y0, double x1y0,
                               double x0y1, double x1y1) {
        return lerp(dy, lerp(dx, x0y0, x1y0), lerp(dx, x0y1, x1y1));
    }
    public static double lerp3(double dx, double dy, double dz, double x0y0z0,
                               double x1y0z0, double x0y1z0, double x1y1z0,
                               double x0y0z1, double x1y0z1, double x0y1z1,
                               double x1y1z1) {
        return lerp(dz, lerp2(dx, dy, x0y0z0, x1y0z0, x0y1z0, x1y1z0),
                         lerp2(dx, dy, x0y0z1, x1y0z1, x0y1z1, x1y1z1));
    }
    public static float clampedLerp(float start, float end, float delta) { return lerp(clamp(delta, 0.0f, 1.0f), start, end); }
    public static double clampedLerp(double start, double end, double delta) { return lerp(clamp(delta, 0.0, 1.0), start, end); }
    public static float clampedLerp(float delta, float start, float end, boolean ignored) { return clampedLerp(start, end, delta); }
    public static double clampedLerp(double delta, double start, double end, boolean ignored) { return clampedLerp(start, end, delta); }
    public static double map(double value, double oldStart, double oldEnd, double newStart, double newEnd) {
        return newStart + (value - oldStart) * (newEnd - newStart) / (oldEnd - oldStart);
    }
    public static double clampedMap(double value, double oldStart, double oldEnd, double newStart, double newEnd) {
        return clampedLerp(newStart, newEnd, getLerpProgress(value, oldStart, oldEnd));
    }
    public static float map(float value, float oldStart, float oldEnd, float newStart, float newEnd) {
        return newStart + (value - oldStart) * (newEnd - newStart) / (oldEnd - oldStart);
    }
    public static float clampedMap(float value, float oldStart, float oldEnd, float newStart, float newEnd) {
        return clampedLerp(newStart, newEnd, (float) getLerpProgress(value, oldStart, oldEnd));
    }
    public static float catmullRom(float delta, float p0, float p1, float p2, float p3) {
        return 0.5f * (2.0f * p1 + (p2 - p0) * delta
            + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * delta * delta
            + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * delta * delta * delta);
    }

    public static int nextInt(Random random, int min, int max) {
        return max <= min ? min : min + random.nextInt(max - min + 1);
    }
    public static int nextBetween(Random random, int min, int max) { return nextInt(random, min, max); }
    public static float nextFloat(Random random, float min, float max) { return min + random.nextFloat() * (max - min); }
    public static double nextDouble(Random random, double min, double max) { return min + random.nextDouble() * (max - min); }
    public static float nextGaussian(Random random, float mean, float deviation) { return (float) (mean + random.nextGaussian() * deviation); }

    public static boolean isMultipleOf(int a, int b) { return b != 0 && a % b == 0; }
    public static int binarySearch(int min, int max, IntPredicate predicate) {
        int low = min, high = max;
        while (low < high) {
            int mid = low + (high - low) / 2;
            if (predicate.test(mid)) high = mid; else low = mid + 1;
        }
        return low;
    }
    public static int floor(float value) { return (int) Math.floor(value); }
    public static int floor(double value) { return (int) Math.floor(value); }
    public static int ceil(float value) { return (int) Math.ceil(value); }
    public static int ceil(double value) { return (int) Math.ceil(value); }
    public static long lfloor(double value) { return (long) Math.floor(value); }
    public static int floorDiv(int dividend, int divisor) { return Math.floorDiv(dividend, divisor); }
    public static int floorMod(int dividend, int divisor) { return Math.floorMod(dividend, divisor); }
    public static double floorMod(double dividend, double divisor) { return dividend - Math.floor(dividend / divisor) * divisor; }
    public static int roundDownToMultiple(int a, int b) { return Math.floorDiv(a, b) * b; }
    public static int roundUpToMultiple(int value, int divisor) { return Math.floorDiv(value + divisor - 1, divisor) * divisor; }
    public static int sign(double value) { return Double.compare(value, 0.0); }
    public static float fractionalPart(float value) { return value - floor(value); }
    public static double fractionalPart(double value) { return value - floor(value); }
    public static int abs(int value) { return Math.abs(value); }
    public static float abs(float value) { return Math.abs(value); }
    public static double abs(double value) { return Math.abs(value); }
    public static double absMax(double a, double b) { return Math.max(Math.abs(a), Math.abs(b)); }
    public static float magnitude(float a, float b, float c) { return (float) Math.sqrt(a * a + b * b + c * c); }
    public static double magnitude(double a, double b, double c) { return Math.sqrt(a * a + b * b + c * c); }
    public static double squaredMagnitude(double a, double b, double c) { return a * a + b * b + c * c; }
    public static float hypot(float a, float b) { return (float) Math.hypot(a, b); }
    public static double hypot(double a, double b) { return Math.hypot(a, b); }
    public static double squaredHypot(double a, double b) { return a * a + b * b; }

    public static float sin(float value) { return (float) Math.sin(value); }
    public static float cos(float value) { return (float) Math.cos(value); }
    public static double atan2(double y, double x) { return Math.atan2(y, x); }
    public static float sqrt(float value) { return (float) Math.sqrt(value); }
    public static float inverseSqrt(float value) { return 1.0f / sqrt(value); }
    public static double inverseSqrt(double value) { return 1.0 / Math.sqrt(value); }
    public static double fastInverseSqrt(double value) { return inverseSqrt(value); }
    public static float fastInverseCbrt(float value) { return (float) (1.0 / Math.cbrt(value)); }
    public static double perlinFade(double value) { return value * value * value * (value * (value * 6.0 - 15.0) + 10.0); }
    public static double perlinFadeDerivative(double value) { return 30.0 * value * value * (value * (value - 2.0) + 1.0); }

    public static float wrap(float value, float maxDeviation) { return wrapDegrees(value) % maxDeviation; }
    public static float wrapDegrees(float degrees) { return (float) (degrees - Math.floor((degrees + 180.0f) / 360.0f) * 360.0f); }
    public static double wrapDegrees(double degrees) { return degrees - Math.floor((degrees + 180.0) / 360.0) * 360.0; }
    public static int wrapDegrees(int degrees) { return (int) wrapDegrees((double) degrees); }
    public static float wrapDegrees(long degrees) { return wrapDegrees((float) degrees); }
    public static float subtractAngles(float start, float end) { return wrapDegrees(end - start); }
    public static float angleBetween(float first, float second) { return wrapDegrees(second - first); }
    public static float lerpAngleDegrees(float delta, float start, float end) { return start + delta * wrapDegrees(end - start); }
    public static double lerpAngleDegrees(double delta, double start, double end) { return start + delta * wrapDegrees(end - start); }
    public static float lerpAngleRadians(float delta, float start, float end) {
        float difference = (float) ((end - start + Math.PI) % (Math.PI * 2.0) - Math.PI);
        return start + delta * difference;
    }
    public static float stepTowards(float from, float to, float step) {
        return Math.abs(to - from) <= step ? to : from + Math.copySign(step, to - from);
    }
    public static float stepUnwrappedAngleTowards(float from, float to, float step) {
        return stepTowards(from, from + wrapDegrees(to - from), step);
    }
    public static float unpackDegrees(byte packedDegrees) { return packedDegrees * 360.0f / 256.0f; }
    public static byte packDegrees(float degrees) { return (byte) Math.round(degrees * 256.0f / 360.0f); }
    public static boolean approximatelyEquals(double a, double b) { return Math.abs(a - b) < 1.0E-5; }
    public static boolean approximatelyEquals(float a, float b) { return Math.abs(a - b) < 1.0E-5f; }

    public static int smallestEncompassingPowerOfTwo(int value) {
        if (value <= 1) return 1;
        return Integer.highestOneBit(value - 1) << 1;
    }
    public static boolean isPowerOfTwo(int value) { return value > 0 && (value & (value - 1)) == 0; }
    public static int floorLog2(int value) { return 31 - Integer.numberOfLeadingZeros(value); }
    public static int ceilLog2(int value) { return value <= 1 ? 0 : 32 - Integer.numberOfLeadingZeros(value - 1); }
    public static int idealHash(int value) { value ^= value >>> 16; value *= -2048144789; value ^= value >>> 13; value *= -1028477387; return value ^ value >>> 16; }
    public static IntStream stream(int from, int to, int step) { return IntStream.iterate(from, i -> i < to, i -> i + step); }
    public static IntStream stream(int from, int to, int step, int ignored) { return stream(from, to, step); }
    public static UUID randomUuid() { return UUID.randomUUID(); }
    public static UUID randomUuid(Random random) { return new UUID(random.nextLong(), random.nextLong()); }
    public static int parseInt(String value, int fallback) { try { return Integer.parseInt(value); } catch (RuntimeException ignored) { return fallback; } }
    public static int hsvToArgb(float hue, float saturation, float value, int alpha) {
        int rgb = java.awt.Color.HSBtoRGB(hue, saturation, value) & 0xFFFFFF;
        return (alpha << 24) | rgb;
    }
    public static int hsvToRgb(float hue, float saturation, float value) {
        return java.awt.Color.HSBtoRGB(hue, saturation, value) & 0xFFFFFF;
    }
}
