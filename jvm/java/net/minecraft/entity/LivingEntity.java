package net.minecraft.entity;

public class LivingEntity extends Entity {
    protected LivingEntity(long nativeHandle) { super(nativeHandle); }
    public float getHealth() { return 20.0f; }
    public float getMaxHealth() { return 20.0f; }
    public boolean damage(Object source, float amount) { return amount > 0.0f; }
}
