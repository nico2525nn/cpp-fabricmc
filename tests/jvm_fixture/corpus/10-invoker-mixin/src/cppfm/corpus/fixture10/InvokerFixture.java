package cppfm.corpus.fixture10;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;

/** Corpus 10: the Invoker class is loadable and its ABI is inspectable. */
public final class InvokerFixture implements DedicatedServerModInitializer {
    @Override
    public void onInitializeServer() {
        ServerLifecycleEvents.SERVER_STARTED.register(server -> {
            boolean ok = server instanceof TickInvoker;
            if (ok) {
                int value = ((TickInvoker) server).cppfm$getTicks();
                ok = value >= 0;
            }
            NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
                "CORPUS case=10 status=" + (ok ? "PASS" : "FAIL")
                + " phase=invoker-runtime");
        });
    }
}
