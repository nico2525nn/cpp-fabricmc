package cppfm.transformer_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(SelectorTarget.class)
public abstract class SelectorArgumentMixin {
    @Inject(method = "run", at = @At("HEAD"))
    private static void mustNotSelectLaterCompatibleOverload(
        int value, CallbackInfo ignored) { }
}
