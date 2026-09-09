package net.minecraft.world.biome;

import java.util.List;
import java.util.Optional;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.sound.BiomeAdditionsSound;
import net.minecraft.sound.BiomeMoodSound;
import net.minecraft.sound.MusicSound;
import net.minecraft.util.collection.DataPool;

/** Mutable-builder/value shell for biome visual and audio effects. */
public final class BiomeEffects {
    private int fogColor;
    private int waterColor;
    private int waterFogColor;
    private int skyColor;
    private Optional<Integer> foliageColor = Optional.empty();
    private Optional<Integer> grassColor = Optional.empty();
    private GrassColorModifier grassColorModifier = GrassColorModifier.NONE;
    private Optional<BiomeParticleConfig> particleConfig = Optional.empty();
    private Optional<RegistryEntry<net.minecraft.sound.SoundEvent>> ambientSound = Optional.empty();
    private Optional<BiomeMoodSound> moodSound = Optional.empty();
    private Optional<BiomeAdditionsSound> additionsSound = Optional.empty();
    private Optional<DataPool<MusicSound>> music = Optional.empty();
    private float musicVolume = 1.0f;

    public BiomeEffects() { }
    public int fogColor() { return fogColor; }
    public int waterColor() { return waterColor; }
    public int waterFogColor() { return waterFogColor; }
    public int skyColor() { return skyColor; }
    public Optional<Integer> foliageColor() { return foliageColor; }
    public Optional<Integer> grassColor() { return grassColor; }
    public GrassColorModifier grassColorModifier() { return grassColorModifier; }
    public Optional<BiomeParticleConfig> particleConfig() { return particleConfig; }
    public Optional<RegistryEntry<net.minecraft.sound.SoundEvent>> ambientSound() { return ambientSound; }
    public Optional<RegistryEntry<net.minecraft.sound.SoundEvent>> getLoopSound() { return ambientSound; }
    public Optional<BiomeMoodSound> moodSound() { return moodSound; }
    public Optional<BiomeMoodSound> getMoodSound() { return moodSound; }
    public Optional<BiomeAdditionsSound> additionsSound() { return additionsSound; }
    public Optional<BiomeAdditionsSound> getAdditionsSound() { return additionsSound; }
    public Optional<DataPool<MusicSound>> music() { return music; }
    public Optional<DataPool<MusicSound>> getMusic() { return music; }
    public int getFogColor() { return fogColor; }
    public int getWaterColor() { return waterColor; }
    public int getWaterFogColor() { return waterFogColor; }
    public int getSkyColor() { return skyColor; }
    public Optional<Integer> getFoliageColor() { return foliageColor; }
    public Optional<Integer> getGrassColor() { return grassColor; }
    public GrassColorModifier getGrassColorModifier() { return grassColorModifier; }
    public Optional<BiomeParticleConfig> getParticleConfig() { return particleConfig; }
    public float musicVolume() { return musicVolume; }
    public float getMusicVolume() { return musicVolume; }

    public void setFogColor(int value) { fogColor = value; }
    public void setWaterColor(int value) { waterColor = value; }
    public void setWaterFogColor(int value) { waterFogColor = value; }
    public void setSkyColor(int value) { skyColor = value; }
    public void setFoliageColor(Optional<Integer> value) { foliageColor = value == null ? Optional.empty() : value; }
    public void setGrassColor(Optional<Integer> value) { grassColor = value == null ? Optional.empty() : value; }
    public void setGrassColorModifier(GrassColorModifier value) { grassColorModifier = value == null ? GrassColorModifier.NONE : value; }
    public void setParticleConfig(Optional<BiomeParticleConfig> value) { particleConfig = value == null ? Optional.empty() : value; }
    public void setAmbientSound(Optional<RegistryEntry<net.minecraft.sound.SoundEvent>> value) { ambientSound = value == null ? Optional.empty() : value; }
    public void setMoodSound(Optional<BiomeMoodSound> value) { moodSound = value == null ? Optional.empty() : value; }
    public void setAdditionsSound(Optional<BiomeAdditionsSound> value) { additionsSound = value == null ? Optional.empty() : value; }
    public void setMusic(Optional<DataPool<MusicSound>> value) { music = value == null ? Optional.empty() : value; }
    public void setMusicVolume(float value) { musicVolume = value; }

    public enum GrassColorModifier { NONE, DARK_FOREST, SWAMP }

    public static final class Builder {
        private final BiomeEffects effects = new BiomeEffects();
        public Builder fogColor(int value) { effects.fogColor = value; return this; }
        public Builder waterColor(int value) { effects.waterColor = value; return this; }
        public Builder waterFogColor(int value) { effects.waterFogColor = value; return this; }
        public Builder skyColor(int value) { effects.skyColor = value; return this; }
        public Builder foliageColor(int value) { effects.foliageColor = Optional.of(value); return this; }
        public Builder grassColor(int value) { effects.grassColor = Optional.of(value); return this; }
        public Builder grassColorModifier(GrassColorModifier value) { effects.grassColorModifier = value == null ? GrassColorModifier.NONE : value; return this; }
        public Builder particleConfig(BiomeParticleConfig value) { effects.particleConfig = Optional.ofNullable(value); return this; }
        public Builder ambientSound(RegistryEntry<net.minecraft.sound.SoundEvent> value) { effects.ambientSound = Optional.ofNullable(value); return this; }
        public Builder loopSound(RegistryEntry<net.minecraft.sound.SoundEvent> value) { return ambientSound(value); }
        public Builder moodSound(BiomeMoodSound value) { effects.moodSound = Optional.ofNullable(value); return this; }
        public Builder additionsSound(BiomeAdditionsSound value) { effects.additionsSound = Optional.ofNullable(value); return this; }
        public Builder music(MusicSound value) {
            effects.music = value == null ? Optional.empty() : Optional.of(new DataPool<>(List.of(value)));
            return this;
        }
        public Builder music(DataPool<MusicSound> value) { effects.music = Optional.ofNullable(value); return this; }
        public Builder musicVolume(float value) { effects.musicVolume = value; return this; }
        public BiomeEffects build() { return effects; }
    }
}
