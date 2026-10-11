package cppfm.corpus.fixture18;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;

/** Corpus 18: modify a literal while preserving the reference value. */
public final class ModifyConstantFixture implements DedicatedServerModInitializer {
    private static boolean seen;

    private static void result(boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=18 status=" + (ok ? "PASS" : "FAIL")
            + " phase=modify-constant");
    }

    @Override
    public void onInitializeServer() {
        ServerLifecycleEvents.SERVER_STARTED.register(server -> {
            int bottom = server.getOverworld().getBottomY();
            int chunkHeight = server.getOverworld().getChunk(0, 0).getHeight();
            result(bottom == -64 && chunkHeight == 384 && seen);
        });
    }

    public static void modified() { seen = true; }
}
