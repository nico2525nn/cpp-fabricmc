package net.minecraft.registry;

import net.minecraft.util.Identifier;

/** Registry view with a deterministic fallback identifier. */
public interface DefaultedRegistry<T> extends MutableRegistry<T> {
    Identifier getDefaultId();
}
