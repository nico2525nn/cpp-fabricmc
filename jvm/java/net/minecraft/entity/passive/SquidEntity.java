package net.minecraft.entity.passive;

import net.minecraft.entity.EntityType;
import net.minecraft.util.NativeAccess;
import net.minecraft.world.World;

/** Vanilla-compatible squid hierarchy anchor backed by a native entity. */
public class SquidEntity extends WaterAnimalEntity {
    public static SquidEntity createNativeWrapper(long handle) {
        String typeId = NativeAccess.entityType(handle);
        return new SquidEntity(handle,
            World.of(NativeAccess.entityWorld(handle)), EntityType.byId(typeId));
    }

    protected SquidEntity(EntityType<?> type, World world) { super(type, world); }
    protected SquidEntity(long nativeHandle, World world, EntityType<?> type) {
        super(nativeHandle, world, type);
    }
    protected SquidEntity() { super(); }
}
