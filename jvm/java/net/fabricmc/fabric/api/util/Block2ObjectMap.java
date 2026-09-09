package net.fabricmc.fabric.api.util;

import net.minecraft.block.Block;
import net.minecraft.registry.tag.TagKey;

/** Block-keyed mutable map contract used by content registries. */
public interface Block2ObjectMap<V> {
    V get(Block block);
    void add(Block block, V value);
    void add(TagKey<Block> tag, V value);
    void remove(Block block);
    void remove(TagKey<Block> tag);
    void clear(Block block);
    void clear(TagKey<Block> tag);
}
