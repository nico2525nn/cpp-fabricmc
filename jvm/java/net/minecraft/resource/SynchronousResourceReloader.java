package net.minecraft.resource;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import net.minecraft.util.profiler.Profiler;

/** Vanilla synchronous resource-reload contract. */
public interface SynchronousResourceReloader extends ResourceReloader {
    void reload(ResourceManager manager);

    @Override
    default CompletableFuture<Void> reload(Synchronizer synchronizer, ResourceManager manager,
                                           Executor prepareExecutor, Executor applyExecutor) {
        CompletableFuture<Void> prepared = synchronizer == null
            ? CompletableFuture.completedFuture(null)
            : synchronizer.whenPrepared(null);
        return prepared.thenRunAsync(() -> reload(manager), applyExecutor);
    }
}
