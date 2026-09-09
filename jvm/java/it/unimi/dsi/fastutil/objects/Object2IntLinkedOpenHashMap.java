package it.unimi.dsi.fastutil.objects;

import java.util.LinkedHashMap;

/** Deterministic boxed implementation of the subset used by FuelRegistry. */
public class Object2IntLinkedOpenHashMap<K> extends LinkedHashMap<K, Integer>
        implements Object2IntSortedMap<K> {
    public Object2IntLinkedOpenHashMap() { }
    public Object2IntLinkedOpenHashMap(int expectedSize) { super(expectedSize); }
    public Object2IntLinkedOpenHashMap(java.util.Map<? extends K, ? extends Integer> values) {
        if (values != null) putAll(values);
    }
}
