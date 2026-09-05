package net.minecraft.item;

import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.util.Hand;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;

public class ItemUsageContext {
    private final PlayerEntity player;
    private final Hand hand;
    private final BlockHitResult hit;
    public ItemUsageContext(PlayerEntity player, Hand hand, BlockHitResult hit) { this.player = player; this.hand = hand; this.hit = hit; }
    public PlayerEntity getPlayer() { return player; }
    public Hand getHand() { return hand; }
    public ItemStack getStack() { return player == null ? ItemStack.EMPTY : player.getStackInHand(hand); }
    public World getWorld() { return player == null ? null : player.getWorld(); }
    public BlockPos getBlockPos() { return hit == null ? new BlockPos(0, 0, 0) : hit.getBlockPos(); }
    public BlockHitResult getHitResult() { return hit; }
}
