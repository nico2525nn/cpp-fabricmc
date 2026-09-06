package it.unimi.dsi.fastutil.ints;

import java.util.HashMap;
import java.util.Map;

/** Minimal fastutil-compatible int-to-object hash map. */
public class Int2ObjectOpenHashMap<V> implements Int2ObjectMap<V> {
    private final Map<Integer, V> values = new HashMap<>();
    public Int2ObjectOpenHashMap() { }
    public Int2ObjectOpenHashMap(int expectedSize) { }
    @Override public V get(int key) { return values.get(key); }
    @Override public V put(int key, V value) { return values.put(key, value); }
    @Override public V remove(int key) { return values.remove(key); }
    @Override public boolean isEmpty() { return values.isEmpty(); }
}
