package net.minecraft.server.world;

import cppfm.bridge.WrapperCache;
import net.minecraft.entity.Entity;
import cppfm.bridge.CppModRuntime;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.function.BooleanSupplier;
import java.util.function.Predicate;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.World;
import net.minecraft.util.NativeAccess;
import java.util.concurrent.Executor;
import java.io.Writer;
import net.minecraft.network.packet.Packet;

public class ServerWorld extends World implements net.minecraft.world.ServerWorldAccess {
    private final MinecraftServer server;
    private final ServerEntityManager entityManager = new ServerEntityManager();

    protected ServerWorld(long nativeHandle, MinecraftServer server) {
        super(nativeHandle, false);
        this.server = server;
    }
    public ServerWorld(MinecraftServer server, Executor workerExecutor,
                       net.minecraft.world.level.storage.LevelStorage.Session session,
                       net.minecraft.world.level.ServerWorldProperties properties,
                       net.minecraft.registry.RegistryKey<net.minecraft.world.World> registryKey,
                       net.minecraft.world.dimension.DimensionOptions dimensionOptions,
                       net.minecraft.server.WorldGenerationProgressListener progressListener,
                       boolean debugWorld, long seed, List<?> spawners,
                       boolean shouldTickTime,
                       net.minecraft.util.math.random.RandomSequencesState randomSequencesState) {
        this(0L, server);
    }
    public static ServerWorld of(long handle, MinecraftServer server) {
        return handle == 0L
            ? WrapperCache.getAllowZero(ServerWorld.class, h -> new ServerWorld(h, server))
            : WrapperCache.get(ServerWorld.class, handle, h -> new ServerWorld(h, server));
    }
    @Override public MinecraftServer getServer() { return server; }
    /** Accessor target used by server-side entity instrumentation. */
    public ServerEntityManager getEntityManager() { return entityManager; }
    public boolean spawnEntity(Entity entity) {
        if (entity == null || entity.isRemoved()) return false;
        entity.setWorld(this);
        CppModRuntime.dispatchEntityLoad(entity, this);
        return true;
    }
    public boolean addEntity(Entity entity) { return spawnEntity(entity); }
    @Override public long getTime() { return nativeHandle == 0 ? NativeAccess.currentTick() : NativeAccess.worldTime(nativeHandle); }
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
        Set<Long> seen = new HashSet<>();
        Map<Long, ServerPlayerEntity> playersByHandle = new HashMap<>();
        for (ServerPlayerEntity player : getPlayers()) {
            seen.add(player.nativeHandle());
            playersByHandle.put(player.nativeHandle(), player);
            if (type.equals(player.getType()) && (box == null || box.contains(player.getPos())) &&
                    (predicate == null || predicate.test((T) player))) result.add((T) player);
        }
        int count = (int) Math.min(Integer.MAX_VALUE, NativeAccess.entityCount());
        for (int index = 0; index < count; index++) {
            long handle = NativeAccess.entityHandle(index);
            if (handle == 0L) continue;
            if (!seen.add(handle)) continue;
            Entity entity = playersByHandle.get(handle);
            if (entity == null) entity = Entity.of(handle);
            if (type.equals(entity.getType()) && (box == null || box.contains(entity.getPos()))
                    && (predicate == null || predicate.test((T) entity))) result.add((T) entity);
        }
        return List.copyOf(result);
    }
    public Entity getEntityById(int id) {
        for (ServerPlayerEntity player : getPlayers()) if (player.getId() == id) return player;
        int count = (int) Math.min(Integer.MAX_VALUE, NativeAccess.entityCount());
        for (int index = 0; index < count; index++) {
            long handle = NativeAccess.entityHandle(index);
            if (handle != 0L && Entity.of(handle).getId() == id) return Entity.of(handle);
        }
        return null;
    }
    public Iterable<Entity> iterateEntities() { return getEntities(); }
    @Override public boolean isChunkLoaded(int chunkX, int chunkZ) { return nativeHandle != 0; }
    public void tickChunk(net.minecraft.world.chunk.WorldChunk chunk, int randomTickSpeed) { }
    public void tick(BooleanSupplier shouldKeepTicking) {
        if (shouldKeepTicking != null && !shouldKeepTicking.getAsBoolean()) return;
        getWorldBorder().tick();
    }
    /** Per-entity tick boundary used by server scheduling and profiling mods. */
    public void tickEntity(Entity entity) { if (entity != null) entity.tick(); }
    /** Vanilla passenger tick boundary used by ServerCore's vehicle limiter. */
    public void tickPassenger(Entity entity, Entity passenger) {
        if (passenger != null) tickEntity(passenger);
    }
    /** World persistence entrypoint used by C2ME's save scheduling mixin. */
    public void save(net.minecraft.util.ProgressListener progressListener, boolean flush, boolean skipErrors) { }
    public void tick() { tick(() -> true); }
    /** Random block-tick entrypoint used by C2ME's scheduling mixin. */
    public void tickBlock(net.minecraft.util.math.BlockPos pos, net.minecraft.block.Block block) {
        if (pos != null && block != null)
            block.randomTick(getBlockState(pos), this, pos, new net.minecraft.util.math.random.Random(0L));
    }
    /** Fluid-tick entrypoint used by C2ME's scheduling mixin. */
    public void tickFluid(net.minecraft.util.math.BlockPos pos, net.minecraft.fluid.Fluid fluid) { }
    public void createExplosion(Entity entity, net.minecraft.entity.damage.DamageSource damageSource,
                                net.minecraft.world.explosion.ExplosionBehavior behavior,
                                double x, double y, double z, float power, boolean createFire,
                                net.minecraft.world.World.ExplosionSourceType sourceType,
                                net.minecraft.particle.ParticleEffect smallParticle,
                                net.minecraft.particle.ParticleEffect largeParticle,
                                net.minecraft.registry.entry.RegistryEntry<?> particle) { }
    public void unloadEntities(net.minecraft.world.chunk.WorldChunk chunk) { }
    public void updateListeners(net.minecraft.util.math.BlockPos pos,
                                net.minecraft.block.BlockState oldState,
                                net.minecraft.block.BlockState newState, int flags) { }
    public void dumpBlockEntities(Writer writer) { }
    /** Vanilla nearby-packet dispatch boundary used by chunk/network mods. */
    public boolean sendToPlayerIfNearby(ServerPlayerEntity player, boolean force,
                                        double x, double y, double z, Packet<?> packet) {
        return player != null && packet != null;
    }
}
