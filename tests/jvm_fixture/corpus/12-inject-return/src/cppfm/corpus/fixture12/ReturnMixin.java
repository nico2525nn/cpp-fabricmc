package cppfm.corpus.fixture12;

import cppfm.bridge.NativeBridge;
import java.util.concurrent.atomic.AtomicBoolean;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** The native tick getter exposes this RETURN site to the bounded shell. */
@Mixin(targets = "net.minecraft.server.MinecraftServer", priority = 1000)
public final class ReturnMixin {
    private static final AtomicBoolean SEEN = new AtomicBoolean();

    @Inject(method = "getTicks()I", at = @At("RETURN"), cancellable = true)
    private static void onGetTicks(CallbackInfoReturnable<Integer> info) {
        if (info.getReturnValue() != null && info.getReturnValue() == 1
            && SEEN.compareAndSet(false, true))
            NativeBridge.nativeLog("INFO", "CORPUS case=12 status=PASS phase=mixin-return");
    }
}
