package net.minecraft.component;

import java.util.stream.Stream;

/** Read-only component holder contract implemented by ItemStack. */
public interface ComponentHolder {
    ComponentMap getComponents();

    @SuppressWarnings("unchecked")
    default <T> T get(ComponentType<? extends T> type) {
        return type == null ? null : (T) getComponents().get(type);
    }

    default <T> T getOrDefault(ComponentType<? extends T> type, T fallback) {
        T value = get(type);
        return value == null ? fallback : value;
    }

    default boolean contains(ComponentType<?> type) { return get(type) != null; }

    default <T> Stream<T> streamAll(Class<? extends T> valueClass) {
        if (valueClass == null) return Stream.empty();
        return getComponents().stream()
            .map(Component::value)
            .filter(valueClass::isInstance)
            .map(valueClass::cast);
    }
}
