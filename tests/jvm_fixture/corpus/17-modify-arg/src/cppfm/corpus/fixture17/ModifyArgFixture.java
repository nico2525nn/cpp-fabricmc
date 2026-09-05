package cppfm.corpus.fixture17;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;

/** Corpus 17: modify an invocation argument while preserving its value. */
public final class ModifyArgFixture implements DedicatedServerModInitializer {
    private static boolean seen;

    private static void result(boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=17 status=" + (ok ? "PASS" : "FAIL")
            + " phase=modify-arg");
    }

    @Override
    public void onInitializeServer() {
        ServerLifecycleEvents.SERVER_STARTED.register(server -> {
            ServerWorld world = server.getOverworld();
            world.getBlockState(new BlockPos(12, -60, 12));
            result(seen);
        });
    }

    public static void modified() { seen = true; }
}
