package cppfm.transformer_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Constant;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyConstant;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(BranchTarget.class)
public final class BranchMixin {
    @Inject(method = "compute(I)I", at = @At("HEAD"))
    private static void headHook(CallbackInfo ignored) {
        System.out.println("HEAD_ONCE");
    }

    @Inject(method = "compute(I)I", at = @At("RETURN"))
    private static void returnHook(CallbackInfoReturnable<Integer> info) {
        if (info.getReturnValueI() == 7) System.out.println("RETURN_ONCE");
    }

    @Redirect(method = "call(I)I", at = @At(value = "INVOKE",
        target = "cppfm/transformer_fixture/BranchTarget;helper(I)I"))
    private static int redirect(BranchTarget target, int value) {
        return target.helper(value) + 10;
    }

    @ModifyConstant(method = "constant()I", constant = @Constant(intValue = 3))
    private static int modifyConstant(int value) {
        return value + 4;
    }
}
