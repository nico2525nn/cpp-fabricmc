package net.minecraft.entity.passive;

import net.minecraft.entity.EntityType;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.item.ItemStack;
import net.minecraft.util.Hand;
import net.minecraft.world.World;

/** Water-creature hierarchy anchor used by ServerCore activation-range fixes. */
public class FishEntity extends AnimalEntity {
    protected FishEntity(EntityType<?> type, World world) { super(type, world); }
    protected FishEntity(long nativeHandle, World world, EntityType<?> type) { super(nativeHandle, world, type); }
    protected FishEntity() { super(); }
    public boolean isFromBucket() { return false; }
    public void setFromBucket(boolean value) { }
    public ItemStack getBucketItem() { return ItemStack.EMPTY; }
    public boolean interactMob(PlayerEntity player, Hand hand) { return false; }
}
