package com.llamalad7.mixinextras.sugar.ref;

/** Minimal runtime ABI for a shared reference captured by a MixinExtras hook. */
public interface LocalRef<T> {
    T get();
    void set(T value);
}
