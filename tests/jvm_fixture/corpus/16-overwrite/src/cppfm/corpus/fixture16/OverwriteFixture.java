package cppfm.corpus.fixture16;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;

/** Corpus 16: verify the real getTickTime() overwrite site. */
public final class OverwriteFixture implements DedicatedServerModInitializer {
    @Override
    public void onInitializeServer() {
        ServerTickEvents.END.register(server -> {
            if (server.getTicks() == 1) {
                long value = server.getTickTime();
                NativeBridge.nativeLog(value == 42L ? "INFO" : "ERROR",
                    "CORPUS case=16 status=" + (value == 42L ? "PASS" : "FAIL")
                    + " phase=mixin-overwrite-value-" + value);
            }
        });
    }
}
