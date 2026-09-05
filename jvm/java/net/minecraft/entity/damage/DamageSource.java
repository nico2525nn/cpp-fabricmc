package net.minecraft.entity.damage;

import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.text.Text;

/** Canonical package view of a damage source used by the shadow entity ABI. */
public class DamageSource {
    private final String name;
    private final Entity source;
    private final Entity attacker;

    public DamageSource(String name) { this(name, null, null); }
    public DamageSource(String name, Entity attacker) { this(name, attacker, attacker); }
    public DamageSource(String name, Entity source, Entity attacker) {
        this.name = name == null ? "generic" : name;
        this.source = source;
        this.attacker = attacker;
    }

    public String getName() { return name; }
    public Entity getSource() { return source; }
    public Entity getAttacker() { return attacker; }
    public boolean isDirect() { return source != null && source == attacker; }
    public boolean isSourceCreativePlayer() { return false; }
    public float getExhaustion() { return 0.1f; }
    public Text getDeathMessage(LivingEntity victim) {
        return Text.translatable("death.attack." + name, victim == null ? "" : victim.getName());
    }

    @Override public String toString() { return name; }
}
