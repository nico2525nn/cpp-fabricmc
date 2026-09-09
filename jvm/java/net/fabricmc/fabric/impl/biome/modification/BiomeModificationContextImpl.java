package net.fabricmc.fabric.impl.biome.modification;

import java.util.Optional;
import java.util.function.BiPredicate;
import net.fabricmc.fabric.api.biome.v1.BiomeModificationContext;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.SpawnGroup;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.registry.RegistryKey;
import net.minecraft.sound.BiomeAdditionsSound;
import net.minecraft.sound.BiomeMoodSound;
import net.minecraft.sound.MusicSound;
import net.minecraft.sound.SoundEvent;
import net.minecraft.util.collection.DataPool;
import net.minecraft.world.biome.Biome;
import net.minecraft.world.biome.BiomeEffects;
import net.minecraft.world.biome.BiomeParticleConfig;
import net.minecraft.world.biome.GenerationSettings;
import net.minecraft.world.biome.SpawnSettings;
import net.minecraft.world.gen.GenerationStep;
import net.minecraft.world.gen.carver.ConfiguredCarver;
import net.minecraft.world.gen.feature.PlacedFeature;

/** Applies Fabric biome mutations directly to a stateful shadow biome. */
public final class BiomeModificationContextImpl implements BiomeModificationContext {
    private final Biome biome;
    private final WeatherContext weather = new WeatherImpl();
    private final EffectsContext effects = new EffectsImpl();
    private final GenerationSettingsContext generation = new GenerationImpl();
    private final SpawnSettingsContext spawns = new SpawnsImpl();

    public BiomeModificationContextImpl(Biome biome) { this.biome = biome; }
    @Override public WeatherContext getWeather() { return weather; }
    @Override public EffectsContext getEffects() { return effects; }
    @Override public GenerationSettingsContext getGenerationSettings() { return generation; }
    @Override public SpawnSettingsContext getSpawnSettings() { return spawns; }

    private final class WeatherImpl implements WeatherContext {
        @Override public void setPrecipitation(boolean value) { set(weather(value, biome.getTemperature(), biome.getTemperatureModifier(), biome.getDownfall())); }
        @Override public void setTemperature(float value) { set(weather(biome.hasPrecipitation(), value, biome.getTemperatureModifier(), biome.getDownfall())); }
        @Override public void setTemperatureModifier(Biome.TemperatureModifier value) { set(weather(biome.hasPrecipitation(), biome.getTemperature(), value, biome.getDownfall())); }
        @Override public void setDownfall(float value) { set(weather(biome.hasPrecipitation(), biome.getTemperature(), biome.getTemperatureModifier(), value)); }
        private Biome.Weather weather(boolean precipitation, float temperature, Biome.TemperatureModifier modifier, float downfall) {
            return new Biome.Weather(precipitation, temperature, modifier == null ? Biome.TemperatureModifier.NONE : modifier, downfall);
        }
        private void set(Biome.Weather value) { biome.setWeather(value); }
    }

    private final class EffectsImpl implements EffectsContext {
        private BiomeEffects value() { return biome.getEffects(); }
        @Override public void setFogColor(int value) { value().setFogColor(value); }
        @Override public void setWaterColor(int value) { value().setWaterColor(value); }
        @Override public void setWaterFogColor(int value) { value().setWaterFogColor(value); }
        @Override public void setSkyColor(int value) { value().setSkyColor(value); }
        @Override public void setFoliageColor(Optional<Integer> value) { value().setFoliageColor(value); }
        @Override public void setGrassColor(Optional<Integer> value) { value().setGrassColor(value); }
        @Override public void setGrassColorModifier(BiomeEffects.GrassColorModifier value) { value().setGrassColorModifier(value); }
        @Override public void setParticleConfig(Optional<BiomeParticleConfig> value) { value().setParticleConfig(value); }
        @Override public void setAmbientSound(Optional<RegistryEntry<SoundEvent>> value) { value().setAmbientSound(value); }
        @Override public void setMoodSound(Optional<BiomeMoodSound> value) { value().setMoodSound(value); }
        @Override public void setAdditionsSound(Optional<BiomeAdditionsSound> value) { value().setAdditionsSound(value); }
        @Override public void setMusic(Optional<DataPool<MusicSound>> value) { value().setMusic(value); }
        @Override public void setMusicVolume(float value) { value().setMusicVolume(value); }
    }

    private final class GenerationImpl implements GenerationSettingsContext {
        private GenerationSettings value() { return biome.getGenerationSettings(); }
        @Override public boolean removeFeature(GenerationStep.Feature step, RegistryKey<PlacedFeature> key) { return value().removeFeature(step, key); }
        @Override public void addFeature(GenerationStep.Feature step, RegistryKey<PlacedFeature> key) {
            if (key != null) value().addFeature(step, RegistryEntry.of(key, new PlacedFeature()));
        }
        @Override public void addCarver(RegistryKey<ConfiguredCarver<?>> key) {
            if (key != null) value().addCarver(RegistryEntry.of(key, new ConfiguredCarver<>()));
        }
        @Override public boolean removeCarver(RegistryKey<ConfiguredCarver<?>> key) { return value().removeCarver(key); }
    }

    private final class SpawnsImpl implements SpawnSettingsContext {
        private SpawnSettings value() { return biome.getSpawnSettings(); }
        @Override public void setCreatureSpawnProbability(float probability) { value().setCreatureSpawnProbability(probability); }
        @Override public void addSpawn(SpawnGroup group, SpawnSettings.SpawnEntry entry) { value().addSpawn(group, entry); }
        @Override public boolean removeSpawns(BiPredicate<SpawnGroup, SpawnSettings.SpawnEntry> predicate) { return value().removeSpawns(predicate); }
        @Override public void setSpawnCost(EntityType<?> type, double mass, double gravityLimit) { value().setSpawnCost(type, mass, gravityLimit); }
        @Override public void clearSpawnCost(EntityType<?> type) { value().clearSpawnCost(type); }
    }
}
