package net.minecraft.resource;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import net.minecraft.util.profiler.Profiler;

/**
 * Vanilla's two-stage resource reload base class.  The native server does
 * not currently execute data-pack reloads, but preserving the asynchronous
 * prepare/apply contract lets Fabric listeners register without ABI shims.
 */
public abstract class SinglePreparationResourceReloader<T> implements ResourceReloader {
    protected abstract T prepare(ResourceManager manager, Profiler profiler);

    protected abstract void apply(T prepared, ResourceManager manager, Profiler profiler);

    @Override
    public final CompletableFuture<Void> reload(Synchronizer synchronizer, ResourceManager manager,
                                                Profiler prepareProfiler, Profiler applyProfiler,
                                                Executor prepareExecutor, Executor applyExecutor) {
        CompletableFuture<T> prepared = CompletableFuture.supplyAsync(
            () -> prepare(manager, prepareProfiler), prepareExecutor);
        CompletableFuture<T> synchronizedResult = synchronizer == null
            ? prepared : prepared.thenCompose(synchronizer::whenPrepared);
        return synchronizedResult.thenAcceptAsync(
            value -> apply(value, manager, applyProfiler), applyExecutor);
    }
}
