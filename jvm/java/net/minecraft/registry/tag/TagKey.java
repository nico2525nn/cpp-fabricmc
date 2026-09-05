package net.minecraft.registry.tag;

import java.util.Objects;
import net.minecraft.registry.Registry;
import net.minecraft.registry.RegistryKey;
import net.minecraft.util.Identifier;

/** Canonical 1.21.4 tag key. */
public final class TagKey<T> {
    private final RegistryKey<? extends Registry<T>> registryRef;
    private final Identifier id;

    private TagKey(RegistryKey<? extends Registry<T>> registryRef, Identifier id) {
        this.registryRef = Objects.requireNonNull(registryRef, "registry");
        this.id = Objects.requireNonNull(id, "id");
    }

    public static <T> TagKey<T> of(RegistryKey<? extends Registry<T>> registry, Identifier id) {
        return new TagKey<>(registry, id);
    }

    public RegistryKey<? extends Registry<T>> registryRef() { return registryRef; }
    public RegistryKey<? extends Registry<T>> registry() { return registryRef; }
    public Identifier id() { return id; }
    public Identifier getId() { return id; }

    @Override public boolean equals(Object other) {
        return other instanceof TagKey<?> tag
            && registryRef.equals(tag.registryRef) && id.equals(tag.id);
    }
    @Override public int hashCode() { return Objects.hash(registryRef, id); }
    @Override public String toString() { return "#" + id; }
}
