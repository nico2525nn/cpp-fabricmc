package net.minecraft.item;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;
import java.util.function.Consumer;
import net.minecraft.component.ComponentChanges;
import net.minecraft.component.ComponentHolder;
import net.minecraft.component.ComponentMap;
import net.minecraft.component.ComponentType;
import net.minecraft.component.DataComponentType;
import net.minecraft.component.DataComponentTypes;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.registry.TagKey;
import net.minecraft.text.Text;
import net.minecraft.util.NativeAccess;
import net.minecraft.util.TypedActionResult;

public class ItemStack implements ComponentHolder, net.fabricmc.fabric.api.item.v1.FabricItemStack {
    public static final ItemStack EMPTY = new ItemStack(Items.AIR, 0, false);
    private Item item;
    private int count;
    private long nativeOwner;
    private int nativeSlot = -1;
    private final Map<ComponentType<?>, Object> components = new LinkedHashMap<>();
    private NbtCompound nbt;

    private ItemStack(Item item, int count, boolean ignored) {
        this.item = item == null ? Items.AIR : item; this.count = Math.max(0, count);
        if (this.item.getSettings() != null) components.putAll(this.item.getSettings().components());
    }
    public ItemStack(Item item) { this(item, 1); }
    public ItemStack(Item item, int count) { this(item, count, false); }
    public ItemStack(RegistryEntry<Item> item) { this(item == null ? Items.AIR : item.value(), 1); }
    public ItemStack(RegistryEntry<Item> item, int count) { this(item == null ? Items.AIR : item.value(), count); }
    /** Legacy package spelling remains accepted by older Fabric modules. */
    public ItemStack(net.minecraft.registry.RegistryEntry<Item> item) {
        this(item == null ? Items.AIR : item.value(), 1);
    }
    /** Legacy package spelling remains accepted by older Fabric modules. */
    public ItemStack(net.minecraft.registry.RegistryEntry<Item> item, int count) {
        this(item == null ? Items.AIR : item.value(), count);
    }
    public static ItemStack fromNative(long playerHandle, int slot) {
        int rawId = NativeAccess.inventoryItemId(playerHandle, slot);
        int nativeCount = NativeAccess.inventoryItemCount(playerHandle, slot);
        ItemStack result = new ItemStack(Item.fromRaw(rawId, NativeAccess.inventoryItemName(playerHandle, slot)), nativeCount);
        result.nativeOwner = playerHandle; result.nativeSlot = slot;
        return result;
    }
    public boolean isEmpty() { return count <= 0 || item == Items.AIR; }
    public Item getItem() { return item; }
    public RegistryEntry<Item> getRegistryEntry() { return item.getRegistryEntry(); }
    public int getCount() { return count; }
    public void setCount(int count) { this.count = Math.max(0, count); syncCountToNative(nativeOwner, nativeSlot); }
    public int getMaxCount() { return item.getMaxCount(); }
    public int getMaxStackSize() { return getMaxCount(); }
    public void decrement(int amount) { setCount(Math.max(0, count - Math.max(0, amount))); }
    public void decrementUnlessCreative(int amount, net.minecraft.entity.player.PlayerEntity player) { if (player == null || !player.isCreative()) decrement(amount); }
    public void increment(int amount) { setCount(count + Math.max(0, amount)); }
    public ItemStack split(int amount) {
        int taken = Math.max(0, Math.min(amount, count)); ItemStack result = copyWithCount(taken); decrement(taken); return result;
    }
    public ItemStack copyWithCount(int value) { ItemStack copy = copy(); copy.setCount(value); return copy; }
    public void syncCountToNative(long owner, int slot) {
        if (owner != 0 && slot >= 0) {
            NativeAccess.setInventoryItem(owner, slot, item.getRawId(), count);
            nativeOwner = owner; nativeSlot = slot;
        }
    }
    public ItemStack copy() {
        ItemStack copy = new ItemStack(item, count);
        copy.components.putAll(components); copy.nbt = nbt == null ? null : nbt.copy();
        return copy;
    }
    public ItemStack copyComponentsToNewStack(Item replacement, int newCount) {
        ItemStack copy = new ItemStack(replacement, newCount); copy.components.putAll(components); return copy;
    }
    public Text getName() {
        Text custom = get(DataComponentTypes.CUSTOM_NAME);
        return custom == null ? item.getName(this) : custom;
    }
    public String getTranslationKey() { return item.getTranslationKey(); }
    public boolean isOf(Item other) { return item.isOf(other); }
    public boolean isIn(TagKey<Item> tag) { return item.getRegistryEntry() != null && item.getRegistryEntry().isIn(tag); }
    public boolean isIn(net.minecraft.registry.tag.TagKey<Item> tag) {
        return item.getRegistryEntry() != null && item.getRegistryEntry().isIn(tag);
    }
    public boolean isStackable() { return getMaxCount() > 1 && !isDamageable(); }
    public boolean isDamageable() { return item.isDamageable(); }
    public int getDamage() { return getOrDefault(DataComponentTypes.DAMAGE, 0); }
    public void setDamage(int value) { set(DataComponentTypes.DAMAGE, Math.max(0, Math.min(value, item.getMaxDamage()))); }
    public boolean isDamaged() { return getDamage() > 0; }
    /** Vanilla equipment-damage entrypoint used by Fabric's entity events. */
    public void damage(int amount, net.minecraft.entity.LivingEntity entity,
                       net.minecraft.entity.EquipmentSlot slot) {
        if (amount <= 0 || !isDamageable() || isEmpty()) return;
        int next = getDamage() + amount;
        if (next >= item.getMaxDamage()) {
            decrement(1);
            setDamage(0);
        } else {
            setDamage(next);
        }
    }
    public boolean hasGlint() {
        return contains(new DataComponentType<Boolean>("minecraft:enchantment_glint_override"))
            || contains(new DataComponentType<Boolean>("minecraft:enchantments"));
    }
    public boolean isEnchantable() { return !isEmpty(); }
    @Override public <T> T get(ComponentType<? extends T> type) {
        return type == null ? null : cast(components.get(type));
    }
    @Override public <T> T getOrDefault(ComponentType<? extends T> type, T fallback) {
        T value = get(type); return value == null ? fallback : value;
    }
    public <T> T get(DataComponentType<? extends T> type) { return get((ComponentType<? extends T>) type); }
    public <T> T getOrDefault(DataComponentType<? extends T> type, T fallback) {
        return getOrDefault((ComponentType<? extends T>) type, fallback);
    }
    public <T> T set(ComponentType<? super T> type, T value) {
        Objects.requireNonNull(type, "type");
        @SuppressWarnings("unchecked") T previous = (T) components.put(type, value); return previous;
    }
    public <T> T set(DataComponentType<? super T> type, T value) {
        return set((ComponentType<? super T>) type, value);
    }
    public <T> T remove(ComponentType<? extends T> type) {
        @SuppressWarnings("unchecked") T previous = (T) components.remove(type); return previous;
    }
    public <T> T remove(DataComponentType<? extends T> type) { return remove((ComponentType<? extends T>) type); }
    @Override public boolean contains(ComponentType<?> type) { return type != null && components.containsKey(type); }
    public boolean contains(DataComponentType<?> type) { return contains((ComponentType<?>) type); }
    @Override public ComponentMap getComponents() { return ComponentMap.of(components); }
    public ComponentChanges getComponentChanges() {
        it.unimi.dsi.fastutil.objects.Reference2ObjectMap<ComponentType<?>, java.util.Optional<?>> changes =
            new it.unimi.dsi.fastutil.objects.Reference2ObjectOpenHashMap<>();
        components.forEach((type, value) -> changes.put(type, java.util.Optional.ofNullable(value)));
        return new ComponentChanges(changes);
    }
    public void applyComponentsFrom(ItemStack source) { if (source != null) components.putAll(source.components); }
    public void applyComponentsFrom(ComponentMap source) { if (source != null) components.putAll(source.asMap()); }
    public void applyChanges(ComponentChanges changes) {
        if (changes == null) return;
        changes.entrySet().forEach(entry -> {
            if (entry.getValue().isPresent()) components.put(entry.getKey(), entry.getValue().get());
            else components.remove(entry.getKey());
        });
    }
    public void applyUnvalidatedChanges(ComponentChanges changes) { applyChanges(changes); }
    public void applyComponents(Consumer<Map<ComponentType<?>, Object>> consumer) { if (consumer != null) consumer.accept(components); }
    public NbtCompound getNbt() { return nbt; }
    public void setNbt(NbtCompound value) { nbt = value == null ? null : value.copy(); }
    public boolean hasNbt() { return nbt != null && !nbt.isEmpty(); }
    public NbtCompound getOrCreateNbt() { if (nbt == null) nbt = new NbtCompound(); return nbt; }
    public TypedActionResult<ItemStack> use(net.minecraft.world.World world, net.minecraft.entity.player.PlayerEntity player, net.minecraft.util.Hand hand) {
        return item.use(world, player, hand);
    }
    public static boolean areItemsEqual(ItemStack left, ItemStack right) { return left != null && right != null && left.item.isOf(right.item); }
    public static boolean areEqual(ItemStack left, ItemStack right) {
        return areItemsEqual(left, right) && left.count == right.count && left.components.equals(right.components);
    }
    public static boolean canCombine(ItemStack left, ItemStack right) { return areItemsEqual(left, right) && left.components.equals(right.components); }
    @SuppressWarnings("unchecked") private static <T> T cast(Object value) { return (T) value; }
    @Override public String toString() { return count + "x " + item; }
}
