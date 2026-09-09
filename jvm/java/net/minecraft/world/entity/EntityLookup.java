package net.minecraft.world.entity;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;
import java.util.function.Predicate;

/** Lightweight entity lookup used by server-side instrumentation APIs. */
public class EntityLookup<T extends EntityLike> {
    private final List<T> entities = new ArrayList<>();
    public void add(T entity) { if (entity != null && !entities.contains(entity)) entities.add(entity); }
    public void remove(T entity) { entities.remove(entity); }
    public T get(int id) { return null; }
    public List<T> getAll() { return List.copyOf(entities); }
    public void forEach(Consumer<? super T> consumer) { if (consumer != null) for (T entity : List.copyOf(entities)) consumer.accept(entity); }
    public void forEach(Predicate<? super T> predicate, Consumer<? super T> consumer) {
        if (consumer == null) return;
        for (T entity : List.copyOf(entities)) if (predicate == null || predicate.test(entity)) consumer.accept(entity);
    }
}
