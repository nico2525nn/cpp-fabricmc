package net.minecraft.entity.passive;

import net.minecraft.entity.EntityType;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.item.ItemStack;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.Hand;
import net.minecraft.world.World;

/** Animal base ABI used by breeding-cap and conversion mixins. */
public class AnimalEntity extends PassiveEntity {
    protected AnimalEntity(EntityType<?> type, World world) { super(type, world); }
    protected AnimalEntity(long nativeHandle, World world, EntityType<?> type) { super(nativeHandle, world, type); }
    protected AnimalEntity() { super(); }
    public boolean isBreedingItem(ItemStack stack) { return stack != null && !stack.isEmpty(); }
    public boolean canBreedWith(AnimalEntity other) { return other != null && other != this; }
    public void breed(ServerWorld world, AnimalEntity other) { }
    public boolean interactMob(PlayerEntity player, Hand hand) { return false; }
}
