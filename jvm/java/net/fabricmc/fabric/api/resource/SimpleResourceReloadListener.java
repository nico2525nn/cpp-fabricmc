package net.fabricmc.fabric.api.resource;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import net.minecraft.resource.ResourceManager;
import net.minecraft.resource.ResourceReloader;
import net.minecraft.util.Identifier;

/** Convenience two-stage Fabric resource reload listener. */
public interface SimpleResourceReloadListener<T> extends IdentifiableResourceReloadListener {
    CompletableFuture<T> load(ResourceManager manager, Executor executor);
    CompletableFuture<Void> apply(T prepared, ResourceManager manager, Executor executor);

    /**
     * Four-argument intermediary ABI used by Fabric's 1.21.4 interface.
     * The named ResourceReloader contract adds profiler parameters, but the
     * Fabric convenience interface intentionally does not expose them.
     */
    default CompletableFuture<Void> method_25931(ResourceReloader.Synchronizer synchronizer,
                                                   ResourceManager manager,
                                                   Executor prepareExecutor,
                                                   Executor applyExecutor) {
        CompletableFuture<T> loaded = load(manager, prepareExecutor);
        CompletableFuture<T> synchronizedResult = synchronizer == null
            ? loaded : loaded.thenCompose(synchronizer::whenPrepared);
        return synchronizedResult.thenCompose(value -> apply(value, manager, applyExecutor));
    }

    @Override
    default CompletableFuture<Void> reload(ResourceReloader.Synchronizer synchronizer,
                                            ResourceManager manager,
                                            net.minecraft.util.profiler.Profiler prepareProfiler,
                                            net.minecraft.util.profiler.Profiler applyProfiler,
                                            Executor prepareExecutor, Executor applyExecutor) {
        CompletableFuture<T> loaded = load(manager, prepareExecutor);
        CompletableFuture<T> synchronizedResult = synchronizer == null
            ? loaded : loaded.thenCompose(synchronizer::whenPrepared);
        return synchronizedResult.thenCompose(value -> apply(value, manager, applyExecutor));
    }

    @Override Identifier getFabricId();
}
