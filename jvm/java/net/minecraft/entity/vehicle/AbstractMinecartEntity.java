package net.minecraft.entity.vehicle;

import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityType;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;

/** Handle-backed minecart base required by the 1.21.4 controller ABI. */
public class AbstractMinecartEntity extends Entity {
    protected AbstractMinecartEntity(EntityType<?> type, World world) { super(type, world); }
    protected AbstractMinecartEntity() { super((EntityType<?>) null, (World) null); }

    public Entity getMinecartEntity() { return this; }
    public BlockPos getRailOrMinecartPos() { return getBlockPos(); }
}
