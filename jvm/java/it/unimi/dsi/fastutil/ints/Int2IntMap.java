package it.unimi.dsi.fastutil.ints;

/** Minimal primitive-int map view required by registry remap callbacks. */
public interface Int2IntMap extends java.util.Map<Integer, Integer> {
    default int get(int key) { return getOrDefault(Integer.valueOf(key), Integer.valueOf(-1)); }
    default int put(int key, int value) { return put(Integer.valueOf(key), Integer.valueOf(value)); }
}
