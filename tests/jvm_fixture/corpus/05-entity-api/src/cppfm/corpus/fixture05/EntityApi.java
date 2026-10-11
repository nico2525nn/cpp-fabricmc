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
            boolean entityFactoryEmpty = noHandle == null;
            boolean playerFactoryEmpty = noPlayer == null;
            boolean worldPlayersEmpty = world.getPlayers().isEmpty();
            boolean serverPlayersEmpty = server.getPlayerManager().getPlayerList().isEmpty();
            boolean spawnNullRejected = !world.spawnEntity(null);
            boolean addNullRejected = !world.addEntity(null);

            result("entity-of-zero", entityFactoryEmpty);
            result("server-player-of-zero", playerFactoryEmpty);
            result("world-player-list-empty", worldPlayersEmpty);
            result("server-player-list-empty", serverPlayersEmpty);
            result("spawn-entity-null-rejected", spawnNullRejected);
            result("add-entity-null-rejected", addNullRejected);
            result("empty-entity-boundary", entityFactoryEmpty && playerFactoryEmpty
                && worldPlayersEmpty && serverPlayersEmpty && spawnNullRejected
                && addNullRejected);
        });
    }
}
