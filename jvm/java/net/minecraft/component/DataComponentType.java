package net.minecraft.component;

import java.util.Objects;

/** Typed key for the 1.20.5+ item data-component API. */
public final class DataComponentType<T> {
    private final String id;
    public DataComponentType(String id) { this.id = id == null ? "" : id; }
    public String id() { return id; }
    @Override public boolean equals(Object other) { return other instanceof DataComponentType<?> t && id.equals(t.id); }
    @Override public int hashCode() { return Objects.hash(id); }
    @Override public String toString() { return id; }
}
