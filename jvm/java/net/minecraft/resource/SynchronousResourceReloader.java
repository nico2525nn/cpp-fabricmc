package net.minecraft.resource;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import net.minecraft.util.profiler.Profiler;

/** Vanilla synchronous resource-reload contract. */
public interface SynchronousResourceReloader extends ResourceReloader {
    void reload(ResourceManager manager);

    @Override
    default CompletableFuture<Void> reload(Synchronizer synchronizer, ResourceManager manager,
                                           Profiler prepareProfiler, Profiler applyProfiler,
                                           Executor prepareExecutor, Executor applyExecutor) {
        CompletableFuture<Void> result = new CompletableFuture<>();
        Runnable work = () -> {
            try {
                reload(manager);
                result.complete(null);
            } catch (Throwable failure) {
                result.completeExceptionally(failure);
            }
        };
        try {
            if (applyExecutor == null) work.run();
            else applyExecutor.execute(work);
        } catch (Throwable failure) {
            result.completeExceptionally(failure);
        }
        return result;
    }
}
