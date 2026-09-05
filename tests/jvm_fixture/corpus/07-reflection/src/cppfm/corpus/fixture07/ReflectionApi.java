package cppfm.corpus.fixture07;

import cppfm.bridge.NativeBridge;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.minecraft.server.MinecraftServer;

/** Corpus 07: reflective discovery and invocation of the shadow ABI. */
public final class ReflectionApi implements DedicatedServerModInitializer {
    private static void result(String phase, boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=07 status=" + (ok ? "PASS" : "FAIL") + " phase=" + phase);
    }

    @Override
    public void onInitializeServer() {
        try {
            Method getTicks = MinecraftServer.class.getDeclaredMethod("getTicks");
            Method getTickTime = MinecraftServer.class.getDeclaredMethod("getTickTime");
            boolean shape = getTicks.getReturnType() == int.class
                && getTickTime.getReturnType() == long.class
                && Modifier.isPublic(getTicks.getModifiers());
            ServerLifecycleEvents.SERVER_STARTED.register(server -> {
                try {
                    Object ticks = getTicks.invoke(server);
                    Object tickTime = getTickTime.invoke(server);
                    result("invoke-getTicks-" + ticks + "-getTickTime-" + tickTime,
                        shape && ticks instanceof Integer && tickTime instanceof Long);
                } catch (Throwable failure) {
                    result("invoke-exception", false);
                }
            });
        } catch (ReflectiveOperationException failure) {
            result("method-discovery", false);
        }
    }
}
