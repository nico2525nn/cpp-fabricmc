package cppfm.corpus.fixture21;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;

/** Corpus 21: two mixin handlers share a target and are ordered by priority. */
public final class TransformOrderFixture implements DedicatedServerModInitializer {
    @Override
    public void onInitializeServer() {
        NativeBridge.nativeLog("INFO", "CORPUS case=21 status=PASS phase=metadata");
    }
}
