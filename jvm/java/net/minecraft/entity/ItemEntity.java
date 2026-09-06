package net.minecraft.entity;

import java.util.UUID;
import net.minecraft.item.ItemStack;
import net.minecraft.world.World;

/** Native-backed item entity surface used by server-side optimization mods. */
public class ItemEntity extends Entity {
    public static final int DESPAWN_AGE = 6000;
    public static final int NEVER_DESPAWN_AGE = -32768;
    public static final int CANNOT_PICK_UP_DELAY = 32767;
    protected ItemStack stack;
    protected Entity thrower;
    protected UUID throwerUuid;
    protected UUID owner;
    protected int itemAge;
    protected int pickupDelay;
    protected int health = 5;

    public ItemEntity() { this(null, 0.0, 0.0, 0.0, ItemStack.EMPTY); }
    public ItemEntity(World world, double x, double y, double z, ItemStack stack) {
        super(EntityType.UNKNOWN, world);
        setPosition(x, y, z);
        this.stack = stack == null ? ItemStack.EMPTY : stack;
    }
    public ItemEntity(World world, double x, double y, double z, ItemStack stack,
                      double velocityX, double velocityY, double velocityZ) {
        this(world, x, y, z, stack);
        setVelocity(velocityX, velocityY, velocityZ);
    }
    protected ItemEntity(ItemEntity source) {
        this(source == null ? null : source.getWorld(),
             source == null ? 0.0 : source.getX(),
             source == null ? 0.0 : source.getY(),
             source == null ? 0.0 : source.getZ(),
             source == null ? ItemStack.EMPTY : source.getStack().copy());
    }

    public ItemStack getStack() { return stack; }
    public void setStack(ItemStack value) { stack = value == null ? ItemStack.EMPTY : value; }
    public int getItemAge() { return itemAge; }
    public void setItemAge(int value) { itemAge = value; }
    public void setPickupDelay(int value) { pickupDelay = value; }
    public int getPickupDelay() { return pickupDelay; }
    public void setToDefaultPickupDelay() { pickupDelay = 10; }
    public void resetPickupDelay() { pickupDelay = 0; }
    public void setPickupDelayInfinite() { pickupDelay = CANNOT_PICK_UP_DELAY; }
    public void setDespawnImmediately() { itemAge = DESPAWN_AGE; }
    public void setNeverDespawn() { itemAge = NEVER_DESPAWN_AGE; }
    public boolean cannotPickup() { return pickupDelay > 0; }
    public void setThrower(Entity value) { thrower = value; throwerUuid = value == null ? null : value.getUuid(); }
    public Entity getThrower() { return thrower; }
    public void setOwner(UUID value) { owner = value; }
    public UUID getOwner() { return owner; }
    public boolean canMerge() { return !isRemoved() && !getStack().isEmpty(); }
    public static boolean canMerge(ItemStack left, ItemStack right) {
        return ItemStack.canCombine(left, right);
    }
    public boolean method_20396(ItemEntity other) { return canMerge() && other != null && other.canMerge(); }
    public void tryMerge() { }
    public void tryMerge(ItemEntity other) { }
    public void tick() { if (itemAge >= 0) itemAge++; }
    public ItemEntity copy() { return new ItemEntity(this); }
}
