package cppfm.transformer_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(AdvancedTarget.class)
public abstract class AllowSentinelMixin {
    @Inject(method = "sliced(I)I", at = @At(value = "INVOKE",
        target = "cppfm/transformer_fixture/AdvancedTarget;helper(I)I"),
        require = 0, allow = -1)
    private static void explicitlyNegativeAllow(CallbackInfo ignored) { }
}
