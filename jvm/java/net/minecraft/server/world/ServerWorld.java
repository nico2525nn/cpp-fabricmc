package net.minecraft.server.world;

import cppfm.bridge.WrapperCache;
import net.minecraft.entity.Entity;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Predicate;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.World;
import net.minecraft.util.NativeAccess;

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
    public boolean spawnEntity(Entity entity) {
        if (entity == null || entity.isRemoved()) return false;
        entity.setWorld(this); return true;
    }
    public boolean addEntity(Entity entity) { return spawnEntity(entity); }
    @Override public long getTime() { return NativeAccess.currentTick(); }
    public List<ServerPlayerEntity> getPlayers() {
        List<ServerPlayerEntity> result = new ArrayList<>();
        if (server == null) return result;
        for (ServerPlayerEntity player : server.getPlayerManager().getPlayerList())
            if (player.getServerWorld() != null && player.getServerWorld().nativeHandle() == nativeHandle)
                result.add(player);
        return List.copyOf(result);
    }
    public List<ServerPlayerEntity> getPlayers(Predicate<ServerPlayerEntity> predicate) {
        List<ServerPlayerEntity> result = new ArrayList<>();
        for (ServerPlayerEntity player : getPlayers()) if (predicate == null || predicate.test(player)) result.add(player);
        return List.copyOf(result);
    }
    @SuppressWarnings("unchecked")
    public <T extends Entity> List<T> getEntitiesByType(net.minecraft.entity.EntityType<T> type,
                                                         net.minecraft.util.math.Box box,
                                                         Predicate<? super T> predicate) {
        if (type == null) return List.of();
        List<T> result = new ArrayList<>();
        for (ServerPlayerEntity player : getPlayers()) {
            if (type.equals(player.getType()) && (box == null || box.contains(player.getPos())) &&
                (predicate == null || predicate.test((T) player))) result.add((T) player);
        }
        return List.copyOf(result);
    }
    public Entity getEntityById(int id) { for (ServerPlayerEntity player : getPlayers()) if (player.getId() == id) return player; return null; }
    @Override public boolean isChunkLoaded(int chunkX, int chunkZ) { return nativeHandle != 0; }
}
