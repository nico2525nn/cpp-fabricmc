package cppfm.corpus.fixture13;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.minecraft.server.world.ServerWorld;

/** Corpus 13: an INVOKE target must execute through the transformed method. */
public final class InjectInvoke implements DedicatedServerModInitializer {
    private static boolean seen;

    private static void result(boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=13 status=" + (ok ? "PASS" : "FAIL")
            + " phase=mixin-invoke");
    }

    @Override
    public void onInitializeServer() {
        ServerLifecycleEvents.SERVER_STARTED.register(server -> {
            ServerWorld world = server.getOverworld();
            result(world != null && seen);
        });
    }

    public static void invoked() { seen = true; }
}
