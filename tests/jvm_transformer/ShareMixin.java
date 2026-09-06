package cppfm.transformer_fixture;

import com.llamalad7.mixinextras.sugar.Share;
import com.llamalad7.mixinextras.sugar.ref.LocalIntRef;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
import org.spongepowered.asm.mixin.injection.Redirect;

/** Exercises MixinExtras @Share across ModifyVariable and Redirect. */
@Mixin(ShareTarget.class)
public final class ShareMixin {
    @ModifyVariable(method = "compute(I)I",
                    at = @At(value = "INVOKE",
                             target = "Lcppfm/transformer_fixture/ShareTarget;helper(I)I"))
    private static int capture(int value, @Share("shared") LocalIntRef shared) {
        shared.set(value + 10);
        return value;
    }

    @Redirect(method = "compute(I)I",
              at = @At(value = "INVOKE",
                       target = "Lcppfm/transformer_fixture/ShareTarget;helper(I)I"))
    private static int redirect(int value, @Share("shared") LocalIntRef shared) {
        return value + shared.get();
    }
}
