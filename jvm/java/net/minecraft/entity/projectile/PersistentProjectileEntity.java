package net.minecraft.entity.projectile;

import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.sound.SoundEvent;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.util.hit.EntityHitResult;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.World;

/**
 * Server-side 1.21.4 projectile ABI used by access wideners and optimisation
 * mixins.  Native projectile simulation remains authoritative; the state
 * here is deliberately ordinary Java state so mod code can inspect and
 * transform the same lifecycle boundaries as the vanilla class.
 */
public class PersistentProjectileEntity extends Entity {
    protected boolean inGround;
    protected boolean critical;
    protected double damage = 2.0;
    protected byte piercingLevel;
    protected ItemStack stack = ItemStack.EMPTY;
    protected SoundEvent sound;
    protected PickupPermission pickupType = PickupPermission.DISALLOWED;

    protected PersistentProjectileEntity(EntityType<? extends PersistentProjectileEntity> type,
                                         LivingEntity owner, World world,
                                         ItemStack stack, ItemStack weapon) {
        super(type, world);
        this.stack = stack == null ? ItemStack.EMPTY : stack;
    }

    protected PersistentProjectileEntity(EntityType<? extends PersistentProjectileEntity> type,
                                         double x, double y, double z, World world,
                                         ItemStack stack, ItemStack weapon) {
        super(type, world);
        this.stack = stack == null ? ItemStack.EMPTY : stack;
        setPosition(x, y, z);
    }

    protected PersistentProjectileEntity(EntityType<? extends PersistentProjectileEntity> type,
                                         World world) {
        super(type, world);
    }

    public PersistentProjectileEntity(long nativeHandle) { super(nativeHandle); }

    public boolean isInGround() { return inGround; }
    protected void setInGround(boolean value) { inGround = value; }
    public boolean isCritical() { return critical; }
    public void setCritical(boolean value) { critical = value; }
    public double getDamage() { return damage; }
    public void setDamage(double value) { damage = value; }
    public void addDamage(double value) { damage += value; }
    public boolean isNoClip() { return false; }
    public void setNoClip(boolean value) { }
    public byte getPierceLevel() { return piercingLevel; }
    public void setPierceLevel(byte value) { piercingLevel = value; }
    public SoundEvent getHitSound() { return sound; }
    public SoundEvent getSound() { return sound; }
    public void setSound(SoundEvent value) { sound = value; }
    public ItemStack getItemStack() { return stack; }
    public void setStack(ItemStack value) { stack = value == null ? ItemStack.EMPTY : value; }
    public ItemStack asItemStack() { return stack.copy(); }
    public ItemStack getDefaultItemStack() { return stack; }
    public float getDragInWater() { return 0.6f; }
    protected void applyDamageModifier(float value) { damage *= value; }
    protected void applyDrag(float value) { setVelocity(getVelocity().multiply(value)); }
    protected boolean tryPickup(PlayerEntity player) { return pickupType == PickupPermission.ALLOWED; }
    public PickupPermission getPickupType() { return pickupType; }
    public void setPickupType(PickupPermission value) {
        pickupType = value == null ? PickupPermission.DISALLOWED : value;
    }
    public boolean isShotFromCrossbow() { return false; }
    public boolean shouldFall() { return false; }
    protected void age() { age++; }
    protected void fall() { }
    protected void clearPiercingStatus() { piercingLevel = 0; }
    protected void onHit(LivingEntity target) { }
    protected void onEntityHit(EntityHitResult hit) { }
    protected void onBlockHit(BlockHitResult hit) { }
    protected void applyCollision(BlockHitResult hit) { }
    protected void knockback(LivingEntity target, DamageSource source) { }
    protected void spawnBubbleParticles(Vec3d position) { }
    public boolean canPickup(PlayerEntity player) { return tryPickup(player); }
    public boolean isInGroundState() { return inGround; }

    @Override protected void readNbt(NbtCompound nbt) {
        super.readNbt(nbt);
        if (nbt != null) inGround = nbt.getInt("inGround", 0) != 0;
    }

    @Override protected NbtCompound writeNbt(NbtCompound nbt) {
        NbtCompound result = super.writeNbt(nbt);
        result.putInt("inGround", inGround ? 1 : 0);
        return result;
    }

    @Override public void tick() {
        super.tick();
        if (!inGround) setPosition(getX() + getVelocity().x,
                                    getY() + getVelocity().y,
                                    getZ() + getVelocity().z);
    }

    public enum PickupPermission {
        DISALLOWED,
        ALLOWED,
        CREATIVE_ONLY
    }
}
