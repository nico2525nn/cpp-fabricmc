package net.fabricmc.fabric.api.item.v1;

import net.minecraft.entity.EquipmentSlot;
import net.minecraft.entity.LivingEntity;
import net.minecraft.item.ItemStack;

@FunctionalInterface
public interface CustomDamageHandler {
    int damage(ItemStack stack, int amount, LivingEntity entity,
               EquipmentSlot slot, Runnable breakCallback);
}
