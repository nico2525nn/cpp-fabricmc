package net.minecraft.registry;

import net.minecraft.util.Identifier;

public final class RegistryKey<T> {
    private final Identifier value;
    public RegistryKey(Identifier value) { this.value = value; }
    public static <T> RegistryKey<T> of(RegistryKey<?> registry, Identifier value) {
        return new RegistryKey<>(value);
    }
    public Identifier getValue() { return value; }
    @Override public String toString() { return value.toString(); }
    @Override public boolean equals(Object other) { return other instanceof RegistryKey<?> k && value.equals(k.value); }
    @Override public int hashCode() { return value.hashCode(); }
}
