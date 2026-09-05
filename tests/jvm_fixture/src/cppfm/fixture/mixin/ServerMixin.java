package cppfm.fixture.mixin;

import cppfm.bridge.NativeBridge;
import net.minecraft.server.MinecraftServer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Overwrite;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Verifies the bounded server-side HEAD/TAIL/RETURN/Overwrite execution shell. */
@Mixin(MinecraftServer.class)
public final class ServerMixin {
    @Inject(method = "setTick", at = @At("HEAD"))
    private static void cppfm$head(long tick, CallbackInfo info) {
        if (tick == 1) NativeBridge.nativeLog("INFO", "fixture MIXIN_HEAD");
    }

    @Inject(method = "setTick", at = @At("TAIL"))
    private static void cppfm$tail(long tick, CallbackInfo info) {
        if (tick == 1) NativeBridge.nativeLog("INFO", "fixture MIXIN_TAIL");
    }

    @Inject(method = "getTicks", at = @At("RETURN"), cancellable = true)
    private static void cppfm$return(CallbackInfoReturnable<Integer> info) {
        if (info.getReturnValue() == 1)
            NativeBridge.nativeLog("INFO", "fixture MIXIN_RETURN");
    }

    @Overwrite
    public long getTickTime() {
        return 42L;
    }
}
