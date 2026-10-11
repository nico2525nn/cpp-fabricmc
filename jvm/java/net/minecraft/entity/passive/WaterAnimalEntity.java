package net.minecraft.entity.passive;

import net.minecraft.entity.EntityType;
import net.minecraft.world.World;

/** Vanilla 1.21.4 parent for aquatic animals such as squids. */
public class WaterAnimalEntity extends PassiveEntity {
    protected WaterAnimalEntity(EntityType<?> type, World world) {
        super(type, world);
    }

    protected WaterAnimalEntity(long nativeHandle, World world,
                                EntityType<?> type) {
        super(nativeHandle, world, type);
    }

    protected WaterAnimalEntity() {
        super();
    }
}
