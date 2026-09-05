package cppfm.vendor.probe.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(targets = "net.minecraft.server.MinecraftServer")
public final class MinecraftServerMixin {
    @Inject(method = "getTicks", at = @At("RETURN"), require = 1)
    private static void cppfm$observeTicks(CallbackInfoReturnable<Integer> callback) {
        cppfm.vendor.probe.ProbeMod.reportMixinReturn(callback.getReturnValue());
    }
}
