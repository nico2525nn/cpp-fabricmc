package cppfm.corpus.fixture23;

import cppfm.bridge.NativeBridge;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;

/** Corpus 23: a Java-created thread calls through JNI and is joined by the owner. */
public final class Threading implements DedicatedServerModInitializer {
    private static final AtomicBoolean SEEN = new AtomicBoolean();
    private static final AtomicBoolean QUEUED = new AtomicBoolean();
    private static final AtomicBoolean EXECUTED = new AtomicBoolean();
    private static final AtomicReference<Thread> EXECUTOR = new AtomicReference<>();

    private static void result(String phase, boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=23 status=" + (ok ? "PASS" : "FAIL") + " phase=" + phase);
    }

    @Override
    public void onInitializeServer() {
        ServerTickEvents.END.register(server -> {
            if (server.getTicks() == 2 && SEEN.get() &&
                    EXECUTED.get() && EXECUTOR.get() == server.getThread()) {
                result("attached-and-server-thread-executor", true);
                return;
            }
            if (server.getTicks() != 1 || !SEEN.compareAndSet(false, true)) return;
            long expected = server.getTicks();
            AtomicLong observed = new AtomicLong(Long.MIN_VALUE);
            Thread worker = new Thread(() -> observed.set(NativeBridge.nativeCurrentTick()),
                "cppfm-corpus-23");
            worker.start();
            try {
                worker.join(5000L);
                boolean attached = !worker.isAlive() && observed.get() == expected;
                result("attached-thread-" + worker.getName(), attached);
                Thread schedulerWorker = new Thread(() -> server.execute(() -> {
                    EXECUTOR.set(Thread.currentThread());
                    EXECUTED.set(true);
                }), "cppfm-corpus-23-scheduler");
                schedulerWorker.start();
                schedulerWorker.join(5000L);
                QUEUED.set(!schedulerWorker.isAlive());
                if (!attached || !QUEUED.get())
                    result("attached-or-queued-thread-failure", false);
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                result("attached-thread-interrupted", false);
            }
        });
    }
}
