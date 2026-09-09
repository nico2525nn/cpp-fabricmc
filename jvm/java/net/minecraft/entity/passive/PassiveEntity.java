package net.minecraft.entity.passive;

import net.minecraft.entity.EntityType;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.world.World;

/** Passive mob base used by Fabric entity-event and ServerCore mixins. */
public class PassiveEntity extends MobEntity {
    private int breedingAge;

    protected PassiveEntity(EntityType<?> type, World world) { super(type, world); }
    protected PassiveEntity(long nativeHandle, World world, EntityType<?> type) {
        super(nativeHandle, world, type);
    }
    protected PassiveEntity() { super(); }

    public int getBreedingAge() { return breedingAge; }
    public void setBreedingAge(int age) { breedingAge = age; }
    public int method_5618() { return breedingAge; }
    public void method_5614(int age) { breedingAge = age; }
    /** Tracked-data callback retained for Lithium's parent-animal sensor mixin. */
    public void onTrackedDataSet(net.minecraft.entity.data.TrackedData<?> data) {
        // Vanilla refreshes the entity dimensions after tracked pose changes.
        // Keeping the call in the target method is important: Lithium injects
        // at this INVOKE site rather than at the method head.
        calculateDimensions();
    }
    /** Intermediary spelling of the 1.21.4 tracked-data callback. */
    public void method_5674(net.minecraft.entity.data.TrackedData<?> data) {
        onTrackedDataSet(data);
    }
    /** Dimensions refresh anchor used by Lithium's parent-animal sensor mixin. */
    public void calculateDimensions() { }
    /** Intermediary spelling used by Lithium's INVOKE target. */
    public void method_18382() { calculateDimensions(); }
}
