package net.minecraft.sound;

/** Ambient mood sound descriptor for a biome. */
public record BiomeMoodSound(SoundEvent sound, int tickDelay, int blockSearchExtent, double offset) { }
