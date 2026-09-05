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
        if (handle == 0) return null;
        Map<Long, Object> byHandle = OBJECTS.computeIfAbsent(type, ignored -> new HashMap<>());
        Object existing = byHandle.get(handle);
        if (existing != null) return (T) existing;
        T created = factory.apply(handle);
        byHandle.put(handle, created);
        return created;
    }

    public static synchronized void remove(long handle) {
        for (Map<Long, Object> byHandle : OBJECTS.values()) byHandle.remove(handle);
    }

    public static synchronized void clear() { OBJECTS.clear(); }
}
