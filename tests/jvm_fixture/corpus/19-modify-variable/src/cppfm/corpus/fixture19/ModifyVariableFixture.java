package cppfm.corpus.fixture19;

import cppfm.bridge.NativeBridge;
import java.util.concurrent.atomic.AtomicBoolean;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;

/** Corpus 19: modify the setter argument without changing its value. */
public final class ModifyVariableFixture implements DedicatedServerModInitializer {
    private static final AtomicBoolean SEEN = new AtomicBoolean();

    public static void modified() { SEEN.set(true); }

    @Override
    public void onInitializeServer() {
        ServerTickEvents.END.register(server -> {
            if (server.getTicks() != 1) return;
            boolean ok = SEEN.get();
            NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
                "CORPUS case=19 status=" + (ok ? "PASS" : "FAIL")
                + " phase=modify-variable");
        });
    }
}
