package net.minecraft.entity;

public enum Pose {
    STANDING, FALL_FLYING, SLEEPING, SWIMMING, SPIN_ATTACK, CROUCHING, LONG_JUMPING, DYING, CROAKING, DIGGING, USING_TONGUE;
    EntityPose toEntityPose() { return EntityPose.valueOf(name()); }
}
