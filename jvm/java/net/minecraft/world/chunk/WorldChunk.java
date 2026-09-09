package net.minecraft.world.chunk;

import java.util.Collections;
import java.util.Map;
import com.google.common.collect.Maps;
import net.minecraft.block.entity.BlockEntity;
import net.minecraft.block.BlockState;
import net.minecraft.block.Blocks;
import net.minecraft.fluid.FluidState;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.ChunkPos;
import net.minecraft.world.World;
import it.unimi.dsi.fastutil.ints.Int2ObjectMap;
import it.unimi.dsi.fastutil.ints.Int2ObjectOpenHashMap;
import net.minecraft.world.event.listener.GameEventDispatcher;

/**
 * Native-backed 1.21.4 world-chunk ABI.
 *
 * <p>The C++ world owns chunk storage.  This class intentionally keeps the
 * common WorldChunk identity and lifecycle methods linkable for server mods;
 * methods which need authoritative storage are conservative no-ops until a
 * native chunk handle is exposed.</p>
 */
public class WorldChunk extends Chunk {
    public enum CreationType { IMMEDIATE, QUEUED, CHECK }

    @FunctionalInterface
    public interface UnsavedListener {
        void setUnsaved(ChunkPos pos);
    }

    @FunctionalInterface
    public interface EntityLoader {
        void run(WorldChunk chunk);
    }

    public static class WrappedBlockEntityTickInvoker implements BlockEntityTickInvoker {
        protected Object wrapped;
        public WrappedBlockEntityTickInvoker() {}
        public WrappedBlockEntityTickInvoker(Object wrapped) { this.wrapped = wrapped; }
        public void setWrapped(Object wrapped) { this.wrapped = wrapped; }
        /** Intermediary 1.21.4 wrapper mutation hook. */
        public void method_31727(BlockEntityTickInvoker ticker) { this.wrapped = ticker; }
    }

    public static class DirectBlockEntityTickInvoker {
        protected final WorldChunk worldChunk;
        protected final BlockEntity blockEntity;
        protected final Object ticker;
        public DirectBlockEntityTickInvoker(WorldChunk worldChunk,
                                            BlockEntity blockEntity, Object ticker) {
            this.worldChunk = worldChunk;
            this.blockEntity = blockEntity;
            this.ticker = ticker;
        }
    }

    private final World world;
    private Map<BlockPos, BlockEntity> blockEntities = new java.util.HashMap<>();
    /** Pending NBT entries retained between proto-chunk load and promotion. */
    private Map<BlockPos, net.minecraft.nbt.NbtCompound> blockEntityNbts = new java.util.HashMap<>();
    /** Vanilla game-event dispatcher sections field used by 1.21.4 mixins. */
    private Int2ObjectMap<GameEventDispatcher> field_28129 = new Int2ObjectOpenHashMap<>();
    private UnsavedListener unsavedListener;
    private boolean loadedToWorld;

    public WorldChunk() { this(null, null); }
    public WorldChunk(World world, net.minecraft.util.math.ChunkPos pos) {
        super(pos);
        this.world = world;
    }
    public WorldChunk(World world, net.minecraft.util.math.ChunkPos pos,
                      UpgradeData upgradeData,
                      net.minecraft.world.tick.ChunkTickScheduler<?> blockTickScheduler,
                      net.minecraft.world.tick.ChunkTickScheduler<?> fluidTickScheduler,
                      long inhabitedTime, ChunkSection[] sections,
                      EntityLoader entityLoader,
                      net.minecraft.world.gen.chunk.BlendingData blendingData) {
        this(world, pos);
        // Keep the vanilla constructor's observable allocation point so
        // INVOKE_ASSIGN mixins can replace the backing map after the
        // superclass has initialized this object.
        Map<BlockPos, BlockEntity> initialBlockEntities = Maps.newHashMap();
        blockEntities.clear();
        blockEntities.putAll(initialBlockEntities);
    }

    /** 1.21.4 promotion constructor used by the chunk-loading pipeline. */
    public WorldChunk(net.minecraft.server.world.ServerWorld world,
                      ProtoChunk protoChunk, EntityLoader entityLoader) {
        this(world, protoChunk == null ? new ChunkPos(0, 0) : protoChunk.getPos());
        if (entityLoader != null) entityLoader.run(this);
    }

