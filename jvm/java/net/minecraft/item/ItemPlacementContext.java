package net.minecraft.item;

import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.util.Hand;
import net.minecraft.util.hit.BlockHitResult;

public class ItemPlacementContext extends ItemUsageContext {
    public ItemPlacementContext(PlayerEntity player, Hand hand, BlockHitResult hit) { super(player, hand, hit); }
    public boolean canPlace() { return getWorld() != null; }
}
