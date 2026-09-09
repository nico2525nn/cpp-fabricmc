package com.mojang.serialization;

import java.util.Optional;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Supplier;

/**
 * Small binary-compatible subset of DataFixerUpper's DataResult.
 *
 * <p>The shadow API is intentionally dependency-free so the JVM ABI can be
 * compiled on a clean machine.  The methods used by Fabric/Minecraft text and
 * registry code are implemented; error/partial values remain observable
 * rather than being silently converted into a success.</p>
 */
public interface DataResult<R> {
    Optional<R> result();

    default Optional<Error<R>> error() { return Optional.empty(); }
    default Lifecycle lifecycle() { return Lifecycle.stable(); }
    default boolean hasResultOrPartial() { return result().isPresent(); }
    default Optional<R> resultOrPartial(Consumer<String> onError) {
        error().ifPresent(error -> { if (onError != null) onError.accept(error.message()); });
        return result();
    }
    default Optional<R> resultOrPartial() { return resultOrPartial(null); }
    default <E extends Throwable> R getOrThrow(Function<String, E> constructor) throws E {
        Optional<R> value = resultOrPartial();
        if (value.isPresent()) return value.get();
        throw constructor.apply(error().map(Error::message).orElse("DataResult contains no result"));
    }
    default <E extends Throwable> R getPartialOrThrow(Function<String, E> constructor) throws E {
        return getOrThrow(constructor);
    }
    default R getOrThrow() { return getOrThrow(IllegalStateException::new); }
    default R getPartialOrThrow() { return getPartialOrThrow(IllegalStateException::new); }

    default <T> DataResult<T> map(Function<? super R, ? extends T> mapper) {
        Optional<R> value = result();
        return value.isPresent() ? success(mapper.apply(value.get()))
            : error(() -> error().map(Error::message).orElse("DataResult contains no result"));
    }
    default <T> T mapOrElse(Function<? super R, ? extends T> success,
                             Function<? super Error<R>, ? extends T> failure) {
        Optional<R> value = result();
        if (value.isPresent()) return success.apply(value.get());
        Error<R> error = error().orElseGet(() -> new Error<>(
            () -> "DataResult contains no result", Optional.empty(), Lifecycle.stable()));
        return failure.apply(error);
    }
    default DataResult<R> ifSuccess(Consumer<? super R> consumer) {
        result().ifPresent(consumer);
        return this;
    }
    default DataResult<R> ifError(Consumer<? super Error<R>> consumer) {
        error().ifPresent(consumer);
        return this;
    }
    default DataResult<R> promotePartial(Consumer<String> onError) { return resultOrPartial(onError).isPresent() ? this : this; }
    default <T> DataResult<T> flatMap(Function<? super R, ? extends DataResult<T>> mapper) {
        Optional<R> value = result();
        if (value.isPresent()) return mapper.apply(value.get());
        return DataResult.error(() -> error().map(Error::message).orElse("DataResult contains no result"));
    }
    default DataResult<R> setPartial(Supplier<R> partial) { return this; }
    default DataResult<R> setPartial(R partial) { return this; }
    default DataResult<R> mapError(Function<String, String> mapper) { return this; }
    default DataResult<R> setLifecycle(Lifecycle value) { return this; }
    default boolean isSuccess() { return result().isPresent(); }
    default boolean isError() { return !isSuccess(); }

    static <R> DataResult<R> success(R value) { return new Success<>(value); }
    static <R> DataResult<R> success(R value, Lifecycle ignored) { return success(value); }
    static <R> DataResult<R> error(Supplier<String> message) {
        return new Error<>(message, Optional.empty(), Lifecycle.stable());
    }
    static <R> DataResult<R> error(Supplier<String> message, R partial) {
        return new Error<>(message, Optional.ofNullable(partial), Lifecycle.stable());
    }
    static <R> DataResult<R> error(Supplier<String> message, Lifecycle ignored) { return error(message); }
    static <R> DataResult<R> error(Supplier<String> message, R partial, Lifecycle ignored) {
        return error(message, partial);
    }

    record Error<R>(Supplier<String> messageSupplier, Optional<R> partialValue,
                    Lifecycle lifecycle) implements DataResult<R> {
        public String message() { return messageSupplier.get(); }
        @Override public Optional<R> result() { return Optional.empty(); }
        @Override public Optional<Error<R>> error() { return Optional.of(this); }
        @Override public boolean hasResultOrPartial() { return partialValue.isPresent(); }
        @Override public Optional<R> resultOrPartial(Consumer<String> onError) {
            if (onError != null) onError.accept(message());
            return partialValue;
        }
        @Override public Optional<R> resultOrPartial() { return partialValue; }
    }

    final class Success<R> implements DataResult<R> {
        private final R value;
        public Success(R value) { this.value = value; }
        @Override public Optional<R> result() { return Optional.ofNullable(value); }
    }

}
