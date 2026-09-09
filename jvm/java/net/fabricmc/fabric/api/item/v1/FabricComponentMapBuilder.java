package net.fabricmc.fabric.api.item.v1;

import java.util.List;
import java.util.function.Supplier;
import net.minecraft.component.ComponentMap;
import net.minecraft.component.ComponentType;

/** Access-widened component builder helpers used by default item components. */
public interface FabricComponentMapBuilder {
    @SuppressWarnings("unchecked")
    default <T> T getOrCreate(ComponentType<T> type, Supplier<T> defaultValue) {
        T value = getOrDefault(type, null);
        if (value != null) return value;
        T created = defaultValue == null ? null : defaultValue.get();
        if (this instanceof ComponentMap.Builder builder && created != null)
            builder.add(type, created);
        return created;
    }

    @SuppressWarnings("unchecked")
    default <T> T getOrDefault(ComponentType<T> type, T fallback) {
        if (this instanceof ComponentMap.Builder builder) {
            Object value = builder.get(type);
            return value == null ? fallback : (T) value;
        }
        return fallback;
    }

    @SuppressWarnings("unchecked")
    default <T> List<T> getOrEmpty(ComponentType<List<T>> type) {
        return getOrDefault(type, List.of());
    }

    default boolean contains(ComponentType<?> type) {
        return this instanceof ComponentMap.Builder builder && builder.contains(type);
    }
}
