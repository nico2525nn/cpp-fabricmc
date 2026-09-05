package net.minecraft.network.packet;

public interface Packet<T> {
    default void apply(T listener) { }
    default boolean isWritingErrorSkippable() { return false; }
}
