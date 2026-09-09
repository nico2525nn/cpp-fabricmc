package net.minecraft.util;

import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.function.Supplier;

/** Common utility methods used by the 1.21.4 server-side mod ABI. */
public final class Util {
    public static final UUID NIL_UUID = new UUID(0L, 0L);
    private Util() { }

    public static <T> T make(Supplier<T> factory) {
        return Objects.requireNonNull(factory, "factory").get();
    }

    public static <T> T make(T value, java.util.function.Consumer<T> consumer) {
        if (consumer != null) consumer.accept(value);
        return value;
    }

    /** Memoizes a function with the same null-safe, concurrent semantics used by common game tables. */
    public static <T, R> Function<T, R> memoize(Function<T, R> function) {
        Objects.requireNonNull(function, "function");
        Map<T, R> cache = new ConcurrentHashMap<>();
        return value -> cache.computeIfAbsent(value, function);
    }

    public static <T> Function<T, T> identity() { return Function.identity(); }

    public static <T> Predicate<T> and(Predicate<T> first, Predicate<T> second) {
        return value -> first != null && first.test(value) && second != null && second.test(value);
    }

    public static <T> Predicate<T> or(Predicate<T> first, Predicate<T> second) {
        return value -> (first != null && first.test(value)) || (second != null && second.test(value));
    }

    public static long getEpochTimeMs() { return System.currentTimeMillis(); }
    public static long getMeasuringTimeMs() { return System.currentTimeMillis(); }
    public static long getMeasuringTimeNano() { return System.nanoTime(); }
}
