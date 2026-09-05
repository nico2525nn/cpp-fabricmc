package net.minecraft.component;

import net.minecraft.text.Text;

/** Frequently used vanilla component keys. Values remain Java-side until a native component bridge exists. */
public final class DataComponentTypes {
    private DataComponentTypes() {}
    public static final DataComponentType<Integer> DAMAGE = new DataComponentType<>("minecraft:damage");
    public static final DataComponentType<Integer> MAX_DAMAGE = new DataComponentType<>("minecraft:max_damage");
    public static final DataComponentType<Text> CUSTOM_NAME = new DataComponentType<>("minecraft:custom_name");
    public static final DataComponentType<Text> ITEM_NAME = new DataComponentType<>("minecraft:item_name");
    public static final DataComponentType<Boolean> HIDE_TOOLTIP = new DataComponentType<>("minecraft:hide_tooltip");
    public static final DataComponentType<Boolean> UNBREAKABLE = new DataComponentType<>("minecraft:unbreakable");
    public static final DataComponentType<String> LORE = new DataComponentType<>("minecraft:lore");
}
