package net.minecraft.registry;

import java.util.Objects;
import java.util.Optional;

/** Stable registry value/reference view. */
public class RegistryEntry<T> {
    protected final T value;
    protected final RegistryKey<T> key;
    protected RegistryEntry(T value, RegistryKey<T> key) { this.value = value; this.key = key; }
    public T value() { return value; }
    public T get() { return value; }
    public RegistryKey<T> registryKey() { return key; }
    public Optional<RegistryKey<T>> getKey() { return Optional.ofNullable(key); }
    public boolean matchesKey(RegistryKey<T> other) { return key != null && key.equals(other); }
    public boolean isIn(TagKey<T> tag) { return tag != null && Registry.containsTag(tag, value); }
    public static <T> RegistryEntry<T> of(RegistryKey<T> key, T value) { return new RegistryEntry<>(value, key); }
    @Override public boolean equals(Object other) { return other instanceof RegistryEntry<?> entry && value == entry.value && Objects.equals(key, entry.key); }
    @Override public int hashCode() { return Objects.hash(System.identityHashCode(value), key); }
    @Override public String toString() { return key == null ? String.valueOf(value) : key.toString(); }

    public static final class Reference<T> extends RegistryEntry<T> {
        private Reference(T value, RegistryKey<T> key) { super(value, key); }
        public static <T> Reference<T> of(RegistryKey<T> key, T value) { return new Reference<>(value, key); }
    }
}
