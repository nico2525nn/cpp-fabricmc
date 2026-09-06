package net.minecraft.entity.decoration;

import net.minecraft.entity.EntityType;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.collection.DefaultedList;
import net.minecraft.world.World;

/** Server-side ArmorStand shell used by Carpet's marker persistence hook. */
public class ArmorStandEntity extends LivingEntity {
    private final DefaultedList<ItemStack> equipment = DefaultedList.ofSize(2, ItemStack.EMPTY);
    public ArmorStandEntity(EntityType<?> type, World world) { super(type, world); }
    protected ArmorStandEntity(long nativeHandle) { super(nativeHandle); }
    public void readAdditionalSaveData(NbtCompound nbt) { }
    public void readCustomDataFromNbt(NbtCompound nbt) {
        equipment.set(0, ItemStack.EMPTY);
        equipment.set(1, ItemStack.EMPTY);
        readAdditionalSaveData(nbt);
    }
    public void writeCustomDataToNbt(NbtCompound nbt) { }
    public void tickCramming() { }
    public void onBreak(ServerWorld world, DamageSource source) {
        equipment.set(0, ItemStack.EMPTY);
        equipment.set(1, ItemStack.EMPTY);
    }
}
