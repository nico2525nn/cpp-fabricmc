package cppfm.transformer_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(SelectorTarget.class)
public abstract class SelectorLimitMixin {
    @Inject(method = "run{3}", at = @At("HEAD"))
    private static void expectsThreeMatches(CallbackInfo ignored) { }
}
