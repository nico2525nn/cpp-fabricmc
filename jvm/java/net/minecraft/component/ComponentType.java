package net.minecraft.component;

/** Mojang-mapped component key name retained alongside Yarn's DataComponentType. */
public final class ComponentType<T> {
    private final String id;
    public ComponentType(String id) { this.id = id == null ? "" : id; }
    public String id() { return id; }
    @Override public boolean equals(Object other) {
        return other instanceof ComponentType<?> type && id.equals(type.id);
    }
    @Override public int hashCode() { return id.hashCode(); }
    @Override public String toString() { return id; }
}
