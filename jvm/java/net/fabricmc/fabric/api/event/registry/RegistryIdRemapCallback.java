package net.fabricmc.fabric.api.event.registry;

import it.unimi.dsi.fastutil.ints.Int2IntMap;
import it.unimi.dsi.fastutil.ints.Int2IntOpenHashMap;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.registry.Registry;
import net.minecraft.util.Identifier;

@FunctionalInterface
public interface RegistryIdRemapCallback<T> {
    void onRemap(RemapState<T> state);

    @SuppressWarnings("unchecked")
    static <T> Event<RegistryIdRemapCallback<T>> event(Registry<T> registry) {
        Class<RegistryIdRemapCallback<T>> type =
            (Class<RegistryIdRemapCallback<T>>) (Class<?>) RegistryIdRemapCallback.class;
        return EventFactory.createArrayBacked(type,
            callbacks -> state -> { for (RegistryIdRemapCallback callback : callbacks)
                callback.onRemap(state); });
    }

    interface RemapState<T> {
        Int2IntMap getRawIdChangeMap();
        Identifier getIdFromOld(int rawId);
        Identifier getIdFromNew(int rawId);
    }

    static <T> RemapState<T> emptyState() {
        return new RemapState<>() {
            @Override public Int2IntMap getRawIdChangeMap() { return new Int2IntOpenHashMap(); }
            @Override public Identifier getIdFromOld(int rawId) { return null; }
            @Override public Identifier getIdFromNew(int rawId) { return null; }
        };
    }
}
