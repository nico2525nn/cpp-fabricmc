package net.fabricmc.fabric.api.item.v1;

import net.minecraft.entity.EquipmentSlot;
import net.minecraft.entity.LivingEntity;
import net.minecraft.item.ItemStack;

@FunctionalInterface
public interface EquipmentSlotProvider {
    EquipmentSlot getPreferredEquipmentSlot(LivingEntity entity, ItemStack stack);
}
