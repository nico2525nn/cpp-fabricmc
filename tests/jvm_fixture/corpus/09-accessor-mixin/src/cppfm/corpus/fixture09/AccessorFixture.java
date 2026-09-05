package cppfm.corpus.fixture09;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;

/** Corpus 09: the Accessor class is loadable and its ABI is inspectable. */
public final class AccessorFixture implements DedicatedServerModInitializer {
    @Override
    public void onInitializeServer() {
        ServerLifecycleEvents.SERVER_STARTED.register(server -> {
            boolean ok = server instanceof TickAccessor;
            if (ok) {
                long value = ((TickAccessor) server).cppfm$getTick();
                ok = value >= 0L;
            }
            NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
                "CORPUS case=09 status=" + (ok ? "PASS" : "FAIL")
                + " phase=accessor-runtime");
        });
    }
}
