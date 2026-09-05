package cppfm.transformer_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
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
    public static int namedSliceHits;
    public static int allInvokeHits;
    public static int assignObservedCalls;
    public static int headCancellationHits;
    public static int returnCancellationHits;
    public static int voidCancellationHits;
    public static long capturedWide;
    public static String capturedReference;

    @Shadow private int helperCalls;

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

    @Inject(method = "sliced(I)I", slice = {
        @Slice(id = "first", from = @At(value = "INVOKE",
            target = "cppfm/transformer_fixture/AdvancedTarget;helper(I)I", ordinal = 0),
            to = @At(value = "INVOKE",
                target = "cppfm/transformer_fixture/AdvancedTarget;helper(I)I", ordinal = 0)),
        @Slice(id = "last", from = @At(value = "INVOKE",
            target = "cppfm/transformer_fixture/AdvancedTarget;helper(I)I", ordinal = 2),
            to = @At(value = "INVOKE",
                target = "cppfm/transformer_fixture/AdvancedTarget;helper(I)I", ordinal = 2))
    }, at = @At(value = "INVOKE", slice = "last",
        target = "cppfm/transformer_fixture/AdvancedTarget;helper(I)I", ordinal = 0), require = 1)
    private static void namedSlice(CallbackInfo ignored) {
        namedSliceHits++;
    }

    @Inject(method = "sliced(I)I", at = @At(value = "INVOKE",
        target = "cppfm/transformer_fixture/AdvancedTarget;helper(I)I"), require = 3, allow = 3)
    private static void allInvokes(CallbackInfo ignored) {
        allInvokeHits++;
    }

    @Inject(method = "assign(I)I", at = @At(value = "INVOKE_ASSIGN",
        target = "cppfm/transformer_fixture/AdvancedTarget;helper(I)I"), require = 1)
    private void afterAssignment(CallbackInfo ignored) {
        assignObservedCalls = helperCalls;
    }

    @Inject(method = "cancelAtHead(I)I", at = @At("HEAD"), cancellable = true, require = 1)
    private static void cancelAtHead(int value, CallbackInfoReturnable<Integer> info) {
        headCancellationHits++;
        if (value == 5) info.setReturnValue(99);
    }

    @Inject(method = "cancelAtReturn(I)I", at = @At("RETURN"), cancellable = true, require = 1)
    private static void cancelAtReturn(CallbackInfoReturnable<Integer> info) {
        returnCancellationHits++;
        info.setReturnValue(info.getReturnValueI() + 100);
    }

    @Inject(method = "cancelVoid(I)V", at = @At("HEAD"), cancellable = true, require = 1)
    private static void cancelVoid(int value, CallbackInfo info) {
        voidCancellationHits++;
        if (value < 0) info.cancel();
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

    @Inject(method = "mixedCapture(I)I", at = @At("TAIL"),
        locals = LocalCapture.CAPTURE_FAILHARD, require = 1)
    private static void mixedCapture(int input, long widened, String label,
                                     CallbackInfoReturnable<Integer> info) {
        if (widened != input * 3L || !label.equals("xy"))
            throw new AssertionError("bad mixed locals: " + widened + ", " + label);
        capturedWide = widened;
        capturedReference = label;
    }

    @Inject(method = "control(I)I", at = @At("JUMP"))
    private static void onJump(CallbackInfo ignored) {
        jumpHits++;
    }
}
