package net.fabricmc.fabric.api.util;

import net.minecraft.item.Item;
import net.minecraft.item.ItemConvertible;
import net.minecraft.registry.tag.TagKey;

/** Item-keyed mutable map contract used by content registries. */
public interface Item2ObjectMap<V> {
    V get(ItemConvertible item);
    void add(ItemConvertible item, V value);
    void add(TagKey<Item> tag, V value);
    void remove(ItemConvertible item);
    void remove(TagKey<Item> tag);
    void clear(ItemConvertible item);
    void clear(TagKey<Item> tag);

    default V get(Item item) { return get((ItemConvertible) item); }
    default void add(Item item, V value) { add((ItemConvertible) item, value); }
    default void remove(Item item) { remove((ItemConvertible) item); }
    default void clear(Item item) { clear((ItemConvertible) item); }
}
