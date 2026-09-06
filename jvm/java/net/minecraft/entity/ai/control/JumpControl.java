package net.minecraft.entity.ai.control;

import net.minecraft.entity.mob.MobEntity;

/** Jump control ABI. */
public class JumpControl {
    protected final MobEntity entity;
    public JumpControl(MobEntity entity) { this.entity = entity; }
    public void tick() { }
    public void setActive() { }
    public boolean isActive() { return false; }
}
