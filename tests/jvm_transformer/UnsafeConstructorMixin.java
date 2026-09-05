package cppfm.transformer_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ConstructorTarget.class)
public final class UnsafeConstructorMixin {
    @Inject(method = "<init>()V", at = @At("HEAD"))
    private static void before(CallbackInfo ignored) { }
}
