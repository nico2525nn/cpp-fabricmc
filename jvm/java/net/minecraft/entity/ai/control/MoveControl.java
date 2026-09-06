package net.minecraft.entity.ai.control;

import net.minecraft.entity.mob.MobEntity;

/** Movement control ABI. */
public class MoveControl {
    protected final MobEntity entity;
    public MoveControl(MobEntity entity) { this.entity = entity; }
    public void tick() { }
}
