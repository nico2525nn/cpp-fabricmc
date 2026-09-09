package it.unimi.dsi.fastutil.objects;

/** Small boxed fallback for the fastutil object-to-int map ABI. */
public interface Object2IntMap<K> extends java.util.Map<K, Integer> {
    default int getInt(Object key) {
        Integer value = get(key);
        return value == null ? 0 : value;
    }

    default int put(K key, int value) {
        Integer previous = put(key, Integer.valueOf(value));
        return previous == null ? 0 : previous;
    }

    default int removeInt(Object key) {
        Integer previous = remove(key);
        return previous == null ? 0 : previous;
    }
}
