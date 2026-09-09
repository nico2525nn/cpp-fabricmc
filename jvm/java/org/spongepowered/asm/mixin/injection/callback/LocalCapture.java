package org.spongepowered.asm.mixin.injection.callback;

public enum LocalCapture {
    NO_CAPTURE, PRINT, CAPTURE_FAILSOFT, CAPTURE_FAILHARD, CAPTURE_FAILEXCEPTION,
    /** Legacy spellings retained for mixins compiled against older Mixin APIs. */
    FAILSOFT, FAILHARD
}
