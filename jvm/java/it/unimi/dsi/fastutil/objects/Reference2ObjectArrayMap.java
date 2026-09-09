package it.unimi.dsi.fastutil.objects;

/**
 * Small insertion-ordered substitute for fastutil's reference array map.
 * Minecraft exposes this concrete type in the State constructor descriptor.
 */
public class Reference2ObjectArrayMap<K, V> extends java.util.LinkedHashMap<K, V>
        implements Reference2ObjectMap<K, V> {
    public Reference2ObjectArrayMap() { }
    public Reference2ObjectArrayMap(int expectedSize) { super(expectedSize); }
    public Reference2ObjectArrayMap(java.util.Map<? extends K, ? extends V> values) {
        if (values != null) putAll(values);
    }
}
