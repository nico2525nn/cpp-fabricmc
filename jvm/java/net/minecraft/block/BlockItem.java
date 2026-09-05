package net.minecraft.block;

import net.minecraft.item.Item;
import net.minecraft.item.ItemPlacementContext;
import net.minecraft.item.ItemStack;
import net.minecraft.util.ActionResult;
import net.minecraft.util.Identifier;

public class BlockItem extends Item {
    private final Block block;
    public BlockItem(Block block) { super(block == null ? Identifier.of("minecraft", "air") : block.getId(), block == null ? 0 : block.getRawState()); this.block = block == null ? Blocks.AIR : block; }
    public BlockItem(Block block, Item.Settings settings) {
        super(block == null ? Identifier.of("minecraft", "air") : block.getId(),
            block == null ? 0 : block.getRawState(), settings);
        this.block = block == null ? Blocks.AIR : block;
    }
    public Block getBlock() { return block; }
    public BlockState getPlacementState(ItemPlacementContext context) { return block.getPlacementState(context); }
    @Override public ActionResult place(ItemPlacementContext context) { return block.getPlacementState(context) == null ? ActionResult.FAIL : ActionResult.SUCCESS; }
    public String getTranslationKey() { return block.getTranslationKey(); }
}
