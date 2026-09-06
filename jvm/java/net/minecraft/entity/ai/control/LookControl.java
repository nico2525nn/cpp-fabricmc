package net.minecraft.entity.ai.control;

import net.minecraft.entity.Entity;
import net.minecraft.entity.mob.MobEntity;

/** Look control ABI. */
public class LookControl {
    protected final MobEntity entity;
    public LookControl(MobEntity entity) { this.entity = entity; }
    public void tick() { }
    public void lookAt(Entity target) { }
}
