package net.fabricmc.fabric.api.event.registry;

import java.util.function.Consumer;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
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
        return new Event<>() {
            private final List<RegistryEntryAddedCallback<T>> listeners = new ArrayList<>();

            {
                invoker = (rawId, id, entry) -> {
                    List<RegistryEntryAddedCallback<T>> snapshot;
                    synchronized (this) { snapshot = List.copyOf(listeners); }
                    for (RegistryEntryAddedCallback<T> callback : snapshot)
                        callback.onEntryAdded(rawId, id, entry);
                };
            }

            @Override
            public synchronized void register(RegistryEntryAddedCallback<T> listener) {
                RegistryEntryAddedCallback<T> checked = Objects.requireNonNull(listener, "listener");
                listeners.add(checked);
                registry.addEntryListener(checked::onEntryAdded);
            }
        };
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
