package cppfm.corpus.fixture11;

import cppfm.bridge.NativeBridge;
import java.util.concurrent.atomic.AtomicBoolean;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** The native tick setter exposes this HEAD site to the bounded shell. */
@Mixin(targets = "net.minecraft.server.MinecraftServer", priority = 1000)
public final class HeadMixin {
    private static final AtomicBoolean SEEN = new AtomicBoolean();

    @Inject(method = "setTick(J)V", at = @At("HEAD"))
    private static void onSetTick(long tick, CallbackInfo info) {
        if (tick == 1 && SEEN.compareAndSet(false, true))
            NativeBridge.nativeLog("INFO", "CORPUS case=11 status=PASS phase=mixin-head");
    }
}
