package net.minecraft.registry;

import net.minecraft.util.Identifier;
import java.util.Objects;

public final class RegistryKey<T> {
    private final Identifier registry;
    private final Identifier value;
    public RegistryKey(Identifier value) { this(null, value); }
    private RegistryKey(Identifier registry, Identifier value) {
        this.registry = registry;
        this.value = Objects.requireNonNull(value, "value");
    }
    public static <T> RegistryKey<T> of(RegistryKey<?> registry, Identifier value) {
        return new RegistryKey<>(registry == null ? null : registry.getValue(), value);
    }
    public static <T> RegistryKey<T> ofRegistry(Identifier registry) { return new RegistryKey<>(registry, registry); }
    public Identifier getRegistry() { return registry; }
    public Identifier getValue() { return value; }
    public boolean isOf(RegistryKey<?> other) { return other != null && Objects.equals(registry, other.registry) && value.equals(other.value); }
    @Override public String toString() { return registry == null ? value.toString() : registry + " / " + value; }
    @Override public boolean equals(Object other) { return other instanceof RegistryKey<?> k && Objects.equals(registry, k.registry) && value.equals(k.value); }
    @Override public int hashCode() { return Objects.hash(registry, value); }
}
