package net.minecraft.network.codec;

/** Functional interface for a static buffer-first packet encoder. */
@FunctionalInterface
public interface PacketEncoder<B, V> {
    void encode(B buf, V value);
}
