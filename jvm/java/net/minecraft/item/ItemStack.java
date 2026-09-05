package net.minecraft.item;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;
import java.util.function.Consumer;
import net.minecraft.component.ComponentMap;
import net.minecraft.component.DataComponentType;
import net.minecraft.component.DataComponentTypes;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.registry.RegistryEntry;
import net.minecraft.registry.TagKey;
import net.minecraft.text.Text;
import net.minecraft.util.NativeAccess;
import net.minecraft.util.TypedActionResult;

public class ItemStack {
    public static final ItemStack EMPTY = new ItemStack(Items.AIR, 0, false);
    private Item item;
    private int count;
    private long nativeOwner;
    private int nativeSlot = -1;
    private final Map<DataComponentType<?>, Object> components = new LinkedHashMap<>();
    private NbtCompound nbt;

    private ItemStack(Item item, int count, boolean ignored) {
        this.item = item == null ? Items.AIR : item; this.count = Math.max(0, count);
    }
    public ItemStack(Item item) { this(item, 1); }
    public ItemStack(Item item, int count) { this(item, count, false); }
    public ItemStack(RegistryEntry<Item> item) { this(item == null ? Items.AIR : item.value(), 1); }
    public ItemStack(RegistryEntry<Item> item, int count) { this(item == null ? Items.AIR : item.value(), count); }
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
    public boolean isStackable() { return getMaxCount() > 1 && !isDamageable(); }
    public boolean isDamageable() { return item.isDamageable(); }
    public int getDamage() { return getOrDefault(DataComponentTypes.DAMAGE, 0); }
    public void setDamage(int value) { set(DataComponentTypes.DAMAGE, Math.max(0, Math.min(value, item.getMaxDamage()))); }
    public boolean isDamaged() { return getDamage() > 0; }
    public boolean hasGlint() { return false; }
    public boolean isEnchantable() { return !isEmpty(); }
    public <T> T get(DataComponentType<T> type) { return type == null ? null : cast(components.get(type)); }
    public <T> T getOrDefault(DataComponentType<T> type, T fallback) { T value = get(type); return value == null ? fallback : value; }
    public <T> T set(DataComponentType<T> type, T value) {
        Objects.requireNonNull(type, "type");
        @SuppressWarnings("unchecked") T previous = (T) components.put(type, value); return previous;
    }
    public <T> T remove(DataComponentType<T> type) {
        @SuppressWarnings("unchecked") T previous = (T) components.remove(type); return previous;
    }
    public boolean contains(DataComponentType<?> type) { return type != null && components.containsKey(type); }
    public ComponentMap getComponents() { return ComponentMap.of(components); }
    public void applyComponentsFrom(ItemStack source) { if (source != null) components.putAll(source.components); }
    public void applyComponents(Consumer<Map<DataComponentType<?>, Object>> consumer) { if (consumer != null) consumer.accept(components); }
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
