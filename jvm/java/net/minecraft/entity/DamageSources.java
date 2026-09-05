package net.minecraft.entity;

public final class DamageSources {
    private DamageSources() {}
    public static DamageSource generic() { return new DamageSource("generic"); }
    public static DamageSource playerAttack(Entity player) { return new DamageSource("player", player); }
    public static DamageSource mobAttack(Entity attacker) { return new DamageSource("mob", attacker); }
}
