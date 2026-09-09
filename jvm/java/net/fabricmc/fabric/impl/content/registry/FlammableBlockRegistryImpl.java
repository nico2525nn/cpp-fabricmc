package net.fabricmc.fabric.impl.content.registry;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.fabricmc.fabric.api.registry.FlammableBlockRegistry;
import net.minecraft.block.Block;
import net.minecraft.registry.tag.TagKey;

/** Internal implementation shared by the default and block-scoped views. */
public final class FlammableBlockRegistryImpl implements FlammableBlockRegistry {
    private static final FlammableBlockRegistryImpl INSTANCE = new FlammableBlockRegistryImpl();
    private final Map<Block, Entry> blocks = new ConcurrentHashMap<>();
    private final Map<TagKey<Block>, Entry> tags = new ConcurrentHashMap<>();

    private FlammableBlockRegistryImpl() { }
    public static FlammableBlockRegistryImpl getInstance(Block ignored) { return INSTANCE; }
    @Override public Entry get(Block block) {
        if (block == null) return null;
        Entry direct = blocks.get(block);
        if (direct != null) return direct;
        for (Map.Entry<TagKey<Block>, Entry> value : tags.entrySet())
            if (block.getRegistryEntry() != null && block.getRegistryEntry().isIn(value.getKey())) return value.getValue();
        return null;
    }
    @Override public void add(Block block, Entry value) { if (block != null && value != null) blocks.put(block, value); }
    @Override public void add(TagKey<Block> tag, Entry value) { if (tag != null && value != null) tags.put(tag, value); }
    @Override public void remove(Block block) { if (block != null) blocks.remove(block); }
    @Override public void remove(TagKey<Block> tag) { if (tag != null) tags.remove(tag); }
    @Override public void clear(Block block) { remove(block); }
    @Override public void clear(TagKey<Block> tag) { remove(tag); }
}
