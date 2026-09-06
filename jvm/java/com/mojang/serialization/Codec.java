package com.mojang.serialization;

/** Minimal generic codec marker for the server storage ABI. */
public interface Codec<T> {
    default T decode(Object input) { return null; }
    default Object encode(T value) { return value; }
}
