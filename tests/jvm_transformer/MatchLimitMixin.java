package cppfm.transformer_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Deliberately violates require after a callback has been assembled. */
@Mixin(AdvancedTarget.class)
public final class MatchLimitMixin {
    @Inject(method = "cancelAtHead(I)I", at = @At("HEAD"), require = 2)
    private static void tooFew(CallbackInfo ignored) { }
}
