package net.fabricmc.fabric.api.biome.v1;

import java.util.Optional;
import java.util.OptionalInt;
import java.util.function.BiPredicate;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.SpawnGroup;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.sound.BiomeAdditionsSound;
import net.minecraft.sound.BiomeMoodSound;
import net.minecraft.sound.MusicSound;
import net.minecraft.sound.SoundEvent;
import net.minecraft.util.collection.DataPool;
import net.minecraft.world.biome.Biome;
import net.minecraft.world.biome.BiomeEffects;
import net.minecraft.world.biome.BiomeParticleConfig;
import net.minecraft.world.biome.SpawnSettings;
import net.minecraft.world.gen.GenerationStep;
import net.minecraft.world.gen.carver.ConfiguredCarver;
import net.minecraft.world.gen.feature.PlacedFeature;

/** Mutable view of the weather, effects, generation and spawn sections of a biome. */
public interface BiomeModificationContext {
    WeatherContext getWeather();
    EffectsContext getEffects();
    GenerationSettingsContext getGenerationSettings();
    SpawnSettingsContext getSpawnSettings();

    interface WeatherContext {
        void setPrecipitation(boolean hasPrecipitation);
        void setTemperature(float temperature);
        void setTemperatureModifier(Biome.TemperatureModifier temperatureModifier);
        void setDownfall(float downfall);
    }

    interface EffectsContext {
        void setFogColor(int color);
        void setWaterColor(int color);
        void setWaterFogColor(int color);
        void setSkyColor(int color);
        void setFoliageColor(Optional<Integer> color);
        default void setFoliageColor(int color) { setFoliageColor(Optional.of(color)); }
        default void setFoliageColor(OptionalInt color) { color.ifPresentOrElse(this::setFoliageColor, this::clearFoliageColor); }
        default void clearFoliageColor() { setFoliageColor(Optional.empty()); }
        void setGrassColor(Optional<Integer> color);
        default void setGrassColor(int color) { setGrassColor(Optional.of(color)); }
        default void setGrassColor(OptionalInt color) { color.ifPresentOrElse(this::setGrassColor, this::clearGrassColor); }
        default void clearGrassColor() { setGrassColor(Optional.empty()); }
        void setGrassColorModifier(BiomeEffects.GrassColorModifier colorModifier);
        void setParticleConfig(Optional<BiomeParticleConfig> particleConfig);
        default void setParticleConfig(BiomeParticleConfig particleConfig) { setParticleConfig(Optional.of(particleConfig)); }
        default void clearParticleConfig() { setParticleConfig(Optional.empty()); }
        void setAmbientSound(Optional<RegistryEntry<SoundEvent>> sound);
        default void setAmbientSound(RegistryEntry<SoundEvent> sound) { setAmbientSound(Optional.of(sound)); }
        default void clearAmbientSound() { setAmbientSound(Optional.empty()); }
        void setMoodSound(Optional<BiomeMoodSound> sound);
        default void setMoodSound(BiomeMoodSound sound) { setMoodSound(Optional.of(sound)); }
        default void clearMoodSound() { setMoodSound(Optional.empty()); }
        void setAdditionsSound(Optional<BiomeAdditionsSound> sound);
        default void setAdditionsSound(BiomeAdditionsSound sound) { setAdditionsSound(Optional.of(sound)); }
        default void clearAdditionsSound() { setAdditionsSound(Optional.empty()); }
        void setMusic(Optional<DataPool<MusicSound>> sound);
        default void setMusic(DataPool<MusicSound> sound) { setMusic(Optional.of(sound)); }
        default void setMusic(MusicSound sound) { setMusic(Optional.of(new DataPool<>(sound))); }
        default void clearMusic() { setMusic(Optional.empty()); }
        void setMusicVolume(float volume);
    }

    interface GenerationSettingsContext {
        boolean removeFeature(GenerationStep.Feature step, RegistryKey<PlacedFeature> placedFeatureKey);
        default boolean removeFeature(RegistryKey<PlacedFeature> placedFeatureKey) {
            boolean removed = false;
            for (GenerationStep.Feature step : GenerationStep.Feature.values()) removed |= removeFeature(step, placedFeatureKey);
            return removed;
        }
        void addFeature(GenerationStep.Feature step, RegistryKey<PlacedFeature> placedFeatureKey);
        void addCarver(RegistryKey<ConfiguredCarver<?>> carverKey);
        boolean removeCarver(RegistryKey<ConfiguredCarver<?>> configuredCarverKey);
    }

    interface SpawnSettingsContext {
        void setCreatureSpawnProbability(float probability);
        void addSpawn(SpawnGroup spawnGroup, SpawnSettings.SpawnEntry spawnEntry);
        boolean removeSpawns(BiPredicate<SpawnGroup, SpawnSettings.SpawnEntry> predicate);
        default boolean removeSpawnsOfEntityType(EntityType<?> entityType) {
            return removeSpawns((group, entry) -> entry.type() == entityType);
        }
        default void clearSpawns(SpawnGroup group) { removeSpawns((spawnGroup, entry) -> spawnGroup == group); }
        default void clearSpawns() { removeSpawns((spawnGroup, entry) -> true); }
        void setSpawnCost(EntityType<?> entityType, double mass, double gravityLimit);
        void clearSpawnCost(EntityType<?> entityType);
    }
}
