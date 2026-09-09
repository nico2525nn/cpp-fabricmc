package cppfm.transformer_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** Verifies that acronym accessor names are not lower-cased incorrectly. */
@Mixin(AcronymAccessorTarget.class)
public interface AcronymAccessorMixin {
    @Accessor int getROOT();
    @Accessor String getURL();
    @Accessor int getXValue();
}
