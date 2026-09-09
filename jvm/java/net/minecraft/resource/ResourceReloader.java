package net.minecraft.resource;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import net.minecraft.util.profiler.Profiler;

/** 1.21.4 resource reload contract used by Fabric API initialization. */
public interface ResourceReloader {
    CompletableFuture<Void> reload(Synchronizer synchronizer, ResourceManager manager,
                                   Profiler prepareProfiler, Profiler applyProfiler,
                                   Executor prepareExecutor, Executor applyExecutor);
    interface Synchronizer {
        <T> CompletableFuture<T> whenPrepared(T preparedObject);
    }
}
