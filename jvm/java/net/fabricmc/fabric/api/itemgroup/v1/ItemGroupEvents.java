package net.fabricmc.fabric.api.itemgroup.v1;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Consumer;
import net.minecraft.item.ItemGroup;
import net.minecraft.item.ItemStack;
import net.minecraft.registry.RegistryKey;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;

/** In-process registration surface for server-side item-group callbacks. */
public final class ItemGroupEvents {
    private ItemGroupEvents() {}
    public interface ModifyEntries {
        void modifyEntries(FabricItemGroupEntries entries);
    }
    public interface ModifyEntriesAll {
        void modifyEntries(ItemGroup group, FabricItemGroupEntries entries);
    }
    public static final Event<ModifyEntriesAll> MODIFY_ENTRIES_ALL =
        EventFactory.createArrayBacked(ModifyEntriesAll.class,
            callbacks -> (group, entries) -> {
                for (ModifyEntriesAll callback : callbacks) callback.modifyEntries(group, entries);
            });
    private static final Map<RegistryKey<ItemGroup>, Event<ModifyEntries>> MODIFIERS =
        new ConcurrentHashMap<>();

    public static Event<ModifyEntries> modifyEntriesEvent(RegistryKey<ItemGroup> group) {
        if (group == null) throw new NullPointerException("group");
        return MODIFIERS.computeIfAbsent(group, ignored -> EventFactory.createArrayBacked(
            ModifyEntries.class, callbacks -> entries -> {
                for (ModifyEntries callback : callbacks) callback.modifyEntries(entries);
            }));
    }

    private static final Map<ItemGroup, Entries> ENTRIES = new ConcurrentHashMap<>();
    public static EntriesEvent modifyEntriesEvent(ItemGroup group) {
        if (group == null) throw new NullPointerException("group");
        return new EntriesEvent(ENTRIES.computeIfAbsent(group, ignored -> new Entries(group)));
    }
    public static EntriesEvent modifyEntriesEvent(net.minecraft.util.Identifier group) { return modifyEntriesEvent(group == null ? null : new ItemGroup(group.toString())); }
    public static void clear() { ENTRIES.clear(); MODIFIERS.clear(); MODIFY_ENTRIES_ALL.clear(); }
    public static final class EntriesEvent {
        private final Entries entries;
        private EntriesEvent(Entries entries) { this.entries = entries; }
        public void register(Consumer<Entries> callback) { if (callback != null) callback.accept(entries); }
    }
    public static final class Entries {
        private final ItemGroup group;
        private final java.util.List<ItemStack> values = new java.util.ArrayList<>();
        private Entries(ItemGroup group) { this.group = group; }
        public void add(ItemStack stack) { if (stack != null) values.add(stack); }
        public void add(net.minecraft.item.Item item) { if (item != null) add(new ItemStack(item)); }
        public void add(ItemStack stack, ItemGroup.StackVisibility visibility) { add(stack); }
        public void add(net.minecraft.item.Item item, ItemGroup.StackVisibility visibility) { add(item); }
        public void addAfter(net.minecraft.item.Item existing, net.minecraft.item.Item item) { add(item); }
        public void addBefore(net.minecraft.item.Item existing, net.minecraft.item.Item item) { add(item); }
        public ItemGroup getGroup() { return group; }
        public java.util.List<ItemStack> snapshot() { return java.util.List.copyOf(values); }
        public java.util.List<ItemStack> getDisplayStacks() { return snapshot(); }
    }
}
