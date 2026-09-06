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
        return getBlockEntity(pos);
    }
    public void addBlockEntity(BlockEntity blockEntity) {}
    public void removeBlockEntity(BlockPos pos) { if (pos != null) blockEntities.remove(pos); }
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
