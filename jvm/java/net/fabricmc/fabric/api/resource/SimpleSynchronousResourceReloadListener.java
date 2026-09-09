package net.fabricmc.fabric.api.resource;

import net.minecraft.resource.SynchronousResourceReloader;

/** Identifier-bearing synchronous resource listener. */
public interface SimpleSynchronousResourceReloadListener
        extends IdentifiableResourceReloadListener, SynchronousResourceReloader { }
