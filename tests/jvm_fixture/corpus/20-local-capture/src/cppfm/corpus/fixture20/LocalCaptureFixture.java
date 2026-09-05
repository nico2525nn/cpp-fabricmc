package cppfm.corpus.fixture20;

import cppfm.bridge.NativeBridge;
import java.util.concurrent.atomic.AtomicBoolean;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.minecraft.util.math.BlockPos;

/** Corpus 20: TAIL must expose a real local value to the handler. */
public final class LocalCaptureFixture implements DedicatedServerModInitializer {
    private static final AtomicBoolean ASSERTED = new AtomicBoolean();

    public static void captured(boolean ok) {
        if (ASSERTED.compareAndSet(false, true)) {
            NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
                "CORPUS case=20 status=" + (ok ? "PASS" : "FAIL")
                + " phase=local-capture-handler");
        }
    }

    @Override
    public void onInitializeServer() {
        ServerLifecycleEvents.SERVER_STARTED.register(server -> {
            server.getOverworld().getBlockState(new BlockPos(13, -60, 13));
            // A missing transformer must become an explicit assertion failure,
            // rather than disappearing as a metadata-only load.
            captured(false);
        });
    }
}
