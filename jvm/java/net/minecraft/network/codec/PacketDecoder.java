package net.minecraft.network.codec;

/** Functional interface for a packet decoder. */
@FunctionalInterface
public interface PacketDecoder<B, V> {
    V decode(B buf);
}
