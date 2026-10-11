package cppfm.bridge;

import java.util.HashMap;
import java.util.Map;
import java.util.function.LongFunction;
import java.util.function.LongPredicate;

/** Strong identity cache for live native-backed Java wrappers. */
public final class WrapperCache {
    private static final Map<Class<?>, Map<Long, Object>> OBJECTS = new HashMap<>();

    private WrapperCache() {}

    @SuppressWarnings("unchecked")
    public static synchronized <T> T getIfPresent(Class<T> type, long handle) {
        if (handle == 0L) return null;
        Map<Long, Object> byHandle = OBJECTS.get(type);
        return byHandle == null ? null : (T) byHandle.get(handle);
    }

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

    /**
     * Return an existing wrapper even during retirement, but require an
     * explicit native lifecycle check before constructing a new one.
     */
    @SuppressWarnings("unchecked")
    public static synchronized <T> T get(Class<T> type, long handle,
                                          LongPredicate mayCreate,
                                          LongFunction<T> factory) {
        if (handle == 0L || mayCreate == null) return null;
        Map<Long, Object> byHandle = OBJECTS.computeIfAbsent(type, ignored -> new HashMap<>());
        Object existing = byHandle.get(handle);
        if (existing != null) return (T) existing;
        if (!mayCreate.test(handle)) return null;
        T created = factory.apply(handle);
        if (created == null) return null;
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
