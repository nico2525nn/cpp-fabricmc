package net.minecraft.item;

import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.function.Supplier;
import net.minecraft.text.Text;
import net.minecraft.util.Identifier;
import net.minecraft.registry.RegistryWrapper;
import net.minecraft.resource.featuretoggle.FeatureSet;

public final class ItemGroup {
    private final String id;
    // These names and descriptors are part of the vanilla 1.21.4 target
    // class.  Fabric's ItemGroup mixins access them by name, so keep the
    // public compatibility list backed by the same collection instead of
    // maintaining two divergent entry stores.
    private final Collection<ItemStack> displayStacks;
    private final Set<ItemStack> searchTabStacks;
    private final List<ItemStack> entries;
    private final Text displayName;
    private final Supplier<ItemStack> iconSupplier;
    private ItemStack icon;
    private boolean special;
    private boolean scrollbar;
    private boolean renderName;
    private Row row;
    private int column;
    private Type type;
    private Identifier texture;
    private EntryCollector entryCollector;
    public ItemGroup(String id) { this(id, null, null); }
    private ItemGroup(String id, Text displayName, Supplier<ItemStack> iconSupplier) {
        this(Row.TOP, 0, Type.CATEGORY, displayName, iconSupplier,
            (context, entries) -> { }, id);
    }
    private ItemGroup(Row row, int column, Type type, Text displayName,
                      Supplier<ItemStack> iconSupplier, EntryCollector entryCollector) {
        this(row, column, type, displayName, iconSupplier, entryCollector,
            displayName == null ? "custom" : displayName.getString());
    }
    private ItemGroup(Row row, int column, Type type, Text displayName,
                      Supplier<ItemStack> iconSupplier, EntryCollector entryCollector,
                      String id) {
        this.id = id == null ? "" : id;
        this.displayStacks = new ArrayList<>();
        this.entries = (List<ItemStack>) this.displayStacks;
        this.searchTabStacks = new LinkedHashSet<>();
        this.displayName = displayName;
        this.iconSupplier = iconSupplier == null ? () -> ItemStack.EMPTY : iconSupplier;
        this.row = row == null ? Row.TOP : row;
        this.column = column;
        this.type = type == null ? Type.CATEGORY : type;
        this.entryCollector = entryCollector == null ? (context, entries) -> { } : entryCollector;
        this.special = false;
        this.scrollbar = true;
        this.renderName = true;
    }
    public static Builder builder() { return new Builder(); }
    public static Builder create(Row row, int column) { return new Builder(row, column); }
    public String id() { return id; }
    public Identifier getId() { return Identifier.of("minecraft", id); }
    public Text getDisplayName() { return displayName == null ? Text.translatable("itemGroup." + id) : displayName; }
    public ItemStack getIcon() {
        ItemStack supplied = iconSupplier == null ? null : iconSupplier.get();
        icon = supplied == null ? (entries.isEmpty() ? ItemStack.EMPTY : entries.get(0)) : supplied;
        return icon;
    }
    public List<ItemStack> getEntries() { return List.copyOf(entries); }
    public Collection<ItemStack> getDisplayStacks() { return List.copyOf(displayStacks); }
    public Collection<ItemStack> getSearchTabStacks() { return List.copyOf(searchTabStacks); }
    public Row getRow() { return row; }
    public int getColumn() { return column; }
    public Type getType() { return type; }
    public Identifier getTexture() { return texture; }
    public boolean isSpecial() { return special; }
    public boolean hasScrollbar() { return scrollbar; }
    public boolean shouldRenderName() { return renderName; }
    public boolean shouldDisplay() { return type != Type.SEARCH || !displayStacks.isEmpty(); }
    public void add(ItemStack stack) { if (stack != null && !stack.isEmpty()) entries.add(stack.copy()); }
    public void add(Item item) { if (item != null) add(item.getDefaultStack()); }
    public void updateEntries(DisplayContext context) {
        displayStacks.clear();
        searchTabStacks.clear();
        entryCollector.accept(context, new EntriesImpl(this));
    }
    public enum Type { CATEGORY, SEARCH, HOTBAR, INVENTORY }
    public enum Row { TOP, BOTTOM }
    public enum EntryPosition { ABOVE, BELOW }
    public enum StackVisibility { PARENT_AND_SEARCH_TABS, PARENT_TAB_ONLY, SEARCH_TAB_ONLY }
    public interface EntryCollector {
        void accept(DisplayContext displayContext, Entries entries);
    }
    public interface Entries {
        void add(ItemStack stack);
        default void add(ItemStack stack, StackVisibility visibility) { add(stack); }
        default void addAll(Collection<ItemStack> stacks) { if (stacks != null) for (ItemStack stack : stacks) add(stack); }
        default void addAll(Collection<ItemStack> stacks, StackVisibility visibility) { addAll(stacks); }
        default void add(Item item) { if (item != null) add(item.getDefaultStack()); }
        default void add(Item item, StackVisibility visibility) { add(item); }
    }
    public static final class DisplayContext {
        private final FeatureSet enabledFeatures;
        private final boolean hasPermissions;
        private final RegistryWrapper.WrapperLookup lookup;
        public DisplayContext(FeatureSet enabledFeatures, boolean hasPermissions,
                              RegistryWrapper.WrapperLookup lookup) {
            this.enabledFeatures = enabledFeatures;
            this.hasPermissions = hasPermissions;
            this.lookup = lookup;
        }
        public FeatureSet enabledFeatures() { return enabledFeatures; }
        public boolean hasPermissions() { return hasPermissions; }
        public RegistryWrapper.WrapperLookup lookup() { return lookup; }
        public boolean doesNotMatch(FeatureSet features, boolean permissions,
                                    RegistryWrapper.WrapperLookup registries) {
            return features != null && enabledFeatures != null && !enabledFeatures.equals(features)
                || permissions != hasPermissions || (registries != null && lookup != registries);
        }
    }
    public static final class Builder {
        private Row row = Row.TOP;
        private int column;
        private Text displayName;
        private Supplier<ItemStack> iconSupplier = () -> ItemStack.EMPTY;
        private EntryCollector entryCollector = (context, entries) -> { };
        private Type type = Type.CATEGORY;
        private boolean special;
        private boolean renderName = true;
        private boolean scrollbar = true;
        private Identifier texture;
        public Builder() { }
        public Builder(Row row, int column) { this.row = row; this.column = column; }
        public Builder displayName(Text value) { displayName = value; return this; }
        public Builder type(Type value) { type = value == null ? Type.CATEGORY : value; return this; }
        public Builder entries(EntryCollector value) { entryCollector = value == null ? (context, entries) -> { } : value; return this; }
        public Builder icon(Supplier<ItemStack> value) { iconSupplier = value == null ? () -> ItemStack.EMPTY : value; return this; }
        public Builder texture(Identifier value) { texture = value; return this; }
        public Builder special() { special = true; return this; }
        public Builder noRenderedName() { renderName = false; return this; }
        public Builder noScrollbar() { scrollbar = false; return this; }
        public ItemGroup build() {
            ItemGroup group = new ItemGroup(row, column, type, displayName, iconSupplier, entryCollector);
            group.special = special;
            group.renderName = renderName;
            group.scrollbar = scrollbar;
            group.texture = texture;
            entryCollector.accept(new DisplayContext(null, false, null), new EntriesImpl(group));
            return group;
        }
    }
    private static final class EntriesImpl implements Entries {
        private final ItemGroup group;
        private EntriesImpl(ItemGroup group) { this.group = group; }
        @Override public void add(ItemStack stack) { group.add(stack); }
    }
    @Override public String toString() { return id; }
}
