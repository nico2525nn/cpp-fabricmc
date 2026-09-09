package net.minecraft.registry;

import java.util.List;
import com.mojang.serialization.Lifecycle;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.registry.entry.RegistryEntryInfo;

/** 1.21.4 mutable-registry interface implemented by SimpleRegistry. */
public interface MutableRegistry<T> extends Registry<T> {
    default RegistryWrapper.Impl<T> createMutableRegistryLookup() {
        @SuppressWarnings("unchecked") RegistryKey<T> key = (RegistryKey<T>) getKey();
        return new RegistryWrapper.Impl<>(key);
    }
    default boolean isEmpty() { return size() == 0; }
    default RegistryEntry.Reference<T> add(RegistryKey<T> key, T value, Lifecycle info) {
        if (key == null) throw new NullPointerException("key");
        Registry.register(this, key.getValue(), value);
        return new RegistryEntry.Reference<>(value, key);
    }
    default RegistryEntry.Reference<T> add(RegistryKey<T> key, T value, RegistryEntryInfo info) {
        if (key == null) throw new NullPointerException("key");
        Registry.register(this, key.getValue(), value);
        return new RegistryEntry.Reference<>(value, key);
    }
    default void setEntries(net.minecraft.registry.tag.TagKey<T> tag,
                            List<RegistryEntry<T>> entries) { }
}
