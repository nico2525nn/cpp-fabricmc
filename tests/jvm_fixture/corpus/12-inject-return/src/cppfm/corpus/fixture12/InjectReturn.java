package cppfm.corpus.fixture12;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;

/** Corpus 12: entrypoint paired with a real RETURN hook. */
public final class InjectReturn implements DedicatedServerModInitializer {
    @Override
    public void onInitializeServer() {
        NativeBridge.nativeLog("INFO", "CORPUS case=12 status=PASS phase=metadata");
    }
}
