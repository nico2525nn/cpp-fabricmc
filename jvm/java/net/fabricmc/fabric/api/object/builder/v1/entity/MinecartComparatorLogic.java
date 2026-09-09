package net.fabricmc.fabric.api.object.builder.v1.entity;

import net.minecraft.block.BlockState;
import net.minecraft.entity.vehicle.AbstractMinecartEntity;
import net.minecraft.util.math.BlockPos;

@FunctionalInterface
public interface MinecartComparatorLogic<T extends AbstractMinecartEntity> {
    int getComparatorValue(T minecart, BlockState state, BlockPos pos);
}
