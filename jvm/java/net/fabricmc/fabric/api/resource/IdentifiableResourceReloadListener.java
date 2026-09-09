package net.fabricmc.fabric.api.resource;

import net.minecraft.resource.ResourceReloader;
import net.minecraft.util.Identifier;
import java.util.Collection;
import java.util.List;

/** Resource reload listener with a stable Fabric identifier. */
public interface IdentifiableResourceReloadListener extends ResourceReloader {
    Identifier getFabricId();

    default Collection<Identifier> getFabricDependencies() { return List.of(); }
}
