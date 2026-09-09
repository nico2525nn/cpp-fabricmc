package net.fabricmc.fabric.api.command.v2;

import java.util.Map;
import java.util.WeakHashMap;
import net.minecraft.util.Identifier;

/** Per-reader flag storage used by custom selector options. */
public interface FabricEntitySelectorReader {
    Map<Object, Map<Identifier, Boolean>> CUSTOM_FLAGS =
        java.util.Collections.synchronizedMap(new WeakHashMap<>());

    default void setCustomFlag(Identifier id, boolean value) {
        if (id == null) return;
        synchronized (CUSTOM_FLAGS) {
            CUSTOM_FLAGS.computeIfAbsent(this, ignored -> new java.util.HashMap<>()).put(id, value);
        }
    }

    default boolean getCustomFlag(Identifier id) {
        if (id == null) return false;
        synchronized (CUSTOM_FLAGS) {
            return CUSTOM_FLAGS.getOrDefault(this, Map.of()).getOrDefault(id, false);
        }
    }
}
