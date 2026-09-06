package net.minecraft.registry;

import java.util.Optional;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.registry.entry.RegistryEntryList;
import net.minecraft.registry.tag.TagKey;

/** Lookup view for registry entries and tags. */
public interface RegistryEntryLookup<T> {
    default Optional<RegistryEntry<T>> getOptional(RegistryKey<T> key) {
        return Optional.empty();
    }

    default RegistryEntry.Reference<T> getOrThrow(RegistryKey<T> key) {
        RegistryEntry<T> entry = getOptional(key).orElseThrow(() ->
            new IllegalArgumentException("Unknown registry key: " + key));
        return entry instanceof RegistryEntry.Reference<T> reference
            ? reference : new RegistryEntry.Reference<>(entry.value(), key);
    }

    default Optional<RegistryEntryList<T>> getOptional(TagKey<T> tag) {
        return Optional.empty();
    }

    @SuppressWarnings("unchecked")
    default RegistryEntryList.Named<T> getOrThrow(TagKey<T> tag) {
        return (RegistryEntryList.Named<T>) getOptional(tag).orElseThrow(() ->
            new IllegalArgumentException("Unknown registry tag: " + tag));
    }

    interface RegistryLookup<T> extends RegistryEntryLookup<T> { }
}
