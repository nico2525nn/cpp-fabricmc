package com.google.common.base;

/** Minimal Guava predicate ABI used by the compatibility shadows. */
@FunctionalInterface
public interface Predicate<T> {
    boolean apply(T input);
}
