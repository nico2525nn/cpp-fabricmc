package net.fabricmc.fabric.api.lookup.v1.block;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.BiFunction;
import net.fabricmc.fabric.api.lookup.v1.custom.ApiLookupMap;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.block.entity.BlockEntity;
import net.minecraft.block.entity.BlockEntityType;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;

public interface BlockApiLookup<A, C> {
    ApiLookupMap<BlockApiLookup<?, ?>> LOOKUPS = ApiLookupMap.create(
        (id, api, context) -> new Impl<>(id, api, context));

    @SuppressWarnings("unchecked")
    static <A, C> BlockApiLookup<A, C> get(Identifier id, Class<A> apiClass, Class<C> contextClass) {
        return (BlockApiLookup<A, C>) LOOKUPS.getLookup(id, apiClass, contextClass);
    }

    default A find(World world, BlockPos pos, C context) {
        if (world == null || pos == null) return null;
        return find(world, pos, world.getBlockState(pos), world.getBlockEntity(pos), context);
    }

    A find(World world, BlockPos pos, BlockState state, BlockEntity blockEntity, C context);
    void registerSelf(BlockEntityType<?>... blockEntityTypes);
    void registerForBlocks(BlockApiProvider<A, C> provider, Block... blocks);
    default <T extends BlockEntity> void registerForBlockEntity(
            BiFunction<? super T, C, A> provider, BlockEntityType<T> blockEntityType) {
        registerForBlockEntities((blockEntity, context) ->
            blockEntity == null ? null : provider.apply((T) blockEntity, context), blockEntityType);
    }
    void registerForBlockEntities(BlockEntityApiProvider<A, C> provider,
                                   BlockEntityType<?>... blockEntityTypes);
    void registerFallback(BlockApiProvider<A, C> provider);
    Identifier getId();
    Class<A> apiClass();
    Class<C> contextClass();
    BlockApiProvider<A, C> getProvider(Block block);

    @FunctionalInterface
    interface BlockApiProvider<A, C> {
        A find(World world, BlockPos pos, BlockState state, BlockEntity blockEntity, C context);
    }
    @FunctionalInterface
    interface BlockEntityApiProvider<A, C> {
        A find(BlockEntity blockEntity, C context);
    }

    final class Impl<A, C> implements BlockApiLookup<A, C> {
        private final Identifier id;
        private final Class<A> apiClass;
        private final Class<C> contextClass;
        private final Map<Block, BlockApiProvider<A, C>> blockProviders = new ConcurrentHashMap<>();
        private final Map<BlockEntityType<?>, BlockEntityApiProvider<A, C>> entityProviders = new ConcurrentHashMap<>();
        private volatile BlockApiProvider<A, C> fallback;
        private Impl(Identifier id, Class<?> apiClass, Class<?> contextClass) {
            this.id = id; this.apiClass = (Class<A>) apiClass; this.contextClass = (Class<C>) contextClass;
        }
        @Override public A find(World world, BlockPos pos, BlockState state, BlockEntity entity, C context) {
            BlockApiProvider<A, C> provider = state == null ? null : getProvider(state.getBlock());
            if (provider != null) return provider.find(world, pos, state, entity, context);
            if (entity != null) for (Map.Entry<BlockEntityType<?>, BlockEntityApiProvider<A, C>> entry : entityProviders.entrySet()) {
                if (entry.getKey().supports(state == null ? null : state.getBlock()))
                    return entry.getValue().find(entity, context);
            }
            return fallback == null ? null : fallback.find(world, pos, state, entity, context);
        }
        @Override public void registerSelf(BlockEntityType<?>... types) {
            registerForBlockEntities((entity, context) -> apiClass.isInstance(entity) ? apiClass.cast(entity) : null, types);
        }
        @Override public void registerForBlocks(BlockApiProvider<A, C> provider, Block... blocks) {
            if (provider != null && blocks != null) for (Block block : blocks) if (block != null) blockProviders.put(block, provider);
        }
        @Override public void registerForBlockEntities(BlockEntityApiProvider<A, C> provider, BlockEntityType<?>... types) {
            if (provider != null && types != null) for (BlockEntityType<?> type : types) if (type != null) entityProviders.put(type, provider);
        }
        @Override public void registerFallback(BlockApiProvider<A, C> provider) { fallback = provider; }
        @Override public Identifier getId() { return id; }
        @Override public Class<A> apiClass() { return apiClass; }
        @Override public Class<C> contextClass() { return contextClass; }
        @Override public BlockApiProvider<A, C> getProvider(Block block) { return blockProviders.get(block); }
    }
}
