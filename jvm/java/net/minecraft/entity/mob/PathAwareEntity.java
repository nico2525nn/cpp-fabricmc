package net.minecraft.entity.mob;

import net.minecraft.entity.EntityType;
import net.minecraft.world.World;

/** Path-aware mob ABI used by vanilla goal constructors. */
public class PathAwareEntity extends MobEntity {
    protected PathAwareEntity(EntityType<?> type, World world) { super(type, world); }
    protected PathAwareEntity(long nativeHandle, World world, EntityType<?> type) { super(nativeHandle, world, type); }
    protected PathAwareEntity() { super(); }
}
