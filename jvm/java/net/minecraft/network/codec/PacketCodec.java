package net.minecraft.network.codec;

import java.util.Objects;
import java.util.function.BiConsumer;
import java.util.function.BiFunction;
import java.util.function.Function;
import com.mojang.datafixers.util.Function3;
import com.mojang.datafixers.util.Function4;
import com.mojang.datafixers.util.Function5;
import com.mojang.datafixers.util.Function6;
import com.mojang.datafixers.util.Function7;
import com.mojang.datafixers.util.Function8;

/**
 * Small source-compatible packet codec abstraction for the 1.21.4 payload API.
 *
 * <p>The native transport still owns framing.  A codec only owns the
 * deterministic value-to-buffer mapping, which is exactly the part Fabric
 * mods use when declaring a {@code CustomPayload}.</p>
 */
public interface PacketCodec<B, V> {
    @FunctionalInterface
    interface ResultFunction<B, S, T> {
        PacketCodec<B, T> apply(PacketCodec<B, S> codec);
    }

    V decode(B buffer);

    void encode(B buffer, V value);

    /** Official 1.21.4 unchecked buffer narrowing helper. */
    @SuppressWarnings("unchecked")
    default <S> PacketCodec<S, V> cast() {
        return (PacketCodec<S, V>) this;
    }

    default <T> PacketCodec<B, T> xmap(Function<? super V, ? extends T> to,
                                        Function<? super T, ? extends V> from) {
        Objects.requireNonNull(to, "to");
        Objects.requireNonNull(from, "from");
        return ofLegacy((buffer, value) -> encode(buffer, from.apply(value)),
            buffer -> to.apply(decode(buffer)));
    }

    default <T> PacketCodec<B, T> map(Function<? super V, ? extends T> to,
                                      Function<? super T, ? extends V> from) {
        return xmap(to, from);
    }

    /**
     * Selects a value codec from a value's discriminator, matching the
     * 1.21.4 Yarn {@code PacketCodec.dispatch} contract.
     */
    @SuppressWarnings({"unchecked", "rawtypes"})
    default <U> PacketCodec<B, U> dispatch(
            Function<? super U, ? extends V> type,
            Function<? super V, ? extends PacketCodec<? super B, ? extends U>> codec) {
        Objects.requireNonNull(type, "type");
        Objects.requireNonNull(codec, "codec");
        return ofLegacy((buffer, value) -> {
            V discriminator = type.apply(value);
            encode(buffer, discriminator);
            PacketCodec selected = codec.apply(discriminator);
            if (selected == null) throw new IllegalArgumentException("codec returned null");
            selected.encode(buffer, value);
        }, buffer -> {
            V discriminator = decode(buffer);
            PacketCodec selected = codec.apply(discriminator);
            if (selected == null) throw new IllegalArgumentException("codec returned null");
            return (U) selected.decode(buffer);
        });
    }

    default <C> PacketCodec<C, V> mapBuf(Function<? super C, ? extends B> mapper) {
        Objects.requireNonNull(mapper, "mapper");
        return ofLegacy((buffer, value) -> encode(mapper.apply(buffer), value),
            buffer -> decode(mapper.apply(buffer)));
    }

    default <O> PacketCodec<B, O> collect(ResultFunction<B, V, O> function) {
        Objects.requireNonNull(function, "function");
        return function.apply(this);
    }

    static <B, V> PacketCodec<B, V> of(BiConsumer<? super B, ? super V> encoder,
                                       Function<? super B, ? extends V> decoder) {
        return ofLegacy(encoder, decoder);
    }

    /** Official 1.21.4 overload for instance-style value-first encoders. */
    static <B, V> PacketCodec<B, V> of(ValueFirstEncoder<? super V, ? super B> encoder,
                                       PacketDecoder<? super B, ? extends V> decoder) {
        Objects.requireNonNull(encoder, "encoder");
        Objects.requireNonNull(decoder, "decoder");
        return ofLegacy((buffer, value) -> encoder.encode(value, buffer), decoder::decode);
    }

    /** Official 1.21.4 overload for static buffer-first encoders. */
    static <B, V> PacketCodec<B, V> ofStatic(PacketEncoder<? super B, ? super V> encoder,
                                             PacketDecoder<? super B, ? extends V> decoder) {
        Objects.requireNonNull(encoder, "encoder");
        Objects.requireNonNull(decoder, "decoder");
        return ofLegacy((buffer, value) -> encoder.encode(buffer, value), decoder::decode);
    }

