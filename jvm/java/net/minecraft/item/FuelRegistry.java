package net.minecraft.item;

import it.unimi.dsi.fastutil.objects.Object2IntLinkedOpenHashMap;
import it.unimi.dsi.fastutil.objects.Object2IntSortedMap;
import java.util.LinkedHashSet;
import java.util.Set;
import net.minecraft.registry.RegistryWrapper;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.resource.featuretoggle.FeatureSet;

/** Stateful vanilla fuel registry used by Fabric content-registry callbacks. */
public class FuelRegistry {
    private final Object2IntSortedMap<Item> fuelValues;

    public FuelRegistry(Object2IntSortedMap<Item> fuelValues) {
        this.fuelValues = fuelValues == null ? new Object2IntLinkedOpenHashMap<>() : fuelValues;
    }

    public boolean isFuel(ItemStack item) { return item != null && fuelValues.containsKey(item.getItem()); }
    public Set<Item> getFuelItems() { return java.util.Collections.unmodifiableSet(new LinkedHashSet<>(fuelValues.keySet())); }
    public int getFuelTicks(ItemStack item) { return item == null ? 0 : fuelValues.getInt(item.getItem()); }

    public static FuelRegistry createDefault(RegistryWrapper.WrapperLookup registries,
                                             FeatureSet enabledFeatures) {
        return createDefault(registries, enabledFeatures, 200);
    }

    public static FuelRegistry createDefault(RegistryWrapper.WrapperLookup registries,
                                             FeatureSet enabledFeatures, int itemSmeltTime) {
        Builder builder = new Builder(registries, enabledFeatures);
        builder.add(Items.COAL, itemSmeltTime * 8);
        builder.add(Items.STICK, itemSmeltTime / 2);
        net.fabricmc.fabric.api.registry.FuelRegistryEvents.BUILD.invoker().build(builder,
            new net.fabricmc.fabric.api.registry.FuelRegistryEvents.Context() {
                @Override public int baseSmeltTime() { return itemSmeltTime; }
                @Override public RegistryWrapper.WrapperLookup registries() { return registries; }
                @Override public FeatureSet enabledFeatures() { return enabledFeatures == null ? FeatureSet.EMPTY : enabledFeatures; }
            });
        net.fabricmc.fabric.api.registry.FuelRegistryEvents.EXCLUSIONS.invoker().buildExclusions(builder,
            new net.fabricmc.fabric.api.registry.FuelRegistryEvents.Context() {
                @Override public int baseSmeltTime() { return itemSmeltTime; }
                @Override public RegistryWrapper.WrapperLookup registries() { return registries; }
                @Override public FeatureSet enabledFeatures() { return enabledFeatures == null ? FeatureSet.EMPTY : enabledFeatures; }
            });
        return builder.build();
    }

    public static final class Builder {
        private final RegistryWrapper.WrapperLookup itemLookup;
        private final FeatureSet enabledFeatures;
        private final Object2IntSortedMap<Item> fuelValues = new Object2IntLinkedOpenHashMap<>();

        public Builder(RegistryWrapper.WrapperLookup itemLookup, FeatureSet enabledFeatures) {
            this.itemLookup = itemLookup;
            this.enabledFeatures = enabledFeatures == null ? FeatureSet.EMPTY : enabledFeatures;
        }

        public Builder add(ItemConvertible item, int value) {
            if (item != null && item.asItem() != null) fuelValues.put(item.asItem(), Math.max(0, value));
            return this;
        }
        public Builder add(TagKey<Item> tag, int value) {
            if (tag != null) for (Item item : net.minecraft.registry.Registries.ITEM)
                if (item.getRegistryEntry() != null && item.getRegistryEntry().isIn(tag)) add(item, value);
            return this;
        }
        public Builder remove(TagKey<Item> tag) {
            if (tag != null) for (Item item : new LinkedHashSet<>(fuelValues.keySet()))
                if (item.getRegistryEntry() != null && item.getRegistryEntry().isIn(tag)) fuelValues.remove(item);
            return this;
        }
        public Builder add(int value, Item item) { return add(item, value); }
        public Builder add(int value, TagKey<Item> tag) { return add(tag, value); }
        public Builder remove(Item item) { if (item != null) fuelValues.remove(item); return this; }
        public FuelRegistry build() { return new FuelRegistry(fuelValues); }
        public RegistryWrapper.WrapperLookup itemLookup() { return itemLookup; }
        public FeatureSet enabledFeatures() { return enabledFeatures; }
    }
}
