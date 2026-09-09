package net.fabricmc.fabric.api.registry;

import net.fabricmc.fabric.api.util.Block2ObjectMap;
import net.minecraft.block.Block;
import net.minecraft.block.Blocks;
import net.minecraft.registry.tag.TagKey;

/** Block flammability registry backed by a process-local identity map. */
public interface FlammableBlockRegistry extends Block2ObjectMap<FlammableBlockRegistry.Entry> {
    static FlammableBlockRegistry getDefaultInstance() { return getInstance(Blocks.OAK_PLANKS); }
    static FlammableBlockRegistry getInstance(Block block) {
        return net.fabricmc.fabric.impl.content.registry.FlammableBlockRegistryImpl.getInstance(block);
    }

    default void add(Block block, int burnChance, int spreadChance) {
        add(block, new Entry(burnChance, spreadChance));
    }
    default void add(TagKey<Block> tag, int burnChance, int spreadChance) {
        add(tag, new Entry(burnChance, spreadChance));
    }

    final class Entry {
        private final int burn;
        private final int spread;
        public Entry(int burn, int spread) { this.burn = burn; this.spread = spread; }
        public int getBurnChance() { return burn; }
        public int getSpreadChance() { return spread; }
        @Override public boolean equals(Object other) {
            return other instanceof Entry value && burn == value.burn && spread == value.spread;
        }
        @Override public int hashCode() { return burn * 11 + spread; }
    }
}
