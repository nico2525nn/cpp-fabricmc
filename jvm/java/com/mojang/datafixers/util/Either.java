package com.mojang.datafixers.util;

import com.mojang.datafixers.kinds.App;
import com.mojang.datafixers.kinds.K1;
import java.util.NoSuchElementException;
import java.util.Objects;
import java.util.Optional;
import java.util.function.Consumer;
import java.util.function.Function;

/**
 * Small dependency-free implementation of DataFixerUpper's public Either
 * surface.  It is present only because the server ABI uses Either in
 * PlayerEntity.trySleep; the full DataFixerUpper library remains optional.
 */
public abstract class Either<L, R> implements App<Either.Mu<R>, L> {
    public static final class Mu<R> implements K1 { }

    public abstract <C, D> Either<C, D> mapBoth(
        Function<? super L, ? extends C> left,
        Function<? super R, ? extends D> right);

    public abstract <T> T map(Function<? super L, ? extends T> left,
                               Function<? super R, ? extends T> right);

    public abstract Either<L, R> ifLeft(Consumer<? super L> consumer);
    public abstract Either<L, R> ifRight(Consumer<? super R> consumer);
    public abstract Optional<L> left();
    public abstract Optional<R> right();

    public <T> Either<T, R> mapLeft(Function<? super L, ? extends T> function) {
        return mapBoth(function, Function.identity());
    }

    public <T> Either<L, T> mapRight(Function<? super R, ? extends T> function) {
        return mapBoth(Function.identity(), function);
    }

    public static <L, R> Either<L, R> left(L value) {
        return new Left<>(value);
    }

    public static <L, R> Either<L, R> right(R value) {
        return new Right<>(value);
    }

    public L orThrow() {
        return left().orElseThrow(() -> new NoSuchElementException("Either has no left value"));
    }

    public Either<R, L> swap() {
        if (left().isPresent()) return Either.right(left().get());
        return Either.left(right().orElse(null));
    }

    public <L2> Either<L2, R> flatMap(Function<L, Either<L2, R>> function) {
        return left().isPresent() ? Objects.requireNonNull(function.apply(left().get()))
            : Either.right(right().orElse(null));
    }

    public static <U> U unwrap(Either<? extends U, ? extends U> value) {
        return value.map(Function.identity(), Function.identity());
    }

    private static final class Left<L, R> extends Either<L, R> {
        private final L value;
        private Left(L value) { this.value = value; }
        @Override public <C, D> Either<C, D> mapBoth(
                Function<? super L, ? extends C> left,
                Function<? super R, ? extends D> right) {
            return Either.left(left.apply(value));
        }
        @Override public <T> T map(Function<? super L, ? extends T> left,
                                    Function<? super R, ? extends T> right) {
            return left.apply(value);
        }
        @Override public Either<L, R> ifLeft(Consumer<? super L> consumer) {
            consumer.accept(value); return this;
        }
        @Override public Either<L, R> ifRight(Consumer<? super R> consumer) { return this; }
        @Override public Optional<L> left() { return Optional.ofNullable(value); }
        @Override public Optional<R> right() { return Optional.empty(); }
    }

    private static final class Right<L, R> extends Either<L, R> {
        private final R value;
        private Right(R value) { this.value = value; }
        @Override public <C, D> Either<C, D> mapBoth(
                Function<? super L, ? extends C> left,
                Function<? super R, ? extends D> right) {
            return Either.right(right.apply(value));
        }
        @Override public <T> T map(Function<? super L, ? extends T> left,
                                    Function<? super R, ? extends T> right) {
            return right.apply(value);
        }
        @Override public Either<L, R> ifLeft(Consumer<? super L> consumer) { return this; }
        @Override public Either<L, R> ifRight(Consumer<? super R> consumer) {
            consumer.accept(value); return this;
        }
        @Override public Optional<L> left() { return Optional.empty(); }
        @Override public Optional<R> right() { return Optional.ofNullable(value); }
    }
}
