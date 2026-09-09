package net.minecraft.sound;

/** Music selection descriptor used by biome effects. */
public record MusicSound(SoundEvent sound, int minDelay, int maxDelay, boolean replaceCurrentMusic) { }
