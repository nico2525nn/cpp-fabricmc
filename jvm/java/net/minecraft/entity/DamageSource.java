package net.minecraft.entity;

/** @deprecated use {@link net.minecraft.entity.damage.DamageSource}. */
@Deprecated
public class DamageSource extends net.minecraft.entity.damage.DamageSource {
    public DamageSource(String name) { super(name); }
    public DamageSource(String name, Entity attacker) { super(name, attacker); }
    public DamageSource(String name, Entity source, Entity attacker) { super(name, source, attacker); }
}
