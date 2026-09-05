package net.minecraft.entity;

import net.minecraft.text.Text;

public class DamageSource {
    private final String name;
    private final Entity attacker;
    public DamageSource(String name) { this(name, null); }
    public DamageSource(String name, Entity attacker) { this.name = name == null ? "generic" : name; this.attacker = attacker; }
    public String getName() { return name; }
    public Entity getAttacker() { return attacker; }
    public Entity getSource() { return attacker; }
    public Text getDeathMessage(LivingEntity victim) { return Text.translatable("death.attack." + name, victim == null ? "" : victim.getName()); }
    @Override public String toString() { return name; }
}
