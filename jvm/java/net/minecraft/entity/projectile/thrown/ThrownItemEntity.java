package net.minecraft.entity.projectile.thrown;

import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.LivingEntity;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.item.Items;
import net.minecraft.world.World;

/**
 * 1.21.4 base class for throwable item projectiles.
 *
 * The native server owns projectile physics.  This handle-free shadow keeps
 * the item/owner state and the exact constructors used by Fabric mods and
 * mixins, so those mods can load without being given a misleading Java-only
 * simulation.
 */
public class ThrownItemEntity extends Entity {
    protected ItemStack item = ItemStack.EMPTY;
    protected LivingEntity owner;

    protected ThrownItemEntity(EntityType<? extends ThrownItemEntity> type, World world) {
        super(type, world);
    }

    public ThrownItemEntity(EntityType<? extends ThrownItemEntity> type,
                            double x, double y, double z, World world,
                            ItemStack stack) {
        super(type, world);
        setPosition(x, y, z);
        setItem(stack);
    }

    public ThrownItemEntity(EntityType<? extends ThrownItemEntity> type,
                            LivingEntity owner, World world, ItemStack stack) {
        super(type, world);
        this.owner = owner;
        if (owner != null) {
            setPosition(owner.getX(), owner.getY() + owner.getStandingEyeHeight(), owner.getZ());
        }
        setItem(stack);
    }

    protected ThrownItemEntity(long nativeHandle) {
        super(nativeHandle);
    }

    public ItemStack getItemStack() { return item; }

    public void setItem(ItemStack stack) {
        item = stack == null ? ItemStack.EMPTY : stack;
    }

    public Item getDefaultItem() { return Items.AIR; }

    public LivingEntity getOwner() { return owner; }

    public void setOwner(LivingEntity value) { owner = value; }
}
