package cppfm.corpus.fixture15;

import cppfm.bridge.NativeBridge;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;

/** The class-file transformer must redirect this world factory call. */
@Mixin(targets = "net.minecraft.server.MinecraftServer")
public final class RedirectMixin {
    @Redirect(method = "getWorld(Lnet/minecraft/registry/RegistryKey;)Lnet/minecraft/server/world/ServerWorld;",
              at = @At(value = "INVOKE", target = "Lnet/minecraft/server/world/ServerWorld;of(JLnet/minecraft/server/MinecraftServer;)Lnet/minecraft/server/world/ServerWorld;"))
    private static ServerWorld redirectWorld(long handle, MinecraftServer server) {
        return RedirectFixture.redirect(handle, server);
    }
}
