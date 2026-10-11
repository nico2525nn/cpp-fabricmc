package cppfm.transformer_fixture;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(SelectorTarget.class)
public abstract class SelectorMixin {
    @Inject(method = "run*", at = @At("HEAD"))
    private static void everyOverload(CallbackInfo ignored) {
        System.out.println("QUANTIFIER_HOOK");
    }

    @Inject(method = "run{1}", at = @At("HEAD"))
    private static void firstQuantifiedMatch(CallbackInfo ignored) {
        System.out.println("FIRST_MATCH_HOOK");
    }

    @Inject(method = "run", at = @At("HEAD"))
    private static void defaultSingleMatch(CallbackInfo ignored) {
        System.out.println("DEFAULT_MATCH_HOOK");
    }

    @Inject(method = "run{,}", at = @At("HEAD"))
    private static void unboundedRange(CallbackInfo ignored) {
        System.out.println("UNBOUNDED_RANGE_HOOK");
    }

    @Inject(method = "*(I)I", at = @At("HEAD"))
    private static void descriptorOnlyMatch(CallbackInfo ignored) {
        System.out.println("DESCRIPTOR_ONLY_HOOK");
    }

    @Inject(method = "cppfm.transformer_fixture.SelectorTarget.run{1}(I)I",
        at = @At("HEAD"))
    private static void dottedOwner(CallbackInfo ignored) {
        System.out.println("DOTTED_OWNER_HOOK");
    }

    @Redirect(method = "run", at = @At(value = "INVOKE",
        target = "Lcppfm/transformer_fixture/SelectorTarget;marker()I"))
    private static int redirectFirstOverloadOnly() {
        System.out.println("REDIRECT_HOOK");
        return 4;
    }
}
