package com.google.common.collect;

import java.util.HashMap;

/** Small ABI-compatible subset used by the 1.21.4 shadow constructor path. */
public final class Maps {
    private Maps() { }

    public static <K, V> HashMap<K, V> newHashMap() {
        return new HashMap<>();
    }

    /** Guava's package-private sizing helper used by {@code Sets}. */
    static int capacity(int expectedSize) {
        if (expectedSize < 3) return expectedSize + 1;
        if (expectedSize < 1_073_741_824) return (int) (expectedSize / 0.75F + 1.0F);
        return Integer.MAX_VALUE;
    }
}
