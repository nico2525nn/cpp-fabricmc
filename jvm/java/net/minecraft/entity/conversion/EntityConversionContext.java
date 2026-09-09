package net.minecraft.entity.conversion;

import net.minecraft.entity.mob.MobEntity;

/** Immutable conversion options passed through the 1.21.4 mob-conversion ABI. */
public final class EntityConversionContext {
    private final boolean keepEquipment;
    private final boolean preserveCanPickUpLoot;
    private final Object team;

    public EntityConversionContext() { this(false, false, null); }
    public EntityConversionContext(boolean keepEquipment, boolean preserveCanPickUpLoot, Object team) {
        this.keepEquipment = keepEquipment;
        this.preserveCanPickUpLoot = preserveCanPickUpLoot;
        this.team = team;
    }
    public boolean keepEquipment() { return keepEquipment; }
    public boolean preserveCanPickUpLoot() { return preserveCanPickUpLoot; }
    public Object team() { return team; }

    @FunctionalInterface
    public interface Finalizer {
        void finalizeConversion(MobEntity convertedEntity);
    }
}
