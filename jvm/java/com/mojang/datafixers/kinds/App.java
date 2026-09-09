package com.mojang.datafixers.kinds;

/**
 * Minimal erased higher-kinded marker used by the 1.21.4 Either ABI.
 *
 * <p>The compatibility runtime does not execute DataFixer codecs, but
 * server-side Fabric APIs expose this type through method signatures.  Keep
 * the marker source-compatible without pulling DataFixerUpper into the
 * default Java shadow class path.</p>
 */
public interface App<F extends K1, A> { }
