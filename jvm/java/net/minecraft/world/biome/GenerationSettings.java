package net.minecraft.world.biome;

import java.util.ArrayList;
import java.util.Collections;
import java.util.EnumMap;
import java.util.List;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.world.gen.GenerationStep;
import net.minecraft.world.gen.carver.ConfiguredCarver;
import net.minecraft.world.gen.feature.PlacedFeature;

/** Mutable generation lists used while Fabric biome modifiers are applied. */
public class GenerationSettings {
    private final EnumMap<GenerationStep.Feature, List<RegistryEntry<PlacedFeature>>> features =
        new EnumMap<>(GenerationStep.Feature.class);
    private final List<RegistryEntry<ConfiguredCarver<?>>> carvers = new ArrayList<>();

    public GenerationSettings() { }

    public List<List<RegistryEntry<PlacedFeature>>> getFeatures() {
        List<List<RegistryEntry<PlacedFeature>>> result = new ArrayList<>();
        for (GenerationStep.Feature step : GenerationStep.Feature.values()) {
            result.add(List.copyOf(features.getOrDefault(step, List.of())));
        }
        return Collections.unmodifiableList(result);
    }

    public List<RegistryEntry<PlacedFeature>> getFeatures(GenerationStep.Feature step) {
        return List.copyOf(features.getOrDefault(step, List.of()));
    }

    public List<RegistryEntry<ConfiguredCarver<?>>> getCarvers() { return List.copyOf(carvers); }

    public void addFeature(GenerationStep.Feature step, RegistryEntry<PlacedFeature> feature) {
        if (step != null && feature != null)
            features.computeIfAbsent(step, ignored -> new ArrayList<>()).add(feature);
    }

    public boolean removeFeature(GenerationStep.Feature step, RegistryKey<PlacedFeature> key) {
        List<RegistryEntry<PlacedFeature>> values = features.get(step);
        if (values == null || key == null) return false;
        return values.removeIf(entry -> key.equals(entry.registryKey()));
    }

    public void addCarver(RegistryEntry<ConfiguredCarver<?>> carver) { if (carver != null) carvers.add(carver); }

    public boolean removeCarver(RegistryKey<ConfiguredCarver<?>> key) {
        if (key == null) return false;
        return carvers.removeIf(entry -> key.equals(entry.registryKey()));
    }
}
