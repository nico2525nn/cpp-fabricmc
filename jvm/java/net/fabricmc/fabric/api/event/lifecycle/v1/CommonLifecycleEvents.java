package net.fabricmc.fabric.api.event.lifecycle.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.registry.DynamicRegistryManager;

public final class CommonLifecycleEvents {
    private CommonLifecycleEvents() { }
    @FunctionalInterface public interface TagsLoaded {
        void onTagsLoaded(DynamicRegistryManager dynamicRegistryManager, boolean client);
    }
    public static final Event<TagsLoaded> TAGS_LOADED = EventFactory.createArrayBacked(
        TagsLoaded.class, callbacks -> (manager, client) -> {
            for (TagsLoaded callback : callbacks) callback.onTagsLoaded(manager, client);
        });
    public static void clear() { TAGS_LOADED.clear(); }
}
