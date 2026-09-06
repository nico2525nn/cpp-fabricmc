package com.google.common.collect;

import com.google.common.base.Predicate;

/** Minimal Guava iterable helper surface used by collision mixins. */
public final class Iterables {
    private Iterables() { }
    public static <T> boolean any(Iterable<T> values, Predicate<? super T> predicate) {
        if (values == null || predicate == null) return false;
        for (T value : values) if (predicate.apply(value)) return true;
        return false;
    }
}
