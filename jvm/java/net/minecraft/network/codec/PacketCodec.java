package net.minecraft.network.codec;

import java.util.Objects;
import java.util.function.BiConsumer;
import java.util.function.BiFunction;
import java.util.function.Function;

/**
 * Small source-compatible packet codec abstraction for the 1.21.4 payload API.
 *
 * <p>The native transport still owns framing.  A codec only owns the
 * deterministic value-to-buffer mapping, which is exactly the part Fabric
 * mods use when declaring a {@code CustomPayload}.</p>
 */
public interface PacketCodec<B, V> {
    V decode(B buffer);

    void encode(B buffer, V value);

    default <T> PacketCodec<B, T> xmap(Function<? super V, ? extends T> to,
                                        Function<? super T, ? extends V> from) {
        Objects.requireNonNull(to, "to");
        Objects.requireNonNull(from, "from");
        return ofStatic((buffer, value) -> encode(buffer, from.apply(value)),
            buffer -> to.apply(decode(buffer)));
    }

    default <T> PacketCodec<B, T> map(Function<? super V, ? extends T> to,
                                      Function<? super T, ? extends V> from) {
        return xmap(to, from);
    }

    default <C> PacketCodec<C, V> mapBuf(Function<? super C, ? extends B> mapper) {
        Objects.requireNonNull(mapper, "mapper");
        return ofStatic((buffer, value) -> encode(mapper.apply(buffer), value),
            buffer -> decode(mapper.apply(buffer)));
    }

    static <B, V> PacketCodec<B, V> of(BiConsumer<? super B, ? super V> encoder,
                                       Function<? super B, ? extends V> decoder) {
        return ofStatic(encoder, decoder);
    }

    static <B, V> PacketCodec<B, V> ofStatic(BiConsumer<? super B, ? super V> encoder,
                                             Function<? super B, ? extends V> decoder) {
        Objects.requireNonNull(encoder, "encoder");
        Objects.requireNonNull(decoder, "decoder");
        return new PacketCodec<>() {
            @Override public V decode(B buffer) { return decoder.apply(buffer); }
            @Override public void encode(B buffer, V value) { encoder.accept(buffer, value); }
        };
    }

    static <B, V> PacketCodec<B, V> unit(V value) {
        return ofStatic((buffer, ignored) -> { }, buffer -> value);
    }

    static <B, T1, V> PacketCodec<B, V> tuple(PacketCodec<? super B, T1> codec1,
                                               Function<V, T1> getter1,
                                               Function<T1, V> constructor) {
        Objects.requireNonNull(codec1, "codec1");
        Objects.requireNonNull(getter1, "getter1");
        Objects.requireNonNull(constructor, "constructor");
        return ofStatic((buffer, value) -> codec1.encode(buffer, getter1.apply(value)),
            buffer -> constructor.apply(codec1.decode(buffer)));
    }

    static <B, T1, T2, V> PacketCodec<B, V> tuple(PacketCodec<? super B, T1> codec1,
                                                   Function<V, T1> getter1,
                                                   PacketCodec<? super B, T2> codec2,
                                                   Function<V, T2> getter2,
                                                   BiFunction<T1, T2, V> constructor) {
        Objects.requireNonNull(codec1, "codec1");
        Objects.requireNonNull(codec2, "codec2");
        return ofStatic((buffer, value) -> {
            codec1.encode(buffer, getter1.apply(value));
            codec2.encode(buffer, getter2.apply(value));
        }, buffer -> constructor.apply(codec1.decode(buffer), codec2.decode(buffer)));
    }

    static <B, T1, T2, T3, V> PacketCodec<B, V> tuple(PacketCodec<? super B, T1> codec1,
                                                       Function<V, T1> getter1,
                                                       PacketCodec<? super B, T2> codec2,
                                                       Function<V, T2> getter2,
                                                       PacketCodec<? super B, T3> codec3,
                                                       Function<V, T3> getter3,
                                                       TriFunction<T1, T2, T3, V> constructor) {
        Objects.requireNonNull(codec1, "codec1");
        Objects.requireNonNull(codec2, "codec2");
        Objects.requireNonNull(codec3, "codec3");
        return ofStatic((buffer, value) -> {
            codec1.encode(buffer, getter1.apply(value));
            codec2.encode(buffer, getter2.apply(value));
            codec3.encode(buffer, getter3.apply(value));
        }, buffer -> constructor.apply(codec1.decode(buffer), codec2.decode(buffer), codec3.decode(buffer)));
    }

    static <B, T1, T2, T3, T4, V> PacketCodec<B, V> tuple(PacketCodec<? super B, T1> codec1,
                                                           Function<V, T1> getter1,
                                                           PacketCodec<? super B, T2> codec2,
                                                           Function<V, T2> getter2,
                                                           PacketCodec<? super B, T3> codec3,
                                                           Function<V, T3> getter3,
                                                           PacketCodec<? super B, T4> codec4,
                                                           Function<V, T4> getter4,
                                                           QuadFunction<T1, T2, T3, T4, V> constructor) {
        Objects.requireNonNull(codec1, "codec1");
        Objects.requireNonNull(codec2, "codec2");
        Objects.requireNonNull(codec3, "codec3");
        Objects.requireNonNull(codec4, "codec4");
        return ofStatic((buffer, value) -> {
            codec1.encode(buffer, getter1.apply(value));
            codec2.encode(buffer, getter2.apply(value));
            codec3.encode(buffer, getter3.apply(value));
            codec4.encode(buffer, getter4.apply(value));
        }, buffer -> constructor.apply(codec1.decode(buffer), codec2.decode(buffer),
            codec3.decode(buffer), codec4.decode(buffer)));
    }

    @FunctionalInterface
    interface TriFunction<A, B, C, R> { R apply(A first, B second, C third); }

    @FunctionalInterface
    interface QuadFunction<A, B, C, D, R> { R apply(A first, B second, C third, D fourth); }
}
