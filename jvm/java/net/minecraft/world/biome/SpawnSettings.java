package net.minecraft.world.biome;

import java.util.ArrayList;
import java.util.EnumMap;
import java.util.List;
import java.util.Map;
import java.util.HashMap;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.SpawnGroup;

/** Spawn lists and costs used by biome selectors and modification contexts. */
public final class SpawnSettings {
    public static final SpawnSettings INSTANCE = new SpawnSettings();
    private final EnumMap<SpawnGroup, List<SpawnEntry>> spawners = new EnumMap<>(SpawnGroup.class);
    private final Map<EntityType<?>, SpawnDensity> spawnCosts = new HashMap<>();
    private float creatureSpawnProbability = 0.1f;
    public SpawnSettings() { }
    public float creatureSpawnProbability() { return creatureSpawnProbability; }
    public float getCreatureSpawnProbability() { return creatureSpawnProbability; }
    public List<SpawnEntry> getSpawner(SpawnGroup group) { return List.copyOf(spawners.getOrDefault(group, List.of())); }
    public List<SpawnEntry> getSpawners(SpawnGroup group) { return getSpawner(group); }
    public SpawnDensity getSpawnDensity(EntityType<?> type) { return spawnCosts.get(type); }
    public static final class SpawnEntry {
        public final EntityType<?> type;
        public final int weight;
        public final int minGroupSize;
        public final int maxGroupSize;
        public SpawnEntry(EntityType<?> type, int weight, int minGroupSize, int maxGroupSize) {
            this.type = type; this.weight = weight; this.minGroupSize = minGroupSize; this.maxGroupSize = maxGroupSize;
        }
        public EntityType<?> type() { return type; }
        public int weight() { return weight; }
        public int minGroupSize() { return minGroupSize; }
        public int maxGroupSize() { return maxGroupSize; }
    }
    public record SpawnDensity(double mass, double gravityLimit) { }
    public static final class Builder {
        private final SpawnSettings value = new SpawnSettings();
        public Builder creatureSpawnProbability(float value) { this.value.creatureSpawnProbability = value; return this; }
        public Builder spawn(SpawnGroup group, SpawnEntry entry) { if (group != null && entry != null) value.spawners.computeIfAbsent(group, ignored -> new ArrayList<>()).add(entry); return this; }
        public Builder spawnCost(EntityType<?> type, double mass, double gravityLimit) {
            if (type != null) value.spawnCosts.put(type, new SpawnDensity(mass, gravityLimit));
            return this;
        }
        public SpawnSettings build() { return value; }
    }

    public void setCreatureSpawnProbability(float value) { creatureSpawnProbability = value; }
    public void addSpawn(SpawnGroup group, SpawnEntry entry) {
        if (group != null && entry != null) spawners.computeIfAbsent(group, ignored -> new ArrayList<>()).add(entry);
    }
    public boolean removeSpawns(java.util.function.BiPredicate<SpawnGroup, SpawnEntry> predicate) {
        boolean changed = false;
        if (predicate == null) return false;
        for (Map.Entry<SpawnGroup, List<SpawnEntry>> entry : spawners.entrySet())
            changed |= entry.getValue().removeIf(value -> predicate.test(entry.getKey(), value));
        return changed;
    }
    public void setSpawnCost(EntityType<?> type, double mass, double gravityLimit) {
        if (type != null) spawnCosts.put(type, new SpawnDensity(mass, gravityLimit));
    }
    public void clearSpawnCost(EntityType<?> type) { if (type != null) spawnCosts.remove(type); }
}
