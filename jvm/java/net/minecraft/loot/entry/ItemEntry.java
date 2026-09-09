package net.minecraft.loot.entry;

import java.util.Collection;
import net.minecraft.item.Item;
import net.minecraft.loot.condition.LootCondition;

/** Common vanilla item-entry builder used by data-pack and mod loot code. */
public final class ItemEntry extends LootPoolEntry {
    private final Item item;

    private ItemEntry(Item item, Collection<? extends LootCondition> conditions) {
        super(conditions);
        this.item = item;
    }

    public Item getItem() { return item; }

    public static Builder builder(Item item) { return new Builder(item); }

    public static final class Builder extends LootPoolEntry.Builder {
        private final Item item;
        public Builder(Item item) { this.item = item; }
        @Override public ItemEntry build() { return new ItemEntry(item, getConditions()); }
    }
}
