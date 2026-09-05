package net.minecraft.world;

import cppfm.bridge.NativeBridge;
import cppfm.bridge.MixinHooks;
import cppfm.bridge.WrapperCache;
import net.minecraft.block.BlockState;
import net.minecraft.block.Blocks;
import net.minecraft.block.entity.BlockEntity;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.NativeAccess;
import net.minecraft.registry.RegistryKey;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.ChunkPos;
import net.minecraft.util.math.Vec3d;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.function.Predicate;

public class World implements BlockView, WorldView, WorldAccess {
    protected final long nativeHandle;
    protected final boolean client;
    private final Map<Long, BlockState> localBlocks = new HashMap<>();
    private final Map<Long, BlockEntity> localBlockEntities = new HashMap<>();
    private final net.minecraft.world.border.WorldBorder border = new net.minecraft.world.border.WorldBorder();
    private final Random random = new Random(0L);
    private final GameRules gameRules = new GameRules();
    private final net.minecraft.world.dimension.DimensionType dimensionType;

    protected World(long nativeHandle, boolean client) {
        this.nativeHandle = nativeHandle;
        this.client = client;
        this.dimensionType = net.minecraft.world.dimension.DimensionType.OVERWORLD;
    }
    public static World of(long handle) {
        return handle == 0L
            ? WrapperCache.getAllowZero(World.class, h -> new World(h, false))
            : WrapperCache.get(World.class, handle, h -> new World(h, false));
    }
    public long nativeHandle() { return nativeHandle; }
    public boolean isClient() { return client; }
    public BlockState getBlockState(BlockPos pos) {
        if (pos == null) return new BlockState(0);
        BlockState overwritten = MixinHooks.invokeOverwrite(this, "getBlockState", pos);
        if (overwritten != null) return overwritten;
        CallbackInfoReturnable<BlockState> head = MixinHooks.invokeHeadReturn(
            this, "getBlockState", null, pos);
        if (head.isCancelled()) return head.getReturnValue();
        BlockState nativeState = nativeHandle == 0 ? localBlocks.getOrDefault(pos.asLong(), Blocks.AIR.getDefaultState())
            : new BlockState(NativeAccess.worldBlock(nativeHandle, pos.getX(), pos.getY(), pos.getZ()));
        CallbackInfoReturnable<BlockState> tail = MixinHooks.invokeTailReturn(
            this, "getBlockState", nativeState, pos);
        if (tail.isCancelled()) return tail.getReturnValue();
        CallbackInfoReturnable<BlockState> returned = MixinHooks.invokeReturn(
            this, "getBlockState", tail.getReturnValue(), pos);
        return returned.getReturnValue();
    }
    public boolean setBlockState(BlockPos pos, BlockState state) { return setBlockState(pos, state, 3); }
    public boolean setBlockState(BlockPos pos, BlockState state, int flags) {
        if (pos == null || state == null) return false;
        Boolean overwritten = MixinHooks.invokeOverwrite(this, "setBlockState", pos, state, flags);
        if (overwritten != null) return overwritten;
        CallbackInfoReturnable<Boolean> head = MixinHooks.invokeHeadReturn(
            this, "setBlockState", false, pos, state, flags);
        if (head.isCancelled()) return Boolean.TRUE.equals(head.getReturnValue());
        boolean changed = nativeHandle == 0 ? localBlocks.put(pos.asLong(), state) != state
            : NativeAccess.setWorldBlock(nativeHandle, pos.getX(), pos.getY(), pos.getZ(), state.getRawState());
        CallbackInfoReturnable<Boolean> tail = MixinHooks.invokeTailReturn(
            this, "setBlockState", changed, pos, state, flags);
        if (tail.isCancelled()) return Boolean.TRUE.equals(tail.getReturnValue());
        CallbackInfoReturnable<Boolean> returned = MixinHooks.invokeReturn(
            this, "setBlockState", tail.getReturnValue(), pos, state, flags);
        return Boolean.TRUE.equals(returned.getReturnValue());
    }
    @SuppressWarnings("unchecked")
    public RegistryKey<World> getRegistryKey() {
        String name = NativeAccess.worldName(nativeHandle);
        return new RegistryKey<>(Identifier.tryParse(name == null || name.isEmpty() ? "minecraft:overworld" : name));
    }
    public long getTime() { return nativeHandle == 0 ? NativeAccess.currentTick() : NativeAccess.worldTime(nativeHandle); }
    public long getTimeOfDay() { return getTime(); }
    /** 1.21.4 overworld floor; kept literal so constant mixins can target it. */
    public int getBottomY() { return -64; }
    public int getTopY() { return dimensionType.minY() + dimensionType.height(); }
    public boolean isChunkLoaded(int chunkX, int chunkZ) { return nativeHandle != 0 || !localBlocks.isEmpty(); }
    public boolean isInBuildLimit(BlockPos pos) { return pos != null && pos.getY() >= getBottomY() && pos.getY() < getTopY(); }
    public boolean isAir(BlockPos pos) { return getBlockState(pos).isAir(); }
    public boolean breakBlock(BlockPos pos, boolean drop) { if (isAir(pos)) return false; return setBlockState(pos, Blocks.AIR.getDefaultState()); }
    public boolean removeBlock(BlockPos pos, boolean move) { return breakBlock(pos, move); }
    @Override public BlockEntity getBlockEntity(BlockPos pos) {
        return pos == null ? null : localBlockEntities.get(pos.asLong());
    }
    public void setBlockEntity(BlockPos pos, BlockEntity blockEntity) {
        if (pos == null) return;
        if (blockEntity == null) localBlockEntities.remove(pos.asLong());
        else localBlockEntities.put(pos.asLong(), blockEntity);
    }
    public List<Entity> getEntities() { return getEntitiesByClass(Entity.class, new Box(-3.0E7, getBottomY(), -3.0E7, 3.0E7, getTopY(), 3.0E7), entity -> true); }
    public <T extends Entity> List<T> getEntitiesByClass(Class<T> type, Box box, Predicate<? super T> predicate) {
        List<T> result = new ArrayList<>();
        if (type == null || predicate == null) return result;
        MinecraftServer server = getServer();
        if (server != null) for (ServerPlayerEntity player : server.getPlayerManager().getPlayerList()) {
            if (type.isInstance(player) && (box == null || box.contains(player.getPos())) && predicate.test(type.cast(player))) result.add(type.cast(player));
        }
        int count = (int) Math.min(Integer.MAX_VALUE, NativeAccess.entityCount());
        for (int index = 0; index < count; index++) {
            long handle = NativeAccess.entityHandle(index);
            if (handle == 0L) continue;
            Entity entity = findPlayer(handle, server);
            if (entity == null) entity = Entity.of(handle);
            if (type.isInstance(entity) && (box == null || box.contains(entity.getPos())) && predicate.test(type.cast(entity)))
                if (!result.contains(entity)) result.add(type.cast(entity));
        }
        return result;
    }
    public List<ServerPlayerEntity> getPlayers() { return getPlayers(player -> true); }
    public List<ServerPlayerEntity> getPlayers(Predicate<ServerPlayerEntity> predicate) {
        List<ServerPlayerEntity> result = new ArrayList<>(); MinecraftServer server = getServer();
        if (server == null || predicate == null) return result;
        for (ServerPlayerEntity player : server.getPlayerManager().getPlayerList()) if (predicate.test(player)) result.add(player);
        return result;
    }
    public Random getRandom() { return random; }
    public net.minecraft.world.border.WorldBorder getWorldBorder() { return border; }
    public GameRules getGameRules() { return gameRules; }
    public net.minecraft.world.dimension.DimensionType getDimension() { return dimensionType; }
    public ChunkPos getChunkPos(BlockPos pos) { return new ChunkPos(pos == null ? new BlockPos(0, 0, 0) : pos); }
    public net.minecraft.server.MinecraftServer getServer() {
        return nativeHandle == 0 ? null : MinecraftServer.of(NativeAccess.serverHandle());
    }

    private static Entity findPlayer(long handle, MinecraftServer server) {
        if (server == null) return null;
        for (ServerPlayerEntity player : server.getPlayerManager().getPlayerList())
            if (player.nativeHandle() == handle) return player;
        return null;
    }
}
