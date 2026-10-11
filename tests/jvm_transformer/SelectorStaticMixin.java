package cppfm.transformer_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(SelectorStaticTarget.class)
public abstract class SelectorStaticMixin {
    @Inject(method = "run*", at = @At("HEAD"))
    private void onlySelectInstanceTargets(int value, CallbackInfo ignored) {
        System.out.println("INSTANCE_SELECTOR_HOOK");
    }
}
