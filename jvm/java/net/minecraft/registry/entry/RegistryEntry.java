package net.minecraft.registry.entry;

import java.util.Optional;
import java.util.Objects;
import java.util.function.Predicate;
import java.util.stream.Stream;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.tag.TagKey;

/** Canonical holder interface used by the 1.21.4 registry ABI. */
public interface RegistryEntry<T> {
    T value();
    default T get() { return value(); }
    default Optional<RegistryKey<T>> getKey() { return Optional.empty(); }
    default RegistryKey<T> registryKey() { return getKey().orElse(null); }
    default boolean matchesKey(RegistryKey<T> key) { return Objects.equals(registryKey(), key); }
    default boolean isIn(TagKey<T> tag) { return false; }
    default boolean matchesId(net.minecraft.util.Identifier id) {
        RegistryKey<T> key = registryKey();
        return key != null && key.getValue().equals(id);
    }
    default boolean matches(Predicate<T> predicate) { return predicate != null && predicate.test(value()); }
    default Stream<TagKey<T>> streamTags() { return Stream.empty(); }
    static <T> RegistryEntry<T> of(RegistryKey<T> key, T value) { return new Direct<>(value, key); }

    final class Direct<T> implements RegistryEntry<T> {
        private final T value;
        private final RegistryKey<T> key;
        public Direct(T value, RegistryKey<T> key) { this.value = value; this.key = key; }
        @Override public T value() { return value; }
        @Override public Optional<RegistryKey<T>> getKey() { return Optional.ofNullable(key); }
    }

    final class Reference<T> implements RegistryEntry<T> {
        private final T value;
        private final RegistryKey<T> key;
        public Reference(T value, RegistryKey<T> key) { this.value = value; this.key = key; }
        @Override public T value() { return value; }
        @Override public Optional<RegistryKey<T>> getKey() { return Optional.ofNullable(key); }
    }

    enum Type { DIRECT, STAND_ALONE, INTRUSIVE, REFERENCED }
}
