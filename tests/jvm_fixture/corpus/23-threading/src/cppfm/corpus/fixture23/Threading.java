package cppfm.corpus.fixture23;

import cppfm.bridge.NativeBridge;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;

/** Corpus 23: a Java-created thread calls through JNI and is joined by the owner. */
public final class Threading implements DedicatedServerModInitializer {
    private static final AtomicBoolean SEEN = new AtomicBoolean();

    private static void result(String phase, boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=23 status=" + (ok ? "PASS" : "FAIL") + " phase=" + phase);
    }

    @Override
    public void onInitializeServer() {
        ServerTickEvents.END.register(server -> {
            if (server.getTicks() != 1 || !SEEN.compareAndSet(false, true)) return;
            long expected = server.getTicks();
            AtomicLong observed = new AtomicLong(Long.MIN_VALUE);
            Thread worker = new Thread(() -> observed.set(NativeBridge.nativeCurrentTick()),
                "cppfm-corpus-23");
            worker.start();
            try {
                worker.join(5000L);
                result("attached-thread-" + worker.getName(),
                    !worker.isAlive() && observed.get() == expected);
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                result("attached-thread-interrupted", false);
            }
        });
    }
}
