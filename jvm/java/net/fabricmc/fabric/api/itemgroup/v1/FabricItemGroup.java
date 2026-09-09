package net.fabricmc.fabric.api.itemgroup.v1;

import net.minecraft.item.ItemGroup;

/** Factory facade for the vanilla item-group builder. */
public final class FabricItemGroup {
    private FabricItemGroup() { }
    public static ItemGroup.Builder builder() { return ItemGroup.builder(); }
}
