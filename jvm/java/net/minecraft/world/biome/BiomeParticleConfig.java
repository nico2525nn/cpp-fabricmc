package net.minecraft.world.biome;

import net.minecraft.particle.ParticleEffect;

/** Particle and probability pair used by biome effects. */
public record BiomeParticleConfig(ParticleEffect particle, float probability) { }
