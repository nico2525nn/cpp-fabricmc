package cppfm.transformer_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(SelectorStaticOnlyTarget.class)
public abstract class SelectorStaticOnlyMixin {
    @Inject(method = "run+", at = @At("HEAD"), require = 0)
    private void optionalStaticTarget(CallbackInfo ignored) { }

    @Inject(method = "absent", at = @At("HEAD"), require = 0)
    private void optionalMissingTarget(CallbackInfo ignored) { }

    @Inject(method = "run",
        at = @At(value = "INVOKE",
            target = "Lcppfm/transformer_fixture/SelectorStaticOnlyTarget;absent()V"))
    private static void targetControlledByConfigDefault(CallbackInfo ignored) { }

    @Redirect(method = "absentRedirect",
        at = @At(value = "INVOKE",
            target = "Lcppfm/transformer_fixture/SelectorStaticOnlyTarget;run()V"),
        require = 0)
    private static void optionalMissingRedirect() { }
}
