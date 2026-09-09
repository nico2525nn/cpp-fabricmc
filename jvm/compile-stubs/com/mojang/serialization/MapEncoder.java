package com.mojang.serialization;

/**
 * Compile-only marker for DFU's map encoder interface.
 *
 * <p>The real interface is supplied by DataFixerUpper at runtime.  Keeping
 * this marker out of the runtime classes directory prevents the lightweight
 * shadow from replacing the official implementation while still allowing
 * MapCodec to preserve its class hierarchy in a clean checkout.</p>
 */
public interface MapEncoder<A> { }
