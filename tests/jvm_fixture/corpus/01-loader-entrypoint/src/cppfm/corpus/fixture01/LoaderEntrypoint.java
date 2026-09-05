package cppfm.corpus.fixture01;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;

/** Corpus 01: the server entrypoint is discovered and invoked once. */
public final class LoaderEntrypoint implements DedicatedServerModInitializer {
    @Override
    public void onInitializeServer() {
        NativeBridge.nativeLog("INFO", "CORPUS case=01 status=PASS phase=entrypoint");
    }
}
