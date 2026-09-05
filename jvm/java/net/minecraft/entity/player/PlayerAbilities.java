package net.minecraft.entity.player;

public class PlayerAbilities {
    public boolean invulnerable;
    public boolean flying;
    public boolean allowFlying;
    public boolean creativeMode;
    public float flySpeed = 0.05f;
    public float walkSpeed = 0.1f;
    public void setFlySpeed(float value) { flySpeed = value; }
    public void setWalkSpeed(float value) { walkSpeed = value; }
}
