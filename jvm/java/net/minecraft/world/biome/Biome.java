package net.minecraft.world.biome;

/**
 * Minimal but stateful named biome value.  It preserves the builder and
 * accessor contract used by Fabric biome selectors; native world generation
 * remains authoritative for actual chunk production.
 */
public class Biome {
    private Weather weather;
    private BiomeEffects effects;
    private GenerationSettings generationSettings;
    private SpawnSettings spawnSettings;

    public Biome() { this(new Weather(true, 0.8f, TemperatureModifier.NONE, 0.4f), new BiomeEffects(), null, new SpawnSettings()); }
    public Biome(Weather weather, BiomeEffects effects,
                 GenerationSettings generationSettings,
                 SpawnSettings spawnSettings) {
        this.weather = weather == null ? new Weather(true, 0.8f, TemperatureModifier.NONE, 0.4f) : weather;
        this.effects = effects == null ? new BiomeEffects() : effects;
        this.generationSettings = generationSettings == null ? new GenerationSettings() : generationSettings;
        this.spawnSettings = spawnSettings == null ? new SpawnSettings() : spawnSettings;
    }
    public Weather getWeather() { return weather; }
    public BiomeEffects getEffects() { return effects; }
    public SpawnSettings getSpawnSettings() { return spawnSettings; }
    public GenerationSettings getGenerationSettings() { return generationSettings; }
    public float getTemperature() { return weather.temperature(); }
    public boolean hasPrecipitation() { return weather.hasPrecipitation(); }
    public float getDownfall() { return weather.downfall(); }
    public TemperatureModifier getTemperatureModifier() { return weather.temperatureModifier(); }
    public BiomeEffects getSpecialEffects() { return effects; }
    public void setWeather(Weather value) { if (value != null) weather = value; }
    public void setEffects(BiomeEffects value) { if (value != null) effects = value; }
    public void setGenerationSettings(GenerationSettings value) { if (value != null) generationSettings = value; }
    public void setSpawnSettings(SpawnSettings value) { if (value != null) spawnSettings = value; }

    public enum Precipitation { RAIN, SNOW, NONE }
    public enum TemperatureModifier { NONE, FROZEN }
    public record Weather(boolean hasPrecipitation, float temperature,
                          TemperatureModifier temperatureModifier, float downfall) { }

    public static class Builder {
        private boolean precipitation = true;
        private float temperature = 0.8f;
        private TemperatureModifier temperatureModifier = TemperatureModifier.NONE;
        private float downfall = 0.4f;
        private BiomeEffects effects = new BiomeEffects();
        private GenerationSettings generationSettings = new GenerationSettings();
        private SpawnSettings spawnSettings = new SpawnSettings();
        public Builder precipitation(boolean value) { precipitation = value; return this; }
        public Builder temperature(float value) { temperature = value; return this; }
        public Builder temperatureModifier(TemperatureModifier value) { temperatureModifier = value == null ? TemperatureModifier.NONE : value; return this; }
        public Builder downfall(float value) { downfall = value; return this; }
        public Builder effects(BiomeEffects value) { effects = value; return this; }
        public Builder generationSettings(GenerationSettings value) { generationSettings = value; return this; }
        public Builder spawnSettings(SpawnSettings value) { spawnSettings = value; return this; }
        public Biome build() { return new Biome(new Weather(precipitation, temperature, temperatureModifier, downfall), effects, generationSettings, spawnSettings); }
    }
}
