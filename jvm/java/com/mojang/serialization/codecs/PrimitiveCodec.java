package com.mojang.serialization.codecs;

import com.mojang.serialization.Codec;

/**
 * Marker surface for DFU's primitive codecs.
 *
 * <p>Fabric uses the more specific field type when it links against
 * {@code Codec.STRING}, {@code Codec.INT}, and the other built-in codecs.  A
 * separate interface preserves that JVM descriptor while inheriting the
 * object-oriented shadow operations supplied by {@link Codec}.</p>
 */
public interface PrimitiveCodec<A> extends Codec<A> {
}
