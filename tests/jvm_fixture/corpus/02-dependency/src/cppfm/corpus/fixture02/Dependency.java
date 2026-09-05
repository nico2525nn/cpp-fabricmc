package cppfm.corpus.fixture02;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;

/** Corpus 02: metadata dependency ordering is observed by the harness. */
public final class Dependency implements DedicatedServerModInitializer {
    @Override
    public void onInitializeServer() {
        NativeBridge.nativeLog("INFO", "CORPUS case=02 status=PASS phase=dependency");
    }
}
