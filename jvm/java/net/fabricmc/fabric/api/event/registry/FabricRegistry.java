package net.fabricmc.fabric.api.event.registry;

import net.minecraft.util.Identifier;

/** Additional registry metadata hooks supplied by Fabric Registry Sync. */
public interface FabricRegistry {
    default void addAlias(Identifier from, Identifier to) { }
}
