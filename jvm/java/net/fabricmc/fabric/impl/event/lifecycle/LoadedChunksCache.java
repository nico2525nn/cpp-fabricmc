package net.fabricmc.fabric.impl.event.lifecycle;

import java.util.Set;
import net.minecraft.world.chunk.WorldChunk;

/**
 * Internal lifecycle-event bridge implemented by the world shadow.
 *
 * <p>The real Fabric mixin adds this contract to {@code World}.  Providing
 * the same small interface in the shadow ABI keeps the contract valid even
 * when a world class was resolved before the optional lifecycle mixin is
 * registered.</p>
 */
public interface LoadedChunksCache {
    Set<WorldChunk> fabric_getLoadedChunks();
    void fabric_markLoaded(WorldChunk chunk);
    void fabric_markUnloaded(WorldChunk chunk);
}
