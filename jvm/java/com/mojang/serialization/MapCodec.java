package com.mojang.serialization;

import java.util.Objects;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Supplier;

/**
 * Minimal class-shaped subset of DataFixerUpper's MapCodec.
 *
 * <p>This must remain a class, rather than an interface.  The real DFU
 * contract is an abstract class and Fabric's generated/custom codec classes
 * invoke its public no-argument constructor with {@code invokespecial}.
 * Keeping the class shape is therefore part of the runtime ABI, even when
 * the native server does not need to serialize a DFU map.</p>
 */
public abstract class MapCodec<T> implements MapEncoder<T>, MapDecoder<T> {
    public MapCodec() {}

    /** Shadow representation used by the dependency-free codec boundary. */
    public T decode(Object input) { return null; }

    /** Shadow representation used by the dependency-free codec boundary. */
    public Object encode(T value) { return value; }

    public <U> MapCodec<U> xmap(Function<? super T, ? extends U> to,
                                Function<? super U, ? extends T> from) {
        Objects.requireNonNull(to, "to");
        Objects.requireNonNull(from, "from");
        return new MapCodec<>() {
            @Override public U decode(Object input) {
                T value = MapCodec.this.decode(input);
                return value == null ? null : to.apply(value);
            }

            @Override public Object encode(U value) {
                return MapCodec.this.encode(from.apply(value));
            }
        };
    }

    /** Build the record-builder field used by Fabric's custom serializers. */
    public final <O> com.mojang.serialization.codecs.RecordCodecBuilder<O, T>
            forGetter(Function<O, T> getter) {
        Objects.requireNonNull(getter, "getter");
        return com.mojang.serialization.codecs.RecordCodecBuilder.of(getter, this);
    }

    public <U> MapCodec<U> flatXmap(
            Function<? super T, ? extends DataResult<? extends U>> to,
            Function<? super U, ? extends DataResult<? extends T>> from) {
        Objects.requireNonNull(to, "to");
        Objects.requireNonNull(from, "from");
        return xmap(value -> to.apply(value).result().orElse(null),
                value -> from.apply(value).result().orElse(null));
    }

    /** Convert this map view back to the ordinary shadow codec surface. */
    public Codec<T> codec() {
        return new Codec<>() {
            @Override public T decode(Object input) { return MapCodec.this.decode(input); }
            @Override public Object encode(T value) { return MapCodec.this.encode(value); }
        };
    }

    public MapCodec<T> fieldOf(String name) {
        Objects.requireNonNull(name, "name");
        return this;
    }

    public MapCodec<T> withLifecycle(Lifecycle ignored) { return this; }
    public MapCodec<T> stable() { return this; }
    public MapCodec<T> deprecated(int ignored) { return this; }
    public MapCodec<T> validate(Function<T, DataResult<T>> validator) {
        Objects.requireNonNull(validator, "validator");
        return this;
    }

    public MapCodec<T> mapResult(Object ignored) { return this; }
    public MapCodec<T> orElse(Consumer<String> ignored, T value) { return this; }
    public MapCodec<T> orElse(T value) { return this; }
    public MapCodec<T> orElseGet(Supplier<? extends T> value) { return this; }

    public static <T> MapCodec<T> unit(T value) {
        return new MapCodec<>() {
            @Override public T decode(Object input) { return value; }
        };
    }

    /** DFU 8.0.16 overload used by Fabric resource-condition codecs. */
    public static <T> MapCodec<T> unit(Supplier<? extends T> value) {
        Objects.requireNonNull(value, "value");
        return new MapCodec<>() {
            @Override public T decode(Object input) { return value.get(); }
        };
    }
}
