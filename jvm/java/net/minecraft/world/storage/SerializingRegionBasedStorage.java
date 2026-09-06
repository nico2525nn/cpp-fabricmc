package net.minecraft.world.storage;

import com.mojang.serialization.Codec;
import java.util.function.BiFunction;
import java.util.function.Function;
import net.minecraft.registry.DynamicRegistryManager;
import net.minecraft.server.world.ChunkErrorHandler;
import net.minecraft.world.HeightLimitView;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;

/**
 * Linkable shadow for the region-backed storage base used by optimization
 * mods.  Persistence is owned by the native server; the Java surface keeps
 * lifecycle and generic method descriptors available to mixins.
 */
public class SerializingRegionBasedStorage<R> implements AutoCloseable {
    public SerializingRegionBasedStorage() {}
    public SerializingRegionBasedStorage(ChunkPosKeyedStorage storage, Codec<R> codec,
                                        Function<?, ?> decoder, BiFunction<?, ?, ?> merger,
                                        Function<?, ?> encoder, DynamicRegistryManager registryManager,
                                        ChunkErrorHandler errorHandler, HeightLimitView heightLimit) { }

    public Optional<R> get(long pos) { return Optional.empty(); }
    public Optional<R> getIfLoaded(long pos) { return Optional.empty(); }
    public R getOrCreate(long pos) { return null; }
    public CompletableFuture<R> load(Object pos) {
        return CompletableFuture.completedFuture(null);
    }
    public boolean hasUnsavedElements() { return false; }
    public void tick(java.util.function.BooleanSupplier shouldKeepTicking) {}
    public void save() {}
    @Override public void close() { save(); }
}
