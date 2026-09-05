package net.minecraft.block;

import net.minecraft.item.Item;
import net.minecraft.util.Identifier;

public class BlockItem extends Item {
    private final Block block;
    public BlockItem(Block block) { super(block == null ? Identifier.of("minecraft", "air") : block.getId(), block == null ? 0 : block.getRawState()); this.block = block == null ? Blocks.AIR : block; }
    public BlockItem(Block block, Item.Settings settings) { this(block); }
    public Block getBlock() { return block; }
}
