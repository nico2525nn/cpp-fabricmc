package net.fabricmc.fabric.api.event.registry;

import java.util.Optional;
import java.util.stream.Stream;
import net.minecraft.registry.DynamicRegistryManager;
import net.minecraft.registry.Registry;
import net.minecraft.registry.RegistryKey;

public interface DynamicRegistryView {
    DynamicRegistryManager asDynamicRegistryManager();
    Stream<Registry<?>> stream();
    <T> Optional<Registry<T>> getOptional(
        RegistryKey<? extends Registry<? extends T>> registryKey);
    <T> void registerEntryAdded(RegistryKey<? extends Registry<? extends T>> registryKey,
        RegistryEntryAddedCallback<T> callback);
}
