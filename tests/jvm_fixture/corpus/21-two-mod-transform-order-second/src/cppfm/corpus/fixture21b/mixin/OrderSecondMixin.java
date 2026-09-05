package cppfm.corpus.fixture21b.mixin;

import cppfm.bridge.NativeBridge;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** This dependent mod must run after the first mod in loader order. */
@Mixin(targets = "net.minecraft.server.MinecraftServer", priority = 1100)
public final class OrderSecondMixin {
    @Inject(method = "setTick(J)V", at = @At("HEAD"))
    private static void second(long tick, CallbackInfo info) {
        if (tick == 1)
            NativeBridge.nativeLog("INFO", "CORPUS case=21 status=PASS phase=order-second");
    }
}
