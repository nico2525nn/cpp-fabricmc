package net.minecraft.component;

import net.minecraft.text.Text;

/** Frequently used vanilla component keys. Values remain Java-side until a native component bridge exists. */
public final class DataComponentTypes {
    private DataComponentTypes() {}
    public static final ComponentType<Integer> DAMAGE = new DataComponentType<>("minecraft:damage");
    public static final ComponentType<Integer> MAX_DAMAGE = new DataComponentType<>("minecraft:max_damage");
    public static final ComponentType<Text> CUSTOM_NAME = new DataComponentType<>("minecraft:custom_name");
    public static final ComponentType<Text> ITEM_NAME = new DataComponentType<>("minecraft:item_name");
    public static final ComponentType<Boolean> HIDE_TOOLTIP = new DataComponentType<>("minecraft:hide_tooltip");
    public static final ComponentType<Boolean> UNBREAKABLE = new DataComponentType<>("minecraft:unbreakable");
    public static final ComponentType<String> LORE = new DataComponentType<>("minecraft:lore");
}
