package cppfm.transformer_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Constant;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyConstant;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
import org.spongepowered.asm.mixin.injection.Slice;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import org.spongepowered.asm.mixin.injection.callback.LocalCapture;

@Mixin(AdvancedTarget.class)
public final class AdvancedMixin {
    public static int arrayAtHits;
    public static int sliceOrdinalHits;
    public static int constantHits;
    public static int variableHits;
    public static int capturedLocal;
    public static int jumpHits;

    @Inject(method = "sliced(I)I", at = {
        @At(value = "INVOKE", target = "cppfm/transformer_fixture/AdvancedTarget;helper(I)I", ordinal = 1),
        @At(value = "RETURN")
    })
    private static void arrayAt(CallbackInfo ignored) {
        arrayAtHits++;
    }

    @Inject(method = "sliced(I)I", slice = @Slice(
        from = @At(value = "INVOKE", target = "cppfm/transformer_fixture/AdvancedTarget;helper(I)I", ordinal = 0),
        to = @At(value = "INVOKE", target = "cppfm/transformer_fixture/AdvancedTarget;helper(I)I", ordinal = 2)
    ), at = @At(value = "INVOKE",
        target = "cppfm/transformer_fixture/AdvancedTarget;helper(I)I", ordinal = 1))
    private static void sliceOrdinal(CallbackInfo ignored) {
        sliceOrdinalHits++;
    }

    @ModifyConstant(method = "constants()I", constant = @Constant(intValue = 3, ordinal = 1))
    private static int modifySecondConstant(int value) {
        constantHits++;
        return value + 100;
    }

    @ModifyVariable(method = "modifyVariable(I)I", at = @At(value = "LOAD", ordinal = 0))
    private static int modifyFirstLoad(int value) {
        variableHits++;
        return value + 10;
    }

    @Inject(method = "capture(I)I", at = @At("TAIL"), locals = LocalCapture.CAPTURE_FAILHARD)
    private static void capture(int input, int local, CallbackInfoReturnable<Integer> info) {
        if (local != input * 2) throw new AssertionError("bad captured local: " + local);
        capturedLocal = local;
    }

    @Inject(method = "control(I)I", at = @At("JUMP"))
    private static void onJump(CallbackInfo ignored) {
        jumpHits++;
    }
}
