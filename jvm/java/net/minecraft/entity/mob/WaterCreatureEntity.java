package net.minecraft.entity.mob;

import net.minecraft.entity.EntityType;
import net.minecraft.world.World;

/** Base type for water creatures in the 1.21.4 entity hierarchy. */
public class WaterCreatureEntity extends MobEntity {
    protected WaterCreatureEntity(EntityType<?> type, World world) { super(type, world); }
    protected WaterCreatureEntity() { super(); }
}
