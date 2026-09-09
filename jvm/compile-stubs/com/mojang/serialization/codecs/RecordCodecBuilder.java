package com.mojang.serialization.codecs;

import com.mojang.serialization.MapCodec;
import java.util.function.Function;

/**
 * Compile-only signature stub for DataFixerUpper's RecordCodecBuilder.
 *
 * <p>Only the return type of MapCodec.forGetter is needed by the shadow
 * classes.  The official implementation is loaded from the DFU runtime jar
 * when Fabric mods are probed.</p>
 */
public class RecordCodecBuilder<O, F> {
    public static <O, F> RecordCodecBuilder<O, F> of(
            Function<O, F> getter, MapCodec<F> codec) {
        return null;
    }
}