    /** Internal lambda-friendly implementation for the local shadow API. */
    static <B, V> PacketCodec<B, V> ofLegacy(BiConsumer<? super B, ? super V> encoder,
                                             Function<? super B, ? extends V> decoder) {
        Objects.requireNonNull(encoder, "encoder");
        Objects.requireNonNull(decoder, "decoder");
        return new PacketCodec<>() {
            @Override public V decode(B buffer) { return decoder.apply(buffer); }
            @Override public void encode(B buffer, V value) { encoder.accept(buffer, value); }
        };
    }

    static <B, V> PacketCodec<B, V> unit(V value) {
        return ofLegacy((buffer, actual) -> {
            if (!Objects.equals(value, actual))
                throw new IllegalStateException("value does not match unit codec");
        }, buffer -> value);
    }

    static <B, T1, V> PacketCodec<B, V> tuple(PacketCodec<? super B, T1> codec1,
                                               Function<V, T1> getter1,
                                               Function<T1, V> constructor) {
        Objects.requireNonNull(codec1, "codec1");
        Objects.requireNonNull(getter1, "getter1");
        Objects.requireNonNull(constructor, "constructor");
        return ofLegacy((buffer, value) -> codec1.encode(buffer, getter1.apply(value)),
            buffer -> constructor.apply(codec1.decode(buffer)));
    }

