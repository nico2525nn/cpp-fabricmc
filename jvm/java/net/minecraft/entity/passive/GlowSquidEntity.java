package net.minecraft.entity.passive;

import net.minecraft.entity.EntityType;
import net.minecraft.util.NativeAccess;
import net.minecraft.world.World;

/** Vanilla-compatible glow-squid subtype backed by a native entity. */
public class GlowSquidEntity extends SquidEntity {
    public static GlowSquidEntity createNativeWrapper(long handle) {
        String typeId = NativeAccess.entityType(handle);
        return new GlowSquidEntity(handle,
            World.of(NativeAccess.entityWorld(handle)), EntityType.byId(typeId));
    }

    protected GlowSquidEntity(EntityType<?> type, World world) { super(type, world); }
    protected GlowSquidEntity(long nativeHandle, World world, EntityType<?> type) {
        super(nativeHandle, world, type);
    }
    protected GlowSquidEntity() { super(); }
}
