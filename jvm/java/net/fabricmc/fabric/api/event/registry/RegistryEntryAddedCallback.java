package net.fabricmc.fabric.api.event.registry;

import java.util.function.Consumer;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.registry.Registry;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.util.Identifier;

@FunctionalInterface
public interface RegistryEntryAddedCallback<T> {
    void onEntryAdded(int rawId, Identifier id, T entry);

    @SuppressWarnings("unchecked")
    static <T> Event<RegistryEntryAddedCallback<T>> event(Registry<T> registry) {
        if (registry == null) throw new NullPointerException("registry");
        Class<RegistryEntryAddedCallback<T>> type =
            (Class<RegistryEntryAddedCallback<T>>) (Class<?>) RegistryEntryAddedCallback.class;
        return new Event<>(listener -> registry.addEntryListener(
            (rawId, id, entry) -> listener.onEntryAdded(rawId, id, entry)), type,
            (RegistryEntryAddedCallback<T>[] callbacks) -> (rawId, id, entry) -> {
                for (RegistryEntryAddedCallback<T> callback : callbacks)
                    callback.onEntryAdded(rawId, id, entry);
            });
    }

    static <T> void allEntries(Registry<T> registry,
                               Consumer<RegistryEntry.Reference<T>> consumer) {
        if (registry == null || consumer == null) return;
        for (T value : registry) {
            RegistryEntry<T> entry = registry.getEntry(value).orElse(null);
            if (entry instanceof RegistryEntry.Reference<T> reference) consumer.accept(reference);
            else if (entry != null) consumer.accept(new RegistryEntry.Reference<>(
                entry.value(), entry.registryKey()));
        }
    }
}
