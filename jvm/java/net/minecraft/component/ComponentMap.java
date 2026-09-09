package net.minecraft.component;

import com.mojang.serialization.Codec;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import java.util.function.Predicate;
import java.util.stream.Stream;

/**
 * Immutable view of typed item components.
 *
 * <p>The public 1.21.4 Yarn contract is an interface. A mutable builder and
 * a compact map-backed implementation are kept inside the interface so the
 * native shadow can expose the same API without leaking mutability to callers.</p>
 */
public interface ComponentMap extends Iterable<Component<?>> {
    ComponentMap EMPTY = new Builder.SimpleComponentMap(Map.of());
    Codec<ComponentMap> CODEC = new Codec<>() { };

    Object get(ComponentType<?> type);
    Set<ComponentType<?>> getTypes();

    default boolean contains(ComponentType<?> type) { return type != null && get(type) != null; }

    @SuppressWarnings("unchecked")
    default <T> T getOrDefault(ComponentType<? extends T> type, T fallback) {
        Object value = get(type);
        return value == null ? fallback : (T) value;
    }

    @SuppressWarnings("unchecked")
    default <T> Component<T> copy(ComponentType<T> type) {
        Object value = get(type);
        return value == null ? null : Component.of(type, (T) value);
    }

    @Override
    default java.util.Iterator<Component<?>> iterator() { return stream().iterator(); }

    default Stream<Component<?>> stream() {
        return getTypes().stream().map(type -> copyUntyped(this, type));
    }

    default int size() { return getTypes().size(); }
    default boolean isEmpty() { return getTypes().isEmpty(); }

    default ComponentMap filtered(Predicate<ComponentType<?>> predicate) {
        if (predicate == null) return EMPTY;
        Map<ComponentType<?>, Object> filtered = new LinkedHashMap<>();
        for (ComponentType<?> type : getTypes()) {
            if (predicate.test(type)) filtered.put(type, get(type));
        }
        return of(filtered);
    }

    /** Non-official convenience used by the existing native item facade. */
    default Map<ComponentType<?>, Object> asMap() {
        Map<ComponentType<?>, Object> result = new LinkedHashMap<>();
        for (ComponentType<?> type : getTypes()) result.put(type, get(type));
        return Collections.unmodifiableMap(result);
    }

    static Builder builder() { return new Builder(); }

    static ComponentMap of(Map<? extends ComponentType<?>, ?> source) {
        return new Builder().addAll(source).build();
    }

    static ComponentMap of(ComponentMap base, ComponentMap overrides) {
        Map<ComponentType<?>, Object> values = new LinkedHashMap<>();
        if (base != null) values.putAll(base.asMap());
        if (overrides != null) values.putAll(overrides.asMap());
        return of(values);
    }

    static Codec<ComponentMap> createCodec(Codec<ComponentType<?>> componentTypeCodec) {
        return CODEC;
    }

    static Codec<ComponentMap> createCodecFromValueMap(Codec<Map<ComponentType<?>, Object>> valueMapCodec) {
        return CODEC;
    }

    @SuppressWarnings("unchecked")
    private static Component<?> copyUntyped(ComponentMap map, ComponentType<?> type) {
        return map.copy((ComponentType<Object>) type);
    }

    /** Mutable construction surface matching Yarn's ComponentMap.Builder. */
    class Builder implements net.fabricmc.fabric.api.item.v1.FabricComponentMapBuilder {
        private final Map<ComponentType<?>, Object> values = new LinkedHashMap<>();

        public <T> Builder add(ComponentType<T> type, T value) {
            if (type == null) throw new NullPointerException("type");
            values.put(type, value);
            return this;
        }

        public <T> Builder add(Component<T> component) {
            if (component == null) throw new NullPointerException("component");
            return add(component.type(), component.value());
        }

        /** Vanilla's builder alias used by item component mixins. */
        public <T> Builder put(ComponentType<T> type, T value) {
            return add(type, value);
        }

        public <T> T get(ComponentType<? extends T> type) {
            @SuppressWarnings("unchecked") T value = (T) values.get(type);
            return value;
        }

        public boolean contains(ComponentType<?> type) {
            return type != null && values.containsKey(type);
        }

        public Builder addAll(Map<? extends ComponentType<?>, ?> source) {
            if (source != null) source.forEach(this::addUntyped);
            return this;
        }

        public Builder addAll(ComponentMap source) {
            if (source != null) addAll(source.asMap());
            return this;
        }

        public <T> Builder remove(ComponentType<T> type) {
            if (type != null) values.remove(type);
            return this;
        }

        public ComponentMap build() { return new SimpleComponentMap(values); }

        private void addUntyped(ComponentType<?> type, Object value) {
            if (type != null) values.put(type, value);
        }

        /** Official implementation name: ComponentMap$Builder$SimpleComponentMap. */
        public static final class SimpleComponentMap implements ComponentMap {
            private final Map<ComponentType<?>, Object> values;

            public SimpleComponentMap(Map<? extends ComponentType<?>, ?> source) {
                Map<ComponentType<?>, Object> copy = new LinkedHashMap<>();
                if (source != null) source.forEach((type, value) -> {
                    if (type != null) copy.put(type, value);
                });
                values = Collections.unmodifiableMap(copy);
            }

            @Override public Object get(ComponentType<?> type) {
                return type == null ? null : values.get(type);
            }

            @Override public Set<ComponentType<?>> getTypes() { return values.keySet(); }

            @Override public boolean equals(Object other) {
                return other instanceof ComponentMap map && values.equals(map.asMap());
            }

            @Override public int hashCode() { return values.hashCode(); }
            @Override public String toString() { return values.toString(); }
        }
    }
}
