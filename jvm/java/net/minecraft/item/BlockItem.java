package net.minecraft.item;

import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.util.ActionResult;
import net.minecraft.util.Identifier;

/** Canonical 1.21.4 block item shadow backed by the block descriptor. */
public class BlockItem extends Item {
    private final Block block;

    public BlockItem(Block block) {
        super(block == null ? Identifier.of("minecraft", "air") : block.getId(),
            block == null ? 0 : block.getRawState());
        this.block = block == null ? net.minecraft.block.Blocks.AIR : block;
    }

    public BlockItem(Block block, Item.Settings settings) {
        super(block == null ? Identifier.of("minecraft", "air") : block.getId(),
            settings == null ? new Item.Settings() : settings);
        this.block = block == null ? net.minecraft.block.Blocks.AIR : block;
    }

    public Block getBlock() { return block; }
    public BlockState getPlacementState(ItemPlacementContext context) { return block.getPlacementState(context); }
    @Override public ActionResult place(ItemPlacementContext context) {
        return block.getPlacementState(context) == null ? ActionResult.FAIL : ActionResult.SUCCESS;
    }
    /** Yarn overload used by Carpet's block-placement mixin. */
    public boolean place(ItemPlacementContext context, BlockState state) {
        return context != null && context.getWorld() != null && state != null;
    }
    /** Placement predicate called by Mojang-mapped mixins. */
    public boolean canPlace(ItemPlacementContext context, BlockState state) {
        return place(context, state);
    }
    @Override public String getTranslationKey() { return block.getTranslationKey(); }
}
