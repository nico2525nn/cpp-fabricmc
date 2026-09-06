package net.minecraft.world;

import net.minecraft.server.world.ServerWorld;

/** Server-side world access marker used by hostile spawn predicates. */
public interface ServerWorldAccess extends WorldAccess {
    default ServerWorld toServerWorld() { return this instanceof ServerWorld world ? world : null; }
}
