package net.minecraft.registry;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.stream.Stream;

/** Dynamic-registry view used by the 1.21.4 server API. */
public interface DynamicRegistryManager {
    Immutable EMPTY = new ImmutableImpl(List.of());

    <T> Registry<T> getOrThrow(RegistryKey<T> key);
    default <T> Registry<T> get(RegistryKey<T> key) { return getOrThrow(key); }
    default Stream<Entry<?>> streamAllRegistries() { return Stream.empty(); }
    default Immutable toImmutable() { return new ImmutableImpl(streamAllRegistries().toList()); }

    static Immutable of(Registry<?> registry) {
        return registry == null ? EMPTY : new ImmutableImpl(List.of(Entry.of(null, registry)));
    }

    final class Entry<T> {
        private final RegistryKey<T> key;
        private final Registry<T> value;
        private Entry(RegistryKey<T> key, Registry<T> value) { this.key = key; this.value = value; }
        public static <T> Entry<T> of(RegistryKey<T> key, Registry<T> value) { return new Entry<>(key, value); }
        public RegistryKey<T> key() { return key; }
        public Registry<T> value() { return value; }
        public Entry<T> freeze() { return this; }
    }

    interface Immutable extends DynamicRegistryManager {}

    final class ImmutableImpl implements Immutable {
        private final Map<RegistryKey<?>, Registry<?>> registries = new LinkedHashMap<>();

        public ImmutableImpl(List<Entry<?>> entries) {
            if (entries != null) for (Entry<?> entry : entries)
                if (entry != null && entry.key() != null && entry.value() != null)
                    registries.put(entry.key(), entry.value());
        }
        public ImmutableImpl(Stream<Entry<?>> entries) { this(entries == null ? List.of() : entries.toList()); }
        public ImmutableImpl(Map<RegistryKey<?>, Registry<?>> registries) {
            if (registries != null) this.registries.putAll(registries);
        }

        @SuppressWarnings("unchecked")
        @Override public <T> Registry<T> getOrThrow(RegistryKey<T> key) {
            Registry<?> registry = registries.get(key);
            if (registry == null) throw new IllegalArgumentException("unknown dynamic registry: " + key);
            return (Registry<T>) registry;
        }
        @Override public Stream<Entry<?>> streamAllRegistries() {
            List<Entry<?>> entries = new ArrayList<>();
            for (Map.Entry<RegistryKey<?>, Registry<?>> entry : registries.entrySet())
                entries.add(DynamicRegistryManager.ofUnchecked(entry.getKey(), entry.getValue()));
            return entries.stream();
        }
    }

    static <T> Entry<T> entryOf(RegistryKey<T> key, Registry<T> value) { return Entry.of(key, value); }

    private static <T> Entry<T> ofUnchecked(RegistryKey<?> key, Registry<?> value) {
        @SuppressWarnings("unchecked") RegistryKey<T> typedKey = (RegistryKey<T>) key;
        @SuppressWarnings("unchecked") Registry<T> typedValue = (Registry<T>) value;
        return Entry.of(typedKey, typedValue);
    }
}
