package net.minecraft.registry;

import java.util.Objects;
import net.minecraft.util.Identifier;

public final class TagKey<T> {
    private final RegistryKey<? extends Registry<T>> registry;
    private final Identifier id;
    private TagKey(RegistryKey<? extends Registry<T>> registry, Identifier id) { this.registry = registry; this.id = id; }
    public static <T> TagKey<T> of(RegistryKey<? extends Registry<T>> registry, Identifier id) {
        return new TagKey<>(Objects.requireNonNull(registry, "registry"), Objects.requireNonNull(id, "id"));
    }
    public RegistryKey<? extends Registry<T>> registry() { return registry; }
    public Identifier id() { return id; }
    public Identifier getId() { return id; }
    @Override public boolean equals(Object other) { return other instanceof TagKey<?> tag && registry.equals(tag.registry) && id.equals(tag.id); }
    @Override public int hashCode() { return Objects.hash(registry, id); }
    @Override public String toString() { return "#" + id; }
}
