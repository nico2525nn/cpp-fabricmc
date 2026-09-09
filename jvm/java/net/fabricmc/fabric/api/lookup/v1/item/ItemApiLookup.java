package net.fabricmc.fabric.api.lookup.v1.item;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.fabricmc.fabric.api.lookup.v1.custom.ApiLookupMap;
import net.minecraft.item.Item;
import net.minecraft.item.ItemConvertible;
import net.minecraft.item.ItemStack;
import net.minecraft.util.Identifier;

public interface ItemApiLookup<A, C> {
    ApiLookupMap<ItemApiLookup<?, ?>> LOOKUPS = ApiLookupMap.create(
        (id, api, context) -> new Impl<>(id, api, context));

    @SuppressWarnings("unchecked")
    static <A, C> ItemApiLookup<A, C> get(Identifier id, Class<A> apiClass, Class<C> contextClass) {
        return (ItemApiLookup<A, C>) LOOKUPS.getLookup(id, apiClass, contextClass);
    }

    A find(ItemStack stack, C context);
    void registerSelf(ItemConvertible... items);
    void registerForItems(ItemApiProvider<A, C> provider, ItemConvertible... items);
    void registerFallback(ItemApiProvider<A, C> provider);
    Identifier getId();
    Class<A> apiClass();
    Class<C> contextClass();
    ItemApiProvider<A, C> getProvider(Item item);

    @FunctionalInterface
    interface ItemApiProvider<A, C> { A find(ItemStack stack, C context); }

    final class Impl<A, C> implements ItemApiLookup<A, C> {
        private final Identifier id;
        private final Class<A> apiClass;
        private final Class<C> contextClass;
        private final Map<Item, ItemApiProvider<A, C>> providers = new ConcurrentHashMap<>();
        private volatile ItemApiProvider<A, C> fallback;
        @SuppressWarnings("unchecked")
        private Impl(Identifier id, Class<?> apiClass, Class<?> contextClass) {
            this.id = id; this.apiClass = (Class<A>) apiClass; this.contextClass = (Class<C>) contextClass;
        }
        @Override public A find(ItemStack stack, C context) {
            ItemApiProvider<A, C> provider = stack == null ? null : providers.get(stack.getItem());
            if (provider != null) return provider.find(stack, context);
            return fallback == null ? null : fallback.find(stack, context);
        }
        @Override public void registerSelf(ItemConvertible... items) {
            registerForItems((stack, context) -> apiClass.isInstance(stack) ? apiClass.cast(stack) : null, items);
        }
        @Override public void registerForItems(ItemApiProvider<A, C> provider, ItemConvertible... items) {
            if (provider != null && items != null) for (ItemConvertible convertible : items) {
                Item item = convertible == null ? null : convertible.asItem();
                if (item != null) providers.put(item, provider);
            }
        }
        @Override public void registerFallback(ItemApiProvider<A, C> provider) { fallback = provider; }
        @Override public Identifier getId() { return id; }
        @Override public Class<A> apiClass() { return apiClass; }
        @Override public Class<C> contextClass() { return contextClass; }
        @Override public ItemApiProvider<A, C> getProvider(Item item) { return providers.get(item); }
    }
}
