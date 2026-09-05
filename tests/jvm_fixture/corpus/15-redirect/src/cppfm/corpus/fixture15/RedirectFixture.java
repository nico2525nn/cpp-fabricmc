package cppfm.corpus.fixture15;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.minecraft.registry.RegistryKey;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.Identifier;
import net.minecraft.world.World;

/** Corpus 15: Redirect metadata loads without claiming transport support. */
public final class RedirectFixture implements DedicatedServerModInitializer {
    private static boolean seen;

    private static void result(boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=15 status=" + (ok ? "PASS" : "FAIL")
            + " phase=mixin-redirect");
    }

    @Override
    public void onInitializeServer() {
        ServerLifecycleEvents.SERVER_STARTED.register(server -> {
            RegistryKey<World> key = new RegistryKey<>(Identifier.of("minecraft", "overworld"));
            ServerWorld world = server.getWorld(key);
            result(world != null && seen);
        });
    }

    public static ServerWorld redirect(long handle, net.minecraft.server.MinecraftServer server) {
        seen = true;
        NativeBridge.nativeLog("INFO", "CORPUS case=15 status=PASS phase=redirect-handler");
        return ServerWorld.of(handle, server);
    }
}
