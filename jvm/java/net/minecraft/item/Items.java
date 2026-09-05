package net.minecraft.item;

import net.minecraft.util.Identifier;

public final class Items {
    private Items() {}
    public static final Item AIR = new Item(Identifier.of("minecraft", "air"), 0);
    public static final Item STONE = item("stone", 1);
    public static final Item DIRT = item("dirt", 6);
    public static final Item COBBLESTONE = item("cobblestone", 7);
    public static final Item OAK_LOG = item("oak_log", 11);
    public static final Item OAK_PLANKS = item("oak_planks", 12);
    public static final Item GLASS = item("glass", 14);
    public static final Item TORCH = item("torch", 20);
    public static final Item IRON_INGOT = item("iron_ingot", 101);
    public static final Item GOLD_INGOT = item("gold_ingot", 102);
    public static final Item DIAMOND = item("diamond", 103);
    public static final Item EMERALD = item("emerald", 104);
    public static final Item REDSTONE = item("redstone", 105);
    public static final Item COAL = item("coal", 106);
    public static final Item STICK = item("stick", 107);
    public static final Item APPLE = item("apple", 108);
    public static final Item BREAD = item("bread", 109);
    public static final Item WHEAT = item("wheat", 110);
    public static final Item IRON_PICKAXE = item("iron_pickaxe", 111);
    public static final Item DIAMOND_PICKAXE = item("diamond_pickaxe", 112);
    public static final Item IRON_SWORD = item("iron_sword", 113);
    public static final Item DIAMOND_SWORD = item("diamond_sword", 114);
    private static Item item(String id, int raw) { return new Item(Identifier.of("minecraft", id), raw); }
    public static Item[] values() { return new Item[] { AIR, STONE, DIRT, COBBLESTONE, OAK_LOG, OAK_PLANKS, GLASS, TORCH, IRON_INGOT, GOLD_INGOT, DIAMOND, EMERALD, REDSTONE, COAL, STICK, APPLE, BREAD, WHEAT, IRON_PICKAXE, DIAMOND_PICKAXE, IRON_SWORD, DIAMOND_SWORD }; }
}
