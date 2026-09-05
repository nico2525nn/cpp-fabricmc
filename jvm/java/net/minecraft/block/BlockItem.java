package net.minecraft.block;

import net.minecraft.item.ItemPlacementContext;

/** @deprecated use the canonical {@link net.minecraft.item.BlockItem}. */
@Deprecated
public class BlockItem extends net.minecraft.item.BlockItem {
    public BlockItem(Block block) { super(block); }
    public BlockItem(Block block, net.minecraft.item.Item.Settings settings) { super(block, settings); }
}
