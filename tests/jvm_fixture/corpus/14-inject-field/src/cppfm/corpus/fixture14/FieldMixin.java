package cppfm.corpus.fixture14;

import cppfm.bridge.NativeBridge;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** The shell must report this point, not pretend to transform bytecode. */
@Mixin(targets = "net.minecraft.server.MinecraftServer")
public final class FieldMixin {
    @Inject(method = "getTicks()I", at = @At(value = "FIELD", target = "Lnet/minecraft/server/MinecraftServer;tick:J"))
    private static void atField(CallbackInfoReturnable<Integer> info) {
        InjectField.touched();
        NativeBridge.nativeLog("INFO", "CORPUS case=14 status=PASS phase=field-handler");
    }
}
