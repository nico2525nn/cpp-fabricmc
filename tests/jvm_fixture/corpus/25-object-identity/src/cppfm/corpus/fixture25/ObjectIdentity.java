package cppfm.corpus.fixture25;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.registry.RegistryKey;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.Identifier;
import net.minecraft.world.World;

/** Corpus 25: repeated native handles map to stable Java wrapper identity. */
public final class ObjectIdentity implements DedicatedServerModInitializer {
    private static void result(String phase, boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=25 status=" + (ok ? "PASS" : "FAIL") + " phase=" + phase);
    }

    @Override
    public void onInitializeServer() {
        ServerLifecycleEvents.SERVER_STARTED.register(server -> {
            MinecraftServer byHandle = MinecraftServer.of(NativeBridge.nativeServerHandle());
            ServerWorld firstWorld = server.getOverworld();
            ServerWorld secondWorld = ServerWorld.of(firstWorld.nativeHandle(), server);
            RegistryKey<World> key = new RegistryKey<>(Identifier.of("minecraft", "overworld"));
            ServerWorld keyedWorld = server.getWorld(key);
            boolean ok = server.nativeHandle() != 0
                && byHandle == server
                && firstWorld == secondWorld
                && firstWorld.getServer() == server
                && keyedWorld == firstWorld
                && NativeBridge.nativeServerHandle() == server.nativeHandle();
            result("wrapper-identity", ok);
        });
        ServerTickEvents.END.register(server -> {
            if (server.getTicks() == 2)
                result("corpus-complete", true);
        });
    }
}
