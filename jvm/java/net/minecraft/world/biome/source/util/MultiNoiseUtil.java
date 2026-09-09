package net.minecraft.world.biome.source.util;

/** Minimal namespace shell for the named 1.21.4 multi-noise ABI. */
public final class MultiNoiseUtil {
    private MultiNoiseUtil() { }

    public static final class MultiNoiseSampler {
        public MultiNoiseSampler() { }
        public NoiseValuePoint sample(int x, int y, int z) {
            return createNoiseValuePoint(0, 0, 0, 0, 0, 0);
        }
    }

    public static NoiseValuePoint createNoiseValuePoint(float temperature, float humidity,
            float continentalness, float erosion, float depth, float weirdness) {
        return new NoiseValuePoint(toLong(temperature), toLong(humidity), toLong(continentalness),
            toLong(erosion), toLong(depth), toLong(weirdness));
    }

    public static NoiseHypercube createNoiseHypercube(float temperature, float humidity,
            float continentalness, float erosion, float depth, float weirdness, float offset) {
        return new NoiseHypercube(ParameterRange.of(temperature), ParameterRange.of(humidity),
            ParameterRange.of(continentalness), ParameterRange.of(erosion),
            ParameterRange.of(depth), ParameterRange.of(weirdness), toLong(offset));
    }

    public static NoiseHypercube createNoiseHypercube(ParameterRange temperature,
            ParameterRange humidity, ParameterRange continentalness, ParameterRange erosion,
            ParameterRange depth, ParameterRange weirdness, float offset) {
        return new NoiseHypercube(temperature, humidity, continentalness, erosion, depth,
            weirdness, toLong(offset));
    }

    public static long toLong(float value) { return Math.round(value * 10000.0f); }
    public static float toFloat(long value) { return value / 10000.0f; }

    public record NoiseValuePoint(long temperatureNoise, long humidityNoise,
            long continentalnessNoise, long erosionNoise, long depth, long weirdnessNoise) {
        public long[] getNoiseValueList() {
            return new long[] { temperatureNoise, humidityNoise, continentalnessNoise,
                erosionNoise, depth, weirdnessNoise };
        }
    }

    public record NoiseHypercube(ParameterRange temperature, ParameterRange humidity,
            ParameterRange continentalness, ParameterRange erosion, ParameterRange depth,
            ParameterRange weirdness, long offset) { }

    public record ParameterRange(long min, long max) {
        public static ParameterRange of(float point) {
            long value = toLong(point);
            return new ParameterRange(value, value);
        }
        public static ParameterRange of(float min, float max) {
            return new ParameterRange(toLong(min), toLong(max));
        }
        public static ParameterRange combine(ParameterRange min, ParameterRange max) {
            return new ParameterRange(min == null ? 0 : min.min, max == null ? 0 : max.max);
        }
        public long getDistance(ParameterRange other) {
            if (other == null) return 0;
            if (max < other.min) return other.min - max;
            if (other.max < min) return min - other.max;
            return 0;
        }
        public long getDistance(long value) {
            if (value < min) return min - value;
            if (value > max) return value - max;
            return 0;
        }
    }
}
