package net.minecraft.entity.mob;

import net.minecraft.entity.EntityType;
import net.minecraft.world.World;

/** Base type for ambient mobs such as bats. */
public class AmbientEntity extends MobEntity {
    protected AmbientEntity(EntityType<?> type, World world) { super(type, world); }
    protected AmbientEntity() { super(); }
}