    static <B, T1, T2, V> PacketCodec<B, V> tuple(PacketCodec<? super B, T1> codec1,
                                                   Function<V, T1> getter1,
                                                   PacketCodec<? super B, T2> codec2,
                                                   Function<V, T2> getter2,
                                                   BiFunction<T1, T2, V> constructor) {
        Objects.requireNonNull(codec1, "codec1");
        Objects.requireNonNull(codec2, "codec2");
        return ofLegacy((buffer, value) -> {
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
                                                       Function3<T1, T2, T3, V> constructor) {
        Objects.requireNonNull(codec1, "codec1");
        Objects.requireNonNull(codec2, "codec2");
        Objects.requireNonNull(codec3, "codec3");
        return ofLegacy((buffer, value) -> {
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
                                                           Function4<T1, T2, T3, T4, V> constructor) {
        Objects.requireNonNull(codec1, "codec1");
        Objects.requireNonNull(codec2, "codec2");
        Objects.requireNonNull(codec3, "codec3");
        Objects.requireNonNull(codec4, "codec4");
        return ofLegacy((buffer, value) -> {
            codec1.encode(buffer, getter1.apply(value));
            codec2.encode(buffer, getter2.apply(value));
            codec3.encode(buffer, getter3.apply(value));
            codec4.encode(buffer, getter4.apply(value));
        }, buffer -> constructor.apply(codec1.decode(buffer), codec2.decode(buffer),
            codec3.decode(buffer), codec4.decode(buffer)));
    }

    static <B, T1, T2, T3, T4, T5, V> PacketCodec<B, V> tuple(
            PacketCodec<? super B, T1> codec1, Function<V, T1> getter1,
            PacketCodec<? super B, T2> codec2, Function<V, T2> getter2,
            PacketCodec<? super B, T3> codec3, Function<V, T3> getter3,
            PacketCodec<? super B, T4> codec4, Function<V, T4> getter4,
            PacketCodec<? super B, T5> codec5, Function<V, T5> getter5,
            Function5<T1, T2, T3, T4, T5, V> constructor) {
        Objects.requireNonNull(constructor, "constructor");
        return ofLegacy((buffer, value) -> {
            codec1.encode(buffer, getter1.apply(value));
            codec2.encode(buffer, getter2.apply(value));
            codec3.encode(buffer, getter3.apply(value));
            codec4.encode(buffer, getter4.apply(value));
            codec5.encode(buffer, getter5.apply(value));
        }, buffer -> constructor.apply(codec1.decode(buffer), codec2.decode(buffer),
            codec3.decode(buffer), codec4.decode(buffer), codec5.decode(buffer)));
    }

    static <B, T1, T2, T3, T4, T5, T6, V> PacketCodec<B, V> tuple(
            PacketCodec<? super B, T1> codec1, Function<V, T1> getter1,
            PacketCodec<? super B, T2> codec2, Function<V, T2> getter2,
            PacketCodec<? super B, T3> codec3, Function<V, T3> getter3,
            PacketCodec<? super B, T4> codec4, Function<V, T4> getter4,
            PacketCodec<? super B, T5> codec5, Function<V, T5> getter5,
            PacketCodec<? super B, T6> codec6, Function<V, T6> getter6,
            Function6<T1, T2, T3, T4, T5, T6, V> constructor) {
        Objects.requireNonNull(constructor, "constructor");
        return ofLegacy((buffer, value) -> {
            codec1.encode(buffer, getter1.apply(value));
            codec2.encode(buffer, getter2.apply(value));
            codec3.encode(buffer, getter3.apply(value));
            codec4.encode(buffer, getter4.apply(value));
            codec5.encode(buffer, getter5.apply(value));
            codec6.encode(buffer, getter6.apply(value));
        }, buffer -> constructor.apply(codec1.decode(buffer), codec2.decode(buffer),
            codec3.decode(buffer), codec4.decode(buffer), codec5.decode(buffer), codec6.decode(buffer)));
    }

    static <B, T1, T2, T3, T4, T5, T6, T7, V> PacketCodec<B, V> tuple(
            PacketCodec<? super B, T1> codec1, Function<V, T1> getter1,
            PacketCodec<? super B, T2> codec2, Function<V, T2> getter2,
            PacketCodec<? super B, T3> codec3, Function<V, T3> getter3,
            PacketCodec<? super B, T4> codec4, Function<V, T4> getter4,
            PacketCodec<? super B, T5> codec5, Function<V, T5> getter5,
            PacketCodec<? super B, T6> codec6, Function<V, T6> getter6,
            PacketCodec<? super B, T7> codec7, Function<V, T7> getter7,
            Function7<T1, T2, T3, T4, T5, T6, T7, V> constructor) {
        Objects.requireNonNull(constructor, "constructor");
        return ofLegacy((buffer, value) -> {
            codec1.encode(buffer, getter1.apply(value));
            codec2.encode(buffer, getter2.apply(value));
            codec3.encode(buffer, getter3.apply(value));
            codec4.encode(buffer, getter4.apply(value));
            codec5.encode(buffer, getter5.apply(value));
            codec6.encode(buffer, getter6.apply(value));
            codec7.encode(buffer, getter7.apply(value));
        }, buffer -> constructor.apply(codec1.decode(buffer), codec2.decode(buffer),
            codec3.decode(buffer), codec4.decode(buffer), codec5.decode(buffer),
            codec6.decode(buffer), codec7.decode(buffer)));
    }

    static <B, T1, T2, T3, T4, T5, T6, T7, T8, V> PacketCodec<B, V> tuple(
            PacketCodec<? super B, T1> codec1, Function<V, T1> getter1,
            PacketCodec<? super B, T2> codec2, Function<V, T2> getter2,
            PacketCodec<? super B, T3> codec3, Function<V, T3> getter3,
            PacketCodec<? super B, T4> codec4, Function<V, T4> getter4,
            PacketCodec<? super B, T5> codec5, Function<V, T5> getter5,
            PacketCodec<? super B, T6> codec6, Function<V, T6> getter6,
            PacketCodec<? super B, T7> codec7, Function<V, T7> getter7,
            PacketCodec<? super B, T8> codec8, Function<V, T8> getter8,
            Function8<T1, T2, T3, T4, T5, T6, T7, T8, V> constructor) {
        Objects.requireNonNull(constructor, "constructor");
        return ofLegacy((buffer, value) -> {
            codec1.encode(buffer, getter1.apply(value));
            codec2.encode(buffer, getter2.apply(value));
            codec3.encode(buffer, getter3.apply(value));
            codec4.encode(buffer, getter4.apply(value));
            codec5.encode(buffer, getter5.apply(value));
            codec6.encode(buffer, getter6.apply(value));
            codec7.encode(buffer, getter7.apply(value));
            codec8.encode(buffer, getter8.apply(value));
        }, buffer -> constructor.apply(codec1.decode(buffer), codec2.decode(buffer),
            codec3.decode(buffer), codec4.decode(buffer), codec5.decode(buffer),
            codec6.decode(buffer), codec7.decode(buffer), codec8.decode(buffer)));
    }
}
