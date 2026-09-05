package cppfm.corpus.fixture13;

import cppfm.bridge.NativeBridge;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;

/** The shell must report this point, not pretend to transform bytecode. */
@Mixin(targets = "net.minecraft.server.MinecraftServer")
public final class InvokeMixin {
    @Inject(method = "getOverworld()Lnet/minecraft/server/world/ServerWorld;",
            at = @At(value = "INVOKE", target = "Lnet/minecraft/server/world/ServerWorld;of(JLnet/minecraft/server/MinecraftServer;)Lnet/minecraft/server/world/ServerWorld;"))
    private static void atInvoke(CallbackInfoReturnable<ServerWorld> info) {
        InjectInvoke.invoked();
        NativeBridge.nativeLog("INFO", "CORPUS case=13 status=PASS phase=invoke-handler");
    }
}
