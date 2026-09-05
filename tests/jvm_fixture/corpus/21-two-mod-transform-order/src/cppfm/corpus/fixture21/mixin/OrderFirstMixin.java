package cppfm.corpus.fixture21.mixin;

import cppfm.bridge.NativeBridge;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Lower Mixin priority applies first in both the shell and transformer. */
@Mixin(targets = "net.minecraft.server.MinecraftServer", priority = 900)
public final class OrderFirstMixin {
    @Inject(method = "setTick(J)V", at = @At("HEAD"))
    private static void first(long tick, CallbackInfo info) {
        if (tick == 1)
            NativeBridge.nativeLog("INFO", "CORPUS case=21 status=PASS phase=order-first");
    }
}
