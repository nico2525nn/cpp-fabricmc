package net.minecraft.server.world;

import cppfm.bridge.NativeBridge;
import cppfm.bridge.WrapperCache;
import net.minecraft.entity.Entity;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.World;

public class ServerWorld extends World {
    private final MinecraftServer server;

    protected ServerWorld(long nativeHandle, MinecraftServer server) {
        super(nativeHandle, false);
        this.server = server;
    }
    public static ServerWorld of(long handle, MinecraftServer server) {
        return WrapperCache.get(ServerWorld.class, handle, h -> new ServerWorld(h, server));
    }
    @Override public MinecraftServer getServer() { return server; }
    public boolean spawnEntity(Entity entity) { return entity != null; }
    public boolean addEntity(Entity entity) { return spawnEntity(entity); }
    public long getTime() { return NativeBridge.currentTick(); }
    public List<ServerPlayerEntity> getPlayers() {
        List<ServerPlayerEntity> result = new ArrayList<>();
        if (server == null) return result;
        for (ServerPlayerEntity player : server.getPlayerManager().getPlayerList())
            if (player.getServerWorld() != null && player.getServerWorld().nativeHandle() == nativeHandle)
                result.add(player);
        return List.copyOf(result);
    }
}
