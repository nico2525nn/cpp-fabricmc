package com.llamalad7.mixinextras.injector.wrapoperation;

/**
 * The erased MixinExtras operation ABI.  The actual operation arguments are
 * supplied as an Object array; primitive values are boxed by the compiler.
 */
@FunctionalInterface
public interface Operation<T> {
    T call(Object... args);
}
