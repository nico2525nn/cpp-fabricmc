package net.minecraft.entity;

import cppfm.bridge.WrapperCache;
import net.minecraft.item.ItemStack;
import net.minecraft.util.Hand;
import net.minecraft.util.NativeAccess;
import net.minecraft.util.TypedActionResult;
import net.minecraft.world.World;

public class LivingEntity extends Entity {
    private float health = 20.0f;
    private float maxHealth = 20.0f;
    private boolean usingItem;
    private Hand activeHand = Hand.MAIN_HAND;
    private EntityPose pose = EntityPose.STANDING;

    protected LivingEntity(long nativeHandle) { super(nativeHandle); }
    protected LivingEntity(EntityType<?> type, World world) { super(type, world); }
    protected LivingEntity(long nativeHandle, World world, EntityType<?> type) { super(nativeHandle, world, type); }
    public static LivingEntity of(long handle) {
        return WrapperCache.get(LivingEntity.class, handle, LivingEntity::new);
    }
    public float getHealth() { return nativeHandle == 0 ? health : NativeAccess.entityHealth(nativeHandle); }
    public float getMaxHealth() { return maxHealth; }
    public void setHealth(float value) {
        health = Math.max(0.0f, Math.min(maxHealth, value));
        if (nativeHandle != 0) NativeAccess.setEntityHealth(nativeHandle, health);
    }
    public void setMaxHealth(float value) { maxHealth = Math.max(0.0f, value); setHealth(health); }
    public boolean damage(DamageSource source, float amount) {
        if (!isAlive() || amount <= 0.0f || isInvulnerableTo(source)) return false;
        setHealth(health - amount);
        if (health <= 0.0f) remove(RemovalReason.KILLED);
        return true;
    }
    public boolean damage(Object source, float amount) { return damage(source instanceof DamageSource d ? d : DamageSources.generic(), amount); }
    public void heal(float amount) { if (amount > 0.0f) setHealth(health + amount); }
    public boolean isDead() { return !isAlive() || (nativeHandle == 0 ? health <= 0.0f : NativeAccess.entityDead(nativeHandle)); }
    public boolean isInvulnerable() { return false; }
    public boolean isInvulnerableTo(DamageSource source) { return isInvulnerable(); }
    public boolean isUsingItem() { return usingItem; }
    public Hand getActiveHand() { return activeHand; }
    public ItemStack getActiveItem() { return getStackInHand(activeHand); }
    public void setCurrentHand(Hand hand) { activeHand = hand == null ? Hand.MAIN_HAND : hand; usingItem = true; }
    public void clearActiveItem() { usingItem = false; }
    public ItemStack getStackInHand(Hand hand) { return ItemStack.EMPTY; }
    public void swingHand(Hand hand) { }
    public EntityPose getPose() { return pose; }
    public void setPose(EntityPose value) { pose = value == null ? EntityPose.STANDING : value; }
    public void setPose(net.minecraft.entity.Pose value) { pose = value == null ? EntityPose.STANDING : value.toEntityPose(); }
    public boolean isSleeping() { return pose == EntityPose.SLEEPING; }
    public float getArmor() { return 0.0f; }
    public float getArmorToughness() { return 0.0f; }
    public ItemStack getEquippedStack(EquipmentSlot slot) { return ItemStack.EMPTY; }
    public void equipStack(EquipmentSlot slot, ItemStack stack) { }
    public TypedActionResult<ItemStack> tryAttack(Entity target) { return TypedActionResult.success(ItemStack.EMPTY); }
}
