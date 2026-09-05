package net.minecraft.component;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/** Immutable-ish value map used by ItemStack; mutation is exposed only through ItemStack. */
public final class ComponentMap {
    public static final ComponentMap EMPTY = new ComponentMap(Map.of());
    private final Map<DataComponentType<?>, Object> values;
    public ComponentMap() { this.values = new LinkedHashMap<>(); }
    private ComponentMap(Map<DataComponentType<?>, Object> values) { this.values = Collections.unmodifiableMap(new LinkedHashMap<>(values)); }
    public static ComponentMap of(Map<DataComponentType<?>, ?> source) {
        Map<DataComponentType<?>, Object> copy = new LinkedHashMap<>();
        if (source != null) copy.putAll(source);
        return new ComponentMap(copy);
    }
    public boolean contains(DataComponentType<?> type) { return type != null && values.containsKey(type); }
    @SuppressWarnings("unchecked") public <T> T get(DataComponentType<T> type) { return type == null ? null : (T) values.get(type); }
    public int size() { return values.size(); }
    public Map<DataComponentType<?>, Object> asMap() { return values; }
    @Override public String toString() { return values.toString(); }
}