    public World getWorld() { return world; }
    @Override public BlockState getBlockState(BlockPos pos) {
        return world == null ? Blocks.AIR.getDefaultState() : world.getBlockState(pos);
    }
    public BlockState setBlockState(BlockPos pos, BlockState state, boolean moved) {
        if (world == null || pos == null || state == null) return getBlockState(pos);
        BlockState previous = getBlockState(pos);
        world.setBlockState(pos, state, moved ? 18 : 3);
        return previous;
    }
    public boolean isEmpty() { return blockEntities.isEmpty(); }
    public boolean canTickBlockEntities() { return loadedToWorld; }
    public void setLoadedToWorld(boolean loaded) { loadedToWorld = loaded; }
    public void setUnsavedListener(UnsavedListener listener) { unsavedListener = listener; }
    public Map<BlockPos, BlockEntity> getBlockEntities() {
        return Collections.unmodifiableMap(blockEntities);
    }
    @Override public BlockEntity getBlockEntity(BlockPos pos) {
        return pos == null ? null : blockEntities.get(pos);
    }
    public BlockEntity getBlockEntity(BlockPos pos, CreationType creationType) {
        if (pos == null) return null;
        BlockEntity blockEntity = blockEntities.get(pos);
        if (blockEntity == null) {
            net.minecraft.nbt.NbtCompound pending = blockEntityNbts.remove(pos);
            if (pending != null) {
                blockEntity = loadBlockEntity(pos, pending);
                if (blockEntity != null) return blockEntity;
            }
        }
        if (blockEntity == null && creationType == CreationType.IMMEDIATE) {
            blockEntity = createBlockEntity(pos);
            if (blockEntity != null) setBlockEntity(blockEntity);
        } else if (blockEntity != null && blockEntity.isRemoved()) {
            blockEntities.remove(pos);
            return null;
        }
        return blockEntity;
    }

    /** Vanilla's block-state factory boundary; native block entities are authoritative. */
    private BlockEntity createBlockEntity(BlockPos pos) { return null; }

    /** NBT hydration boundary used while a proto chunk is promoted. */
    private BlockEntity loadBlockEntity(BlockPos pos, net.minecraft.nbt.NbtCompound nbt) { return null; }
    public void addBlockEntity(BlockEntity blockEntity) {
        setBlockEntity(blockEntity);
    }
    /** Mojang-mapped name used by Fabric's block-entity transfer mixin. */
    public void setBlockEntity(BlockEntity blockEntity) {
        if (blockEntity != null && blockEntity.getPos() != null) {
            BlockEntity previous = blockEntities.put(blockEntity.getPos(), blockEntity);
            if (previous != null) previous.markRemoved();
            updateTicker(blockEntity);
        }
    }
    /** Block-entity ticker refresh boundary used by lifecycle mixins. */
    public void updateTicker(BlockEntity blockEntity) { }
    public void removeBlockEntity(BlockPos pos) {
        if (pos == null) return;
        BlockEntity removed = blockEntities.remove(pos);
        if (removed != null) removed.markRemoved();
    }
    public void loadEntities() {}
    public void updateAllBlockEntities() {}
    public void clear() { blockEntities.clear(); }
    public void setLoadedToWorldAndNotify(boolean loaded) { setLoadedToWorld(loaded); }
    public FluidState getFluidState(int x, int y, int z) {
        return net.minecraft.fluid.Fluids.EMPTY.getDefaultState();
    }
    public void markUnsaved() {
        markNeedsSaving();
        if (unsavedListener != null) unsavedListener.setUnsaved(getPos());
    }
    /** Intermediary 1.21.4 ticker factory symbol retained for accessors. */
    public WrappedBlockEntityTickInvoker method_31719(
            BlockEntity blockEntity, net.minecraft.block.entity.BlockEntityTicker<?> ticker,
            BlockPos pos, WrappedBlockEntityTickInvoker wrapped) {
        BlockEntityTickInvoker blockEntityTickInvoker = new BlockEntityTickInvoker() { };
        if (wrapped != null) {
            wrapped.method_31727(blockEntityTickInvoker);
            return wrapped;
        }
        WrappedBlockEntityTickInvoker wrappedBlockEntityTickInvoker2 =
            new WrappedBlockEntityTickInvoker(ticker);
        if (getWorld() != null) getWorld().addBlockEntityTicker(blockEntityTickInvoker);
        return wrappedBlockEntityTickInvoker2;
    }
    /** Synthetic lambda target emitted by the 1.21.4 block-entity ticker path. */
    private WrappedBlockEntityTickInvoker lambda$updateBlockEntityTicker$7(
            BlockEntity blockEntity, net.minecraft.block.entity.BlockEntityTicker<?> ticker,
            BlockPos pos, WrappedBlockEntityTickInvoker wrapped) {
        BlockEntityTickInvoker blockEntityTickInvoker = new BlockEntityTickInvoker() { };
        Object wrappedBlockEntityTickInvoker2 = wrapped;
        if (wrapped != null) wrapped.method_31727(blockEntityTickInvoker);
        if (wrappedBlockEntityTickInvoker2 == null) return null;
        return wrapped == null ? new WrappedBlockEntityTickInvoker(ticker) : wrapped;
    }
    /** Chunk-provider callback used by Lithium's level-type cache mixin. */
    public void setLevelTypeProvider(java.util.function.Supplier<?> provider) { }
    /** Game-event dispatcher cleanup hook used by 1.21.4 chunk mixins. */
    private void removeGameEventDispatcher(int ySectionCoord) {
        if (field_28129 != null) field_28129.remove(ySectionCoord);
    }
    /** Game-event dispatcher lookup boundary used by Lithium's event cache. */
    public GameEventDispatcher getGameEventDispatcher(int ySectionCoord) {
        return field_28129 == null ? null : field_28129.get(ySectionCoord);
    }
}
