package net.minecraft.entity.mob;

import net.minecraft.entity.EntityType;
import net.minecraft.world.World;

/** Vanilla 1.21.4 water-creature hierarchy anchor. */
public class WaterCreatureEntity extends PathAwareEntity {
    protected WaterCreatureEntity(EntityType<?> type, World world) {
        super(type, world);
    }

    protected WaterCreatureEntity(long nativeHandle, World world,
                                  EntityType<?> type) {
        super(nativeHandle, world, type);
    }

    protected WaterCreatureEntity() {
        super();
    }
}
