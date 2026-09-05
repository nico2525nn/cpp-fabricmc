package net.minecraft.entity.damage;

import net.minecraft.entity.Entity;

/** Factory for the bounded damage-source values exposed by the shadow layer. */
public class DamageSources {
    public DamageSources() {}
    public DamageSource generic() { return new DamageSource("generic"); }
    public DamageSource playerAttack(Entity player) { return new DamageSource("player", player); }
    public DamageSource mobAttack(Entity attacker) { return new DamageSource("mob", attacker); }
    public DamageSource outsideBorder() { return new DamageSource("outsideBorder"); }
    public DamageSource inFire() { return new DamageSource("inFire"); }
    public DamageSource lava() { return new DamageSource("lava"); }
    public DamageSource fall() { return new DamageSource("fall"); }
}
