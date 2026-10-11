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

    @Override
    default CompletableFuture<Void> reload(ResourceReloader.Synchronizer synchronizer,
                                            ResourceManager manager,
                                            Executor prepareExecutor, Executor applyExecutor) {
        CompletableFuture<T> loaded = load(manager, prepareExecutor);
        CompletableFuture<T> synchronizedResult = synchronizer == null
            ? loaded : loaded.thenCompose(synchronizer::whenPrepared);
        return synchronizedResult.thenComposeAsync(
            value -> apply(value, manager, applyExecutor), applyExecutor);
    }

    @Override Identifier getFabricId();
}
