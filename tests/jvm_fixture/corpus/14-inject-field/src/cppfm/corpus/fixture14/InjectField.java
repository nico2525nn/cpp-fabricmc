package cppfm.corpus.fixture14;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;

/** Corpus 14: a FIELD target must execute through the transformed method. */
public final class InjectField implements DedicatedServerModInitializer {
    private static boolean seen;

    private static void result(boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=14 status=" + (ok ? "PASS" : "FAIL")
            + " phase=mixin-field");
    }

    @Override
    public void onInitializeServer() {
        ServerLifecycleEvents.SERVER_STARTED.register(server -> {
            int ticks = server.getTicks();
            result(ticks >= 0 && seen);
        });
    }

    public static void touched() { seen = true; }
}
