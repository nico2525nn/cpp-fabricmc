package net.minecraft.item;

/** Tool material marker used by MiningToolItem constructors. */
public interface ToolMaterial {
    default int getDurability() { return 0; }
    default float getMiningSpeedMultiplier() { return 1.0f; }
    default float getAttackDamage() { return 0.0f; }
}
