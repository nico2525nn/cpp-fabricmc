package net.fabricmc.fabric.api.itemgroup.v1;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Consumer;
import net.minecraft.item.ItemGroup;
import net.minecraft.item.ItemStack;

/** In-process registration surface for server-side item-group callbacks. */
public final class ItemGroupEvents {
    private ItemGroupEvents() {}
    private static final Map<ItemGroup, Entries> ENTRIES = new ConcurrentHashMap<>();
    public static EntriesEvent modifyEntriesEvent(ItemGroup group) {
        return new EntriesEvent(ENTRIES.computeIfAbsent(group, ignored -> new Entries()));
    }
    public static void clear() { ENTRIES.clear(); }
    public static final class EntriesEvent {
        private final Entries entries;
        private EntriesEvent(Entries entries) { this.entries = entries; }
        public void register(Consumer<Entries> callback) { if (callback != null) callback.accept(entries); }
    }
    public static final class Entries {
        private final java.util.List<ItemStack> values = new java.util.ArrayList<>();
        public void add(ItemStack stack) { if (stack != null) values.add(stack); }
        public void add(net.minecraft.item.Item item) { if (item != null) add(new ItemStack(item)); }
        public java.util.List<ItemStack> snapshot() { return java.util.List.copyOf(values); }
    }
}
