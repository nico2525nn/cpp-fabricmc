package it.unimi.dsi.fastutil.objects;

/** Minimal identity-map integer value contract used by SimpleRegistry. */
public interface Reference2IntMap<K> extends java.util.Map<K, Integer> {
    default int getInt(Object key) {
        Integer value = get(key);
        return value == null ? 0 : value;
    }

    default int put(K key, int value) {
        Integer previous = put(key, Integer.valueOf(value));
        return previous == null ? 0 : previous.intValue();
    }
}
