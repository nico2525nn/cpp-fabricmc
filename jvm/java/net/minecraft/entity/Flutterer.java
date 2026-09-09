package net.minecraft.entity;

/** Capability implemented by flying entities that expose an airborne state. */
public interface Flutterer {
    default boolean isInAir() { return true; }
}
