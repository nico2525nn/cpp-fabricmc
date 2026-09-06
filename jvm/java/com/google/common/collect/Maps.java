package com.google.common.collect;

import java.util.HashMap;

/** Small ABI-compatible subset used by the 1.21.4 shadow constructor path. */
public final class Maps {
    private Maps() { }

    public static <K, V> HashMap<K, V> newHashMap() {
        return new HashMap<>();
    }
}
