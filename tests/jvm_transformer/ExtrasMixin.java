package cppfm.transformer_fixture;

import com.llamalad7.mixinextras.injector.ModifyExpressionValue;
import com.llamalad7.mixinextras.injector.ModifyReturnValue;
import com.llamalad7.mixinextras.injector.v2.WrapWithCondition;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

@Mixin(ExtrasTarget.class)
public final class ExtrasMixin {
    @ModifyReturnValue(method = "returnValue(I)I", at = @At("RETURN"))
    private static int modifyReturn(int value) {
        return value + 10;
    }

    @ModifyExpressionValue(method = "expression(I)I", at = @At(value = "INVOKE",
        target = "Lcppfm/transformer_fixture/ExtrasTarget;helper(I)I"))
    private static int modifyExpression(int value) {
        return value + 3;
    }

    @WrapWithCondition(method = "conditional(I)I", at = @At(value = "INVOKE",
        target = "Lcppfm/transformer_fixture/ExtrasTarget;helper(I)I"))
    private static boolean condition(ExtrasTarget target, int value) {
        return value >= 0;
    }

    @WrapOperation(method = "wrapped(I)I", at = @At(value = "INVOKE",
        target = "Lcppfm/transformer_fixture/ExtrasTarget;helper(I)I"))
    private static int operation(ExtrasTarget target, int value, Operation<Integer> original) {
        return original.call(target, value) + 5;
    }

    @com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod(method = "wrappedMethod(I)I")
    private static int method(int value, Operation<Integer> original) {
        return original.call(value) + 20;
    }
}
