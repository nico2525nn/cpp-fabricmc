package net.fabricmc.fabric.api.event.lifecycle.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.world.chunk.WorldChunk;

public final class ServerChunkEvents {
    private ServerChunkEvents() { }
    @FunctionalInterface public interface Load { void onChunkLoad(ServerWorld world, WorldChunk chunk); }
    @FunctionalInterface public interface Generate { void onChunkGenerate(ServerWorld world, WorldChunk chunk); }
    @FunctionalInterface public interface Unload { void onChunkUnload(ServerWorld world, WorldChunk chunk); }
    public static final Event<Load> CHUNK_LOAD = EventFactory.createArrayBacked(
        Load.class, callbacks -> (world, chunk) -> {
            for (Load callback : callbacks) callback.onChunkLoad(world, chunk);
        });
    public static final Event<Generate> CHUNK_GENERATE = EventFactory.createArrayBacked(
        Generate.class, callbacks -> (world, chunk) -> {
            for (Generate callback : callbacks) callback.onChunkGenerate(world, chunk);
        });
    public static final Event<Unload> CHUNK_UNLOAD = EventFactory.createArrayBacked(
        Unload.class, callbacks -> (world, chunk) -> {
            for (Unload callback : callbacks) callback.onChunkUnload(world, chunk);
        });
    public static void clear() { CHUNK_LOAD.clear(); CHUNK_GENERATE.clear(); CHUNK_UNLOAD.clear(); }
}
