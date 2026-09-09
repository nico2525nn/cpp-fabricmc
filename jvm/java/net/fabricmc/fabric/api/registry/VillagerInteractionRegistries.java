package net.fabricmc.fabric.api.registry;

import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import net.minecraft.item.Item;
import net.minecraft.item.ItemConvertible;
import net.minecraft.loot.LootTable;
import net.minecraft.registry.RegistryKey;
import net.minecraft.util.Identifier;
import net.minecraft.village.VillagerProfession;

/** Extensible villager gatherable, compostable, food and gift registries. */
public final class VillagerInteractionRegistries {
    private static final Set<Item> COLLECTABLE = ConcurrentHashMap.newKeySet();
    private static final List<Item> COMPOSTABLE = new CopyOnWriteArrayList<>();
    private static final Map<Item, Integer> FOOD = new ConcurrentHashMap<>();
    private static final Map<VillagerProfession, RegistryKey<LootTable>> GIFTS = new ConcurrentHashMap<>();
    private VillagerInteractionRegistries() { }

    public static void registerCollectable(ItemConvertible item) {
        if (item == null || item.asItem() == null) throw new NullPointerException("Item cannot be null!");
        COLLECTABLE.add(item.asItem());
    }
    public static void registerCompostable(ItemConvertible item) {
        if (item == null || item.asItem() == null) throw new NullPointerException("Item cannot be null!");
        COMPOSTABLE.add(item.asItem());
    }
    public static void registerFood(ItemConvertible item, int value) {
        if (item == null || item.asItem() == null) throw new NullPointerException("Item cannot be null!");
        FOOD.put(item.asItem(), value);
    }
    public static void registerGiftLootTable(VillagerProfession profession, Identifier id) {
        if (id == null) throw new NullPointerException("Loot table identifier cannot be null!");
        registerGiftLootTable(profession, RegistryKey.of(net.minecraft.registry.RegistryKeys.LOOT_TABLE, id));
    }
    public static void registerGiftLootTable(VillagerProfession profession, RegistryKey<LootTable> table) {
        if (profession == null || table == null) throw new NullPointerException("profession/loot table");
        GIFTS.put(profession, table);
    }
    public static boolean isCollectable(Item item) { return COLLECTABLE.contains(item); }
    public static boolean isCompostable(Item item) { return COMPOSTABLE.contains(item); }
    public static Integer foodValue(Item item) { return FOOD.get(item); }
    public static RegistryKey<LootTable> giftLootTable(VillagerProfession profession) { return GIFTS.get(profession); }
}
