package cppfm.corpus.fixture08;

import cppfm.bridge.NativeBridge;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import net.fabricmc.api.DedicatedServerModInitializer;

/** Corpus 08: metadata/resource visibility; applying access is a loader task. */
public final class AccessWidener implements DedicatedServerModInitializer {
    @Override
    public void onInitializeServer() {
        boolean ok = false;
        try (InputStream input = AccessWidener.class.getResourceAsStream(
                "/fixture08.accesswidener")) {
            if (input != null) {
                String text = new String(input.readAllBytes(), StandardCharsets.UTF_8);
                ok = text.startsWith("accessWidener v2 named")
                    && text.contains("net/minecraft/server/MinecraftServer");
            }
        } catch (Exception ignored) {
            ok = false;
        }
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=08 status=" + (ok ? "PASS" : "FAIL")
            + " phase=metadata-resource");
    }
}
