package it.unimi.dsi.fastutil.objects;

import java.util.HashMap;

/** Hash-map implementation for the embedded fastutil integer map surface. */
public class Reference2IntOpenHashMap<K> extends HashMap<K, Integer>
        implements Reference2IntMap<K> {
    public Reference2IntOpenHashMap() { super(); }
    public Reference2IntOpenHashMap(int expectedSize) { super(expectedSize); }
}
