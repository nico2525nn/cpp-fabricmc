package net.minecraft.entity.mob;

import net.minecraft.entity.EntityType;
import net.minecraft.entity.attribute.DefaultAttributeContainer;
import net.minecraft.world.World;

/** Elder guardian ABI referenced by Carpet's mob mixin set. */
public class ElderGuardianEntity extends HostileEntity {
    public ElderGuardianEntity(EntityType<?> type, World world) { super(type, world); }
    protected ElderGuardianEntity(long nativeHandle, World world, EntityType<?> type) { super(nativeHandle, world, type); }
    protected ElderGuardianEntity() { super(); }
    public static DefaultAttributeContainer.Builder createElderGuardianAttributes() {
        return DefaultAttributeContainer.builder();
    }
}
