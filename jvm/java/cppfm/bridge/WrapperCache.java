package cppfm.bridge;

import java.util.HashMap;
import java.util.Map;
import java.util.function.LongFunction;

/** Strong identity cache for live native-backed Java wrappers. */
public final class WrapperCache {
    private static final Map<Class<?>, Map<Long, Object>> OBJECTS = new HashMap<>();

    private WrapperCache() {}

    @SuppressWarnings("unchecked")
    public static synchronized <T> T get(Class<T> type, long handle,
                                          LongFunction<T> factory) {
        if (handle == 0L) return null;
        Map<Long, Object> byHandle = OBJECTS.computeIfAbsent(type, ignored -> new HashMap<>());
        Object existing = byHandle.get(handle);
        if (existing != null) return (T) existing;
        T created = factory.apply(handle);
        byHandle.put(handle, created);
        return created;
    }

    /** Return a cached Java-only wrapper when no native handle exists. */
    @SuppressWarnings("unchecked")
    public static synchronized <T> T getAllowZero(Class<T> type, LongFunction<T> factory) {
        Map<Long, Object> byHandle = OBJECTS.computeIfAbsent(type, ignored -> new HashMap<>());
        Object existing = byHandle.get(0L);
        if (existing != null) return (T) existing;
        T created = factory.apply(0L);
        byHandle.put(0L, created);
        return created;
    }

    public static synchronized void remove(long handle) {
        for (Map<Long, Object> byHandle : OBJECTS.values()) byHandle.remove(handle);
    }

    public static synchronized void clear() { OBJECTS.clear(); }
}
