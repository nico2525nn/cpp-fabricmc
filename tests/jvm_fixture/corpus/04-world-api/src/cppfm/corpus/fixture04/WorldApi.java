package cppfm.corpus.fixture04;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.minecraft.block.BlockState;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;

/** Corpus 04: read/write a native world state through the shadow API. */
public final class WorldApi implements DedicatedServerModInitializer {
    private static void result(String phase, boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=04 status=" + (ok ? "PASS" : "FAIL") + " phase=" + phase);
    }

    @Override
    public void onInitializeServer() {
        ServerLifecycleEvents.SERVER_STARTED.register(server -> {
            ServerWorld world = server.getOverworld();
            BlockPos pos = new BlockPos(11, -60, 11);
            int before = world.getBlockState(pos).getRawId();
            BlockState desired = new BlockState(1234);
            boolean wrote = world.setBlockState(pos, desired, 3);
            int after = world.getBlockState(pos).getRawId();
            String dimension = world.getRegistryKey().getValue().toString();
            boolean ok = wrote && after == desired.getRawId()
                && "minecraft:overworld".equals(dimension);
            result("world-state-before-" + before + "-after-" + after, ok);
        });
    }
}
