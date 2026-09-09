package net.minecraft.network.codec;

/** Functional interface for an instance-style value-first packet encoder. */
@FunctionalInterface
public interface ValueFirstEncoder<V, B> {
    void encode(V value, B buf);
}
