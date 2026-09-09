package net.minecraft.recipe;

import com.mojang.serialization.Codec;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.function.Predicate;
import java.util.stream.Stream;
import net.minecraft.item.Item;
import net.minecraft.item.ItemConvertible;
import net.minecraft.item.ItemStack;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.codec.PacketCodecs;
import net.minecraft.recipe.display.SlotDisplay;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.Registries;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.registry.entry.RegistryEntryList;
import net.minecraft.util.Identifier;
import net.minecraft.util.context.ContextParameterMap;

/**
 * A vanilla 1.21.4 ingredient: an immutable set of item registry entries.
 *
 * <p>Recipe JSON/DFU parsing remains outside the native runtime, but the
 * matching, display, and packet ABI is implemented here rather than exposed
 * as an empty class.  Fabric's custom-ingredient mixin also relies on the
 * exact {@code entries} and codec fields.</p>
 */
public class Ingredient implements RecipeMatcher.RawIngredient<RegistryEntry<Item>>,
                                  Predicate<ItemStack> {
    public static final Codec<Ingredient> CODEC = new Codec<>() { };
    public static final Codec<RegistryEntryList<Item>> ENTRIES_CODEC = new Codec<>() { };

    public static final PacketCodec<RegistryByteBuf, Ingredient> PACKET_CODEC =
        PacketCodec.ofLegacy((buffer, ingredient) -> {
            RegistryEntryList<Item> values = ingredient == null
                ? RegistryEntryList.empty() : ingredient.entries;
            buffer.writeVarInt(values.size());
            for (RegistryEntry<Item> entry : values) {
                Identifier id = entry == null || entry.value() == null
                    ? Identifier.ofVanilla("air")
                    : entry.getKey().map(RegistryKey::getValue).orElse(entry.value().getId());
                buffer.writeIdentifier(id);
            }
        }, buffer -> {
            int size = buffer.readVarInt();
            if (size < 0 || size > 256) throw new IllegalArgumentException("ingredient too large");
            java.util.ArrayList<RegistryEntry<Item>> values = new java.util.ArrayList<>(size);
            for (int index = 0; index < size; index++) {
                Identifier id = buffer.readIdentifier();
                Item item = Registries.ITEM.get(id);
                if (item == null) item = new Item(id);
                final Item decodedItem = item;
                RegistryEntry<Item> entry = Registries.ITEM.getEntry(id)
                    .orElseGet(() -> RegistryEntry.of(RegistryKey.of(RegistryKeys.ITEM, id), decodedItem));
                values.add(entry);
            }
            return new Ingredient(RegistryEntryList.of(values));
        });

    public static final PacketCodec<RegistryByteBuf, Optional<Ingredient>> OPTIONAL_PACKET_CODEC =
        PacketCodecs.optional(PACKET_CODEC);

    private final RegistryEntryList<Item> entries;

    protected Ingredient(RegistryEntryList<Item> entries) {
        this.entries = entries == null ? RegistryEntryList.empty() : entries;
    }

    /*
     * These three helpers are intentionally kept under their 1.21.4
     * intermediary names.  Fabric Recipe API injects at them because Mojang
     * did not assign named Yarn names to the codec/tag entry-list bridges.
     * Their signatures are part of the runtime ABI even though ordinary
     * mod source does not call them directly.
     */
    static RegistryEntryList<Item> method_61673(Ingredient ingredient) {
        return ingredient == null ? RegistryEntryList.empty() : ingredient.entries;
    }

    static RegistryEntryList<Item> method_61677(Ingredient ingredient) {
        return ingredient == null ? RegistryEntryList.empty() : ingredient.entries;
    }

    static RegistryEntryList<Item> method_61680(Ingredient ingredient) {
        return ingredient == null ? RegistryEntryList.empty() : ingredient.entries;
    }

    public static boolean matches(Optional<Ingredient> ingredient, ItemStack stack) {
        return ingredient != null && ingredient.isPresent() && ingredient.get().test(stack);
    }

    public static Ingredient ofItem(ItemConvertible item) {
        return item == null ? new Ingredient(RegistryEntryList.empty())
            : ofItems(Stream.of(item));
    }

    public static Ingredient ofItems(ItemConvertible[] items) {
        return items == null ? new Ingredient(RegistryEntryList.empty())
            : ofItems(Stream.of(items));
    }

    public static Ingredient ofItems(Stream<? extends ItemConvertible> items) {
        if (items == null) return new Ingredient(RegistryEntryList.empty());
        List<RegistryEntry<Item>> values = items
            .filter(Objects::nonNull)
            .map(ItemConvertible::asItem)
            .filter(Objects::nonNull)
            .map(Ingredient::entryFor)
            .filter(Objects::nonNull)
            .distinct()
            .toList();
        return new Ingredient(RegistryEntryList.of(values));
    }

    public static Ingredient fromTag(RegistryEntryList<Item> tag) {
        return new Ingredient(tag == null ? RegistryEntryList.empty() : tag);
    }

    /** Entry-list adapter used by Fabric's custom ingredient bridge. */
    public static RegistryEntryList<Item> entryListFromStream(
            Stream<? extends RegistryEntry<Item>> values) {
        if (values == null) return RegistryEntryList.empty();
        java.util.List<RegistryEntry<Item>> collected = values.filter(Objects::nonNull)
            .map(entry -> (RegistryEntry<Item>) entry).toList();
        return RegistryEntryList.of(collected);
    }

    /** Deprecated vanilla stream view retained for Fabric's RawIngredient ABI. */
    @Deprecated
    @Override public Stream<RegistryEntry<Item>> getMatchingItems() {
        return entries.stream();
    }

    public boolean isEmpty() { return entries.size() == 0; }

    @Override public boolean test(ItemStack itemStack) {
        return itemStack != null && !itemStack.isEmpty() && acceptsItem(itemStack.getRegistryEntry());
    }

    @Override public boolean acceptsItem(RegistryEntry<Item> registryEntry) {
        return registryEntry != null && entries.contains(registryEntry);
    }

    public SlotDisplay toDisplay() { return toDisplay(Optional.of(this)); }

    public static SlotDisplay toDisplay(Optional<Ingredient> ingredient) {
        List<ItemStack> stacks = ingredient == null ? List.of()
            : ingredient.filter(value -> !value.isEmpty())
                .map(value -> value.entries.stream()
                    .filter(Objects::nonNull)
                    .map(ItemStack::new)
                    .toList())
                .orElse(List.of());
        return new SlotDisplay() {
            @Override public List<ItemStack> getStacks(ContextParameterMap parameters) { return stacks; }
        };
    }

    /** Vanilla helper used by the recipe API's access widener. */
    private static SlotDisplay createDisplayWithRemainder(RegistryEntry<Item> displayedItem) {
        if (displayedItem == null || displayedItem.value() == null)
            return toDisplay(Optional.empty());
        return toDisplay(Optional.of(ofItem(displayedItem.value())));
    }

    @Override public boolean equals(Object other) {
        return other instanceof Ingredient ingredient && entries.equals(ingredient.entries);
    }

    @Override public int hashCode() { return entries.hashCode(); }

    private static RegistryEntry<Item> entryFor(Item item) {
        RegistryEntry<Item> entry = item.getRegistryEntry();
        if (entry != null) return entry;
        return RegistryEntry.of(RegistryKey.of(RegistryKeys.ITEM, item.getId()), item);
    }
}
