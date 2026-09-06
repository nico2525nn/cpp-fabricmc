package net.minecraft.registry.entry;

/** Owner identity contract for registry entries and tag-backed lists. */
public interface RegistryEntryOwner<T> {
    default boolean ownerEquals(RegistryEntryOwner<T> other) { return this == other; }
}
