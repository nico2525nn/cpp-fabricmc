package net.minecraft.registry;

import net.minecraft.util.Identifier;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public class Registry<T> {
    private final Map<Identifier, T> values = new ConcurrentHashMap<>();
    public T get(Identifier id) { return values.get(id); }
    public T getOrThrow(Identifier id) {
        T value = get(id);
        if (value == null) throw new IllegalArgumentException("unknown registry id: " + id);
        return value;
    }
    public boolean containsId(Identifier id) { return values.containsKey(id); }
    public int size() { return values.size(); }
    public Identifier getId(T value) {
        for (Map.Entry<Identifier, T> entry : values.entrySet()) if (entry.getValue() == value) return entry.getKey();
        return null;
    }
    public static <T> T register(Registry<T> registry, Identifier id, T value) {
        registry.values.put(id, value); return value;
    }
}
