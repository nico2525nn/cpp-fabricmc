package net.minecraft.world.entity;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;
import net.minecraft.util.TypeFilter;
import net.minecraft.util.function.LazyIterationConsumer;

/**
 * Entity index used by the 1.21.4 server entity manager.
 *
 * <p>The native server remains the source of truth for live entities; this
 * index supplies the identity and iteration contract used by server-side
 * profiling and lifecycle mods.</p>
 */
public final class EntityIndex<T extends EntityLike> {
    private final Map<UUID, T> uuidToEntity = new HashMap<>();
    private final Map<Integer, T> idToEntity = new HashMap<>();

    public void add(T entity) {
        if (entity == null) return;
        if (entity instanceof net.minecraft.entity.Entity concrete) {
            uuidToEntity.put(concrete.getUuid(), entity);
            idToEntity.put(concrete.getId(), entity);
        }
    }

    public T get(UUID uuid) { return uuid == null ? null : uuidToEntity.get(uuid); }
    public T get(int id) { return idToEntity.get(id); }
    public int size() { return idToEntity.size(); }

    public void remove(T entity) {
        if (entity instanceof net.minecraft.entity.Entity concrete) {
            uuidToEntity.remove(concrete.getUuid());
            idToEntity.remove(concrete.getId());
        }
    }

    public Iterable<T> iterate() { return List.copyOf(idToEntity.values()); }

    public void forEach(TypeFilter<? super EntityLike, ?> filter,
                        LazyIterationConsumer<? super T> consumer) {
        if (consumer == null) return;
        for (T entity : List.copyOf(idToEntity.values())) {
            if (filter != null && filter.downcast(entity) == null) continue;
            if (consumer.accept(entity).shouldAbort()) break;
        }
    }

    public void forEach(Consumer<? super T> consumer) {
        if (consumer != null) for (T entity : List.copyOf(idToEntity.values())) consumer.accept(entity);
    }
}
