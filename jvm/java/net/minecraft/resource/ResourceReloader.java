package net.minecraft.resource;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;

/** 1.21.4 resource reload contract used by Fabric API initialization. */
public interface ResourceReloader {
    CompletableFuture<Void> reload(Synchronizer synchronizer, ResourceManager manager,
                                   Executor prepareExecutor, Executor applyExecutor);

    /** Intermediary name for the 1.21.4 four-argument reload method. */
    default CompletableFuture<Void> method_25931(Synchronizer synchronizer,
                                                  ResourceManager manager,
                                                  Executor prepareExecutor,
                                                  Executor applyExecutor) {
        return reload(synchronizer, manager, prepareExecutor, applyExecutor);
    }

    /** Default name used by vanilla reload diagnostics. */
    default String getName() { return getClass().getSimpleName(); }

    interface Synchronizer {
        <T> CompletableFuture<T> whenPrepared(T preparedObject);
    }
}
