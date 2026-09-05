package cppfm.corpus.fixture11;

import cppfm.bridge.NativeBridge;
import java.util.concurrent.atomic.AtomicBoolean;
import net.fabricmc.api.DedicatedServerModInitializer;

/** Corpus 11: entrypoint paired with a real HEAD hook. */
public final class InjectHead implements DedicatedServerModInitializer {
    @Override
    public void onInitializeServer() {
        NativeBridge.nativeLog("INFO", "CORPUS case=11 status=PASS phase=metadata");
    }
}
