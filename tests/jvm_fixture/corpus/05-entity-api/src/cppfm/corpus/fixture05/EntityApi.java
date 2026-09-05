package cppfm.corpus.fixture05;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.minecraft.entity.Entity;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;

/** Corpus 05: exercise the safe empty/live-list entity boundary without a client. */
public final class EntityApi implements DedicatedServerModInitializer {
    private static void result(String phase, boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=05 status=" + (ok ? "PASS" : "FAIL") + " phase=" + phase);
    }

    @Override
    public void onInitializeServer() {
        ServerLifecycleEvents.SERVER_STARTED.register(server -> {
            ServerWorld world = server.getOverworld();
            Entity noHandle = Entity.of(0);
            ServerPlayerEntity noPlayer = ServerPlayerEntity.of(0);
            boolean ok = noHandle == null && noPlayer == null
                && world.getPlayers().isEmpty()
                && server.getPlayerManager().getPlayerList().isEmpty()
                && !world.spawnEntity(null) && !world.addEntity(null);
            result("empty-entity-boundary", ok);
        });
    }
}
