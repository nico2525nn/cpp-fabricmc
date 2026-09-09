package net.fabricmc.fabric.api.event.registry;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;

@FunctionalInterface
public interface DynamicRegistrySetupCallback {
    Event<DynamicRegistrySetupCallback> EVENT = EventFactory.createArrayBacked(
        DynamicRegistrySetupCallback.class, callbacks -> view -> {
            for (DynamicRegistrySetupCallback callback : callbacks) callback.onRegistrySetup(view);
        });
    void onRegistrySetup(DynamicRegistryView view);
}
