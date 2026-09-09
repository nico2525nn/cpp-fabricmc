package com.mojang.serialization;

import com.mojang.datafixers.util.Either;
import com.mojang.serialization.codecs.PrimitiveCodec;
import java.nio.ByteBuffer;
import java.util.List;
import java.util.Objects;
import java.util.function.Function;
import java.util.stream.IntStream;
import java.util.stream.LongStream;

/**
 * Small dependency-free subset of DataFixerUpper's public Codec surface.
 *
 * <p>Fabric's recipe API composes codecs while registering custom
 * ingredients.  The native server does not use DFU to parse those payloads,
 * but the JVM ABI still needs the composition methods and their exact erased
 * descriptors so that the real Fabric classes can initialize.</p>
 */
public interface Codec<T> {
    PrimitiveCodec<Boolean> BOOL = primitive();
    PrimitiveCodec<Byte> BYTE = primitive();
    PrimitiveCodec<Short> SHORT = primitive();
    PrimitiveCodec<Integer> INT = primitive();
    PrimitiveCodec<Long> LONG = primitive();
    PrimitiveCodec<Float> FLOAT = primitive();
    PrimitiveCodec<Double> DOUBLE = primitive();
    PrimitiveCodec<String> STRING = primitive();
    PrimitiveCodec<ByteBuffer> BYTE_BUFFER = primitive();
    PrimitiveCodec<IntStream> INT_STREAM = primitive();
    PrimitiveCodec<LongStream> LONG_STREAM = primitive();

    default T decode(Object input) { return null; }
    default Object encode(T value) { return value; }

    private static <A> PrimitiveCodec<A> primitive() {
        return new PrimitiveCodec<>() { };
    }

    /** DataFixerUpper's value-mapping combinator. */
    default <U> Codec<U> xmap(Function<? super T, ? extends U> to,
                              Function<? super U, ? extends T> from) {
        Objects.requireNonNull(to, "to");
        Objects.requireNonNull(from, "from");
        return new Codec<>() {
            @Override public U decode(Object input) {
                T value = Codec.this.decode(input);
                return value == null ? null : to.apply(value);
            }
            @Override public Object encode(U value) {
                return Codec.this.encode(from.apply(value));
            }
        };
    }

    /** DataFixerUpper's fallible value-mapping combinator. */
    default <U> Codec<U> flatXmap(
            Function<? super T, ? extends DataResult<? extends U>> to,
            Function<? super U, ? extends DataResult<? extends T>> from) {
        Objects.requireNonNull(to, "to");
        Objects.requireNonNull(from, "from");
        return xmap(value -> to.apply(value).result().orElse(null),
            value -> from.apply(value).result().orElse(null));
    }

    /** DFU's one-way fallible mapping combinator used by resource conditions. */
    default <U> Codec<U> comapFlatMap(
            Function<? super T, ? extends DataResult<? extends U>> to,
            Function<? super U, ? extends T> from) {
        Objects.requireNonNull(to, "to");
        Objects.requireNonNull(from, "from");
        return flatXmap(to, value -> DataResult.success(from.apply(value)));
    }

    /** DFU's fallible encode-side mapping combinator. */
    default <U> Codec<U> flatComapMap(
            Function<? super T, ? extends U> to,
            Function<? super U, ? extends DataResult<? extends T>> from) {
        Objects.requireNonNull(to, "to");
        Objects.requireNonNull(from, "from");
        return flatXmap(value -> DataResult.success(to.apply(value)), from);
    }

    /** Codec for a homogeneous list. */
    default Codec<List<T>> listOf() {
        return new Codec<>() { };
    }

    /** Named map-field view used by Fabric's custom ingredient serializers. */
    default MapCodec<T> fieldOf(String name) {
        Objects.requireNonNull(name, "name");
        return new MapCodec<>() { };
    }

    /**
     * Dispatches to a codec selected by a discriminator.  The shadow runtime
     * has no DFU wire reader, so the base codec remains the safe fallback;
     * retaining this method is nevertheless important for Fabric's static
     * codec construction path.
     */
    default <K> Codec<T> dispatch(String typeKey,
                                  Function<? super T, ? extends K> type,
                                  Function<? super K, ? extends Codec<? extends T>> codec) {
        Objects.requireNonNull(typeKey, "typeKey");
        Objects.requireNonNull(type, "type");
        Objects.requireNonNull(codec, "codec");
        return this;
    }

    /** DataFixerUpper's sum codec used by Fabric custom ingredients. */
    static <L, R> Codec<Either<L, R>> either(Codec<L> left,
                                              Codec<R> right) {
        Objects.requireNonNull(left, "left");
        Objects.requireNonNull(right, "right");
        return new Codec<>() {
            @Override public Either<L, R> decode(Object input) { return null; }
            @Override public Object encode(Either<L, R> value) {
                if (value == null) return null;
                return value.map(left::encode, right::encode);
            }
        };
    }

    /**
     * DataFixerUpper's decode fallback combinator.
     *
     * <p>The shadow codec does not carry DFU's {@code DataResult} through its
     * simplified object-based decode surface, so the primary codec remains
     * authoritative here.  Keeping the overloads and the generic conversion
     * contract is still important: Fabric resource-condition codecs link
     * against both forms during static initialization.</p>
     */
    static <T> Codec<T> withAlternative(Codec<T> primary,
                                        Codec<? extends T> alternative) {
        Objects.requireNonNull(primary, "primary");
        Objects.requireNonNull(alternative, "alternative");
        return primary;
    }

    /** DataFixerUpper's decode fallback with a conversion for the alternative. */
    static <T, U> Codec<T> withAlternative(Codec<T> primary,
                                           Codec<U> alternative,
                                           Function<U, T> converter) {
        Objects.requireNonNull(primary, "primary");
        Objects.requireNonNull(alternative, "alternative");
        Objects.requireNonNull(converter, "converter");
        return primary;
    }
}
