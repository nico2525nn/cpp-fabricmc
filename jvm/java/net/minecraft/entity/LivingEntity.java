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
    public boolean damage(net.minecraft.entity.damage.DamageSource source, float amount) {
        if (!isAlive() || !Float.isFinite(amount) || amount <= 0.0f || isInvulnerableTo(source)) return false;
        net.minecraft.entity.damage.DamageSource damageSource = source == null
            ? new net.minecraft.entity.damage.DamageSource("generic") : source;
        if (!net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.ALLOW_DAMAGE.invoker()
                .allowDamage(this, damageSource, amount)) return false;
        net.minecraft.entity.DamageSource legacySource = new net.minecraft.entity.DamageSource(
            damageSource.getName(), damageSource.getSource(), damageSource.getAttacker());
        if (!net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.ALLOW_DAMAGE.invoker()
                .allowDamage(this, legacySource, amount)) return false;
        float before = health;
        if (nativeHandle != 0 && !NativeAccess.entityDead(nativeHandle)) {
            float observed = getHealth();
            if (observed > 0.0f) before = observed;
        }
        float next = Math.max(0.0f, before - amount);
        setHealth(next);
        boolean died = next <= 0.0f;
        if (died) {
            boolean allowed = net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.ALLOW_DEATH.invoker()
                .allowDeath(this, damageSource, amount);
            allowed &= net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.ALLOW_DEATH.invoker()
                .allowDeath(this, legacySource, amount);
            if (this instanceof net.minecraft.server.network.ServerPlayerEntity player)
                allowed &= net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents.ALLOW_DEATH.invoker()
                    .allowDeath(player, damageSource, amount);
            if (allowed) remove(RemovalReason.KILLED);
            else { died = false; setHealth(Math.max(1.0f, Math.min(maxHealth, before))); }
        }
        final boolean blocked = false;
        net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AFTER_DAMAGE.invoker()
            .afterDamage(this, damageSource, amount, Math.max(0.0f, before - getHealth()), blocked);
        net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AFTER_DAMAGE.invoker()
            .afterDamage(this, legacySource, amount, Math.max(0.0f, before - getHealth()), blocked);
        if (died) {
            net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AFTER_DEATH.invoker()
                .afterDeath(this, damageSource);
            net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AFTER_DEATH.invoker()
                .afterDeath(this, legacySource);
        }
        return true;
    }
    /** Legacy package overload retained for source compatibility. */
    public boolean damage(DamageSource source, float amount) { return damage((net.minecraft.entity.damage.DamageSource) source, amount); }
    public boolean damage(Object source, float amount) {
        return damage(source instanceof net.minecraft.entity.damage.DamageSource d
            ? d : new net.minecraft.entity.damage.DamageSource("generic"), amount);
    }
    public void heal(float amount) { if (amount > 0.0f) setHealth(health + amount); }
    public boolean isDead() { return !isAlive() || (nativeHandle == 0 ? health <= 0.0f : NativeAccess.entityDead(nativeHandle)); }
    public boolean isInvulnerable() { return false; }
    public boolean isInvulnerableTo(net.minecraft.entity.damage.DamageSource source) { return isInvulnerable(); }
    public boolean isInvulnerableTo(DamageSource source) { return isInvulnerableTo((net.minecraft.entity.damage.DamageSource) source); }
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
