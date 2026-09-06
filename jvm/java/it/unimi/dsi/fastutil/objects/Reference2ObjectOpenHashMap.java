package it.unimi.dsi.fastutil.objects;

/** Deterministic insertion-ordered implementation for the embedded shadow. */
public class Reference2ObjectOpenHashMap<K, V> extends java.util.LinkedHashMap<K, V>
        implements Reference2ObjectMap<K, V> {
    public Reference2ObjectOpenHashMap() { super(); }
    public Reference2ObjectOpenHashMap(int expectedSize) { super(expectedSize); }
}
