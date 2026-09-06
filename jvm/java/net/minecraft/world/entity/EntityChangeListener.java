package net.minecraft.world.entity;

/** Marker/callback ABI for vanilla's entity section change listener. */
public interface EntityChangeListener {
    default void updateEntityPosition() { }
}
