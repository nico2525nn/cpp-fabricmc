package net.minecraft.entity.player;

import cppfm.bridge.NativeBridge;
import net.minecraft.entity.LivingEntity;
import net.minecraft.item.ItemStack;
import net.minecraft.util.Hand;
import net.minecraft.world.World;

public class PlayerEntity extends LivingEntity {
    protected PlayerEntity(long nativeHandle) { super(nativeHandle); }
    public boolean isSneaking() { return NativeBridge.nativePlayerSneaking(nativeHandle); }
    public boolean isCreative() { return NativeBridge.nativePlayerGameMode(nativeHandle) == 1; }
    public boolean isSpectator() { return NativeBridge.nativePlayerGameMode(nativeHandle) == 3; }
    public PlayerInventory getInventory() { return new PlayerInventory(nativeHandle); }
    public ItemStack getMainHandStack() { return getInventory().getMainHandStack(); }
    public ItemStack getOffHandStack() { return getInventory().getOffHandStack(); }
    public World getEntityWorld() { return getWorld(); }
    public void swingHand(Hand hand) {}
}
