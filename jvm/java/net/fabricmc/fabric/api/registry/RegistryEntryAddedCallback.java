package net.fabricmc.fabric.api.registry;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.registry.Registry;
import net.minecraft.util.Identifier;

@FunctionalInterface
public interface RegistryEntryAddedCallback<T> {
    void onEntryAdded(int rawId, Identifier id, T entry);
    @SuppressWarnings("unchecked")
    static <T> Event<RegistryEntryAddedCallback<T>> event(Registry<T> registry) {
        if (registry == null) throw new NullPointerException("registry");
        Class<RegistryEntryAddedCallback<T>> type = (Class<RegistryEntryAddedCallback<T>>) (Class<?>) RegistryEntryAddedCallback.class;
        return new Event<>(listener -> registry.addEntryListener((rawId, id, entry) -> listener.onEntryAdded(rawId, id, entry)),
            type, (RegistryEntryAddedCallback<T>[] callbacks) -> (rawId, id, entry) -> {
            for (RegistryEntryAddedCallback<T> callback : callbacks) callback.onEntryAdded(rawId, id, entry);
        });
    }
}
