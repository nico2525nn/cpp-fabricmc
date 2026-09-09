package net.minecraft.world.entity;

import java.util.UUID;

/** Lookup facade backed by the vanilla EntityIndex/SectionedEntityCache pair. */
public final class SimpleEntityLookup<T extends EntityLike> extends EntityLookup<T> {
    private final EntityIndex<T> index;
    private final SectionedEntityCache cache;

    public SimpleEntityLookup(EntityIndex<T> index, SectionedEntityCache cache) {
        this.index = index == null ? new EntityIndex<>() : index;
        this.cache = cache == null ? new SectionedEntityCache() : cache;
    }

    @Override public T get(int id) { return index.get(id); }
    public T get(UUID uuid) { return index.get(uuid); }
    @Override public java.util.List<T> getAll() {
        java.util.ArrayList<T> result = new java.util.ArrayList<>();
        for (T entity : index.iterate()) result.add(entity);
        return java.util.List.copyOf(result);
    }
    public EntityIndex<T> getIndex() { return index; }
    public SectionedEntityCache getCache() { return cache; }
}
