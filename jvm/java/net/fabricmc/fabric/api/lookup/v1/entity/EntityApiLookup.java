package net.fabricmc.fabric.api.lookup.v1.entity;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.BiFunction;
import net.fabricmc.fabric.api.lookup.v1.custom.ApiLookupMap;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityType;
import net.minecraft.util.Identifier;

public interface EntityApiLookup<A, C> {
    ApiLookupMap<EntityApiLookup<?, ?>> LOOKUPS = ApiLookupMap.create(
        (id, api, context) -> new Impl<>(id, api, context));

    @SuppressWarnings("unchecked")
    static <A, C> EntityApiLookup<A, C> get(Identifier id, Class<A> apiClass, Class<C> contextClass) {
        return (EntityApiLookup<A, C>) LOOKUPS.getLookup(id, apiClass, contextClass);
    }

    A find(Entity entity, C context);
    void registerSelf(EntityType<?>... entityTypes);
    default <T extends Entity> void registerForType(BiFunction<T, C, A> provider,
                                                     EntityType<T> entityType) {
        registerForTypes((entity, context) -> entity == null ? null : provider.apply((T) entity, context), entityType);
    }
    void registerForTypes(EntityApiProvider<A, C> provider, EntityType<?>... entityTypes);
    void registerFallback(EntityApiProvider<A, C> provider);
    Identifier getId();
    Class<A> apiClass();
    Class<C> contextClass();
    EntityApiProvider<A, C> getProvider(EntityType<?> type);

    @FunctionalInterface
    interface EntityApiProvider<A, C> { A find(Entity entity, C context); }

    final class Impl<A, C> implements EntityApiLookup<A, C> {
        private final Identifier id;
        private final Class<A> apiClass;
        private final Class<C> contextClass;
        private final Map<EntityType<?>, EntityApiProvider<A, C>> providers = new ConcurrentHashMap<>();
        private volatile EntityApiProvider<A, C> fallback;
        @SuppressWarnings("unchecked")
        private Impl(Identifier id, Class<?> apiClass, Class<?> contextClass) {
            this.id = id; this.apiClass = (Class<A>) apiClass; this.contextClass = (Class<C>) contextClass;
        }
        @Override public A find(Entity entity, C context) {
            EntityApiProvider<A, C> provider = entity == null ? null : providers.get(entity.getType());
            if (provider != null) return provider.find(entity, context);
            return fallback == null ? null : fallback.find(entity, context);
        }
        @Override public void registerSelf(EntityType<?>... types) {
            registerForTypes((entity, context) -> apiClass.isInstance(entity) ? apiClass.cast(entity) : null, types);
        }
        @Override public void registerForTypes(EntityApiProvider<A, C> provider, EntityType<?>... types) {
            if (provider != null && types != null) for (EntityType<?> type : types) if (type != null) providers.put(type, provider);
        }
        @Override public void registerFallback(EntityApiProvider<A, C> provider) { fallback = provider; }
        @Override public Identifier getId() { return id; }
        @Override public Class<A> apiClass() { return apiClass; }
        @Override public Class<C> contextClass() { return contextClass; }
        @Override public EntityApiProvider<A, C> getProvider(EntityType<?> type) { return providers.get(type); }
    }
}
