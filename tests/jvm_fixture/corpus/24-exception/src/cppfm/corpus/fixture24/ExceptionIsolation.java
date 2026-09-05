package cppfm.corpus.fixture24;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;

/** Corpus 24: one callback throws while a later callback and later tick survive. */
public final class ExceptionIsolation implements DedicatedServerModInitializer {
    private static void result(String phase, boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=24 status=" + (ok ? "PASS" : "FAIL") + " phase=" + phase);
    }

    @Override
    public void onInitializeServer() {
        ServerTickEvents.END.register(server -> {
            if (server.getTicks() == 1) {
                result("throwing-callback-entered", true);
                throw new IllegalStateException("corpus24 intentional callback failure");
            }
        });
        ServerTickEvents.END.register(server -> {
            if (server.getTicks() == 1) result("following-callback-recovered", true);
        });
    }
}
