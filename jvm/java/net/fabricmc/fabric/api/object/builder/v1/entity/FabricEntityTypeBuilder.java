package net.fabricmc.fabric.api.object.builder.v1.entity;

import java.util.LinkedHashSet;
import java.util.Set;
import java.util.function.Supplier;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityDimensions;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.SpawnGroup;
import net.minecraft.entity.SpawnLocation;
import net.minecraft.entity.SpawnRestriction;
import net.minecraft.entity.attribute.DefaultAttributeContainer;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.block.Block;
import net.minecraft.registry.RegistryKey;
import net.minecraft.resource.featuretoggle.FeatureFlag;
import net.minecraft.resource.featuretoggle.FeatureSet;
import net.minecraft.world.Heightmap;

/** Stateful fluent builder for custom entity descriptors. */
public class FabricEntityTypeBuilder<T extends Entity> implements FabricEntityType.Builder<T> {
    protected SpawnGroup spawnGroup = SpawnGroup.MISC;
    protected EntityType.EntityFactory<? extends T> factory;
    protected EntityDimensions dimensions = EntityDimensions.changing(0.6f, 1.8f);
    protected boolean disableSummon, disableSaving, fireImmune, spawnableFarFromPlayer;
    protected boolean alwaysUpdateVelocity, canPotentiallyExecuteCommands, forceVelocityUpdates;
    protected int trackingRange = 5, updateRate = 3;
    protected final Set<Block> spawnBlocks = new LinkedHashSet<>();
    protected FeatureSet requiredFeatures = FeatureSet.EMPTY;
    protected DefaultAttributeContainer defaultAttributes;
    protected SpawnLocation spawnLocation;
    protected Heightmap.Type spawnHeightmap;
    protected SpawnRestriction.SpawnPredicate<?> spawnPredicate;

    public static <T extends Entity> FabricEntityTypeBuilder<T> create() { return new FabricEntityTypeBuilder<>(); }
    public static <T extends Entity> FabricEntityTypeBuilder<T> create(SpawnGroup group) { return new FabricEntityTypeBuilder<T>().spawnGroup(group); }
    public static <T extends Entity> FabricEntityTypeBuilder<T> create(SpawnGroup group, EntityType.EntityFactory<T> factory) { return new FabricEntityTypeBuilder<T>().spawnGroup(group).entityFactory(factory); }
    public static <T extends LivingEntity> Living<T> createLiving() { return new Living<>(); }
    public static <T extends MobEntity> Mob<T> createMob() { return new Mob<>(); }
    public FabricEntityTypeBuilder<T> spawnGroup(SpawnGroup value) { if (value != null) spawnGroup = value; return this; }
    public <N extends T> FabricEntityTypeBuilder<N> entityFactory(EntityType.EntityFactory<N> value) { factory = value; return (FabricEntityTypeBuilder<N>) this; }
    public FabricEntityTypeBuilder<T> disableSummon() { disableSummon = true; return this; }
    public FabricEntityTypeBuilder<T> disableSaving() { disableSaving = true; return this; }
    public FabricEntityTypeBuilder<T> fireImmune() { fireImmune = true; return this; }
    public FabricEntityTypeBuilder<T> spawnableFarFromPlayer() { spawnableFarFromPlayer = true; return this; }
    public FabricEntityTypeBuilder<T> dimensions(EntityDimensions value) { if (value != null) dimensions = value; return this; }
    public FabricEntityTypeBuilder<T> trackable(int range, int rate) { trackingRange = range; updateRate = rate; return this; }
    public FabricEntityTypeBuilder<T> trackable(int range, int rate, boolean force) { trackingRange = range; updateRate = rate; forceVelocityUpdates = force; return this; }
    public FabricEntityTypeBuilder<T> trackRangeChunks(int range) { trackingRange = range * 16; return this; }
    public FabricEntityTypeBuilder<T> trackRangeBlocks(int range) { trackingRange = range; return this; }
    public FabricEntityTypeBuilder<T> trackedUpdateRate(int rate) { updateRate = rate; return this; }
    public FabricEntityTypeBuilder<T> forceTrackedVelocityUpdates(boolean value) { forceVelocityUpdates = value; return this; }
    public FabricEntityTypeBuilder<T> specificSpawnBlocks(Block... values) { if (values != null) for (Block value : values) if (value != null) spawnBlocks.add(value); return this; }
    public FabricEntityTypeBuilder<T> requires(FeatureFlag... values) {
        requiredFeatures = values == null || values.length == 0 ? FeatureSet.EMPTY : FeatureSet.of(values[0], java.util.Arrays.copyOfRange(values, 1, values.length));
        return this;
    }
    public FabricEntityTypeBuilder<T> alwaysUpdateVelocitySetting(boolean value) { alwaysUpdateVelocity = value; return this; }
    public FabricEntityTypeBuilder<T> canPotentiallyExecuteCommandsSetting(boolean value) { canPotentiallyExecuteCommands = value; return this; }
    public EntityType<T> build(RegistryKey<EntityType<?>> key) {
        EntityType<T> value = vanillaBuilder().build(key);
        if (defaultAttributes != null) FabricDefaultAttributeRegistry.register((EntityType<? extends LivingEntity>) value, defaultAttributes);
        if (spawnPredicate != null) registerRestriction(value);
        return value;
    }
    protected void registerRestriction(EntityType<?> value) { }
    public EntityType.Builder<T> vanillaBuilder() {
        @SuppressWarnings("unchecked") EntityType.EntityFactory<T> typedFactory = (EntityType.EntityFactory<T>) factory;
        EntityType.Builder<T> builder = EntityType.Builder.create((type, world) -> typedFactory == null ? null : typedFactory.create(type, world), spawnGroup);
        builder.dimensions(dimensions).alwaysUpdateVelocity(alwaysUpdateVelocity).canPotentiallyExecuteCommands(canPotentiallyExecuteCommands);
        if (disableSummon) builder.disableSummon(); if (disableSaving) builder.disableSaving(); if (fireImmune) builder.fireImmune();
        if (spawnableFarFromPlayer) builder.spawnableFarFromPlayer();
        builder.trackable(trackingRange, updateRate, forceVelocityUpdates);
        if (defaultAttributes != null) builder.defaultAttributes(DefaultAttributeContainer.builder());
        return builder;
    }

    public static class Living<T extends LivingEntity> extends FabricEntityTypeBuilder<T> implements FabricEntityType.Builder.Living<T> {
        @Override public Living<T> spawnGroup(SpawnGroup value) { super.spawnGroup(value); return this; }
        @Override public <N extends T> Living<N> entityFactory(EntityType.EntityFactory<N> value) { super.entityFactory(value); return (Living<N>) this; }
        @Override public Living<T> disableSummon() { super.disableSummon(); return this; }
        @Override public Living<T> disableSaving() { super.disableSaving(); return this; }
        @Override public Living<T> fireImmune() { super.fireImmune(); return this; }
        @Override public Living<T> spawnableFarFromPlayer() { super.spawnableFarFromPlayer(); return this; }
        @Override public Living<T> dimensions(EntityDimensions value) { super.dimensions(value); return this; }
        @Override public Living<T> trackable(int range, int rate) { super.trackable(range, rate); return this; }
        @Override public Living<T> trackable(int range, int rate, boolean force) { super.trackable(range, rate, force); return this; }
        @Override public Living<T> trackRangeChunks(int range) { super.trackRangeChunks(range); return this; }
        @Override public Living<T> trackRangeBlocks(int range) { super.trackRangeBlocks(range); return this; }
        @Override public Living<T> trackedUpdateRate(int rate) { super.trackedUpdateRate(rate); return this; }
        @Override public Living<T> forceTrackedVelocityUpdates(boolean value) { super.forceTrackedVelocityUpdates(value); return this; }
        @Override public Living<T> specificSpawnBlocks(Block... values) { super.specificSpawnBlocks(values); return this; }
        @Override public Living<T> requires(FeatureFlag... values) { super.requires(values); return this; }
        @Override public Living<T> defaultAttributes(Supplier<DefaultAttributeContainer.Builder> value) { defaultAttributes = value == null || value.get() == null ? null : value.get().build(); return this; }
        @Override public EntityType<T> build(RegistryKey<EntityType<?>> key) { return super.build(key); }
    }
    public static class Mob<T extends MobEntity> extends Living<T> implements FabricEntityType.Builder.Mob<T> {
        @Override public Mob<T> spawnGroup(SpawnGroup value) { super.spawnGroup(value); return this; }
        @Override public <N extends T> Mob<N> entityFactory(EntityType.EntityFactory<N> value) { super.entityFactory(value); return (Mob<N>) this; }
        @Override public Mob<T> disableSummon() { super.disableSummon(); return this; }
        @Override public Mob<T> disableSaving() { super.disableSaving(); return this; }
        @Override public Mob<T> fireImmune() { super.fireImmune(); return this; }
        @Override public Mob<T> spawnableFarFromPlayer() { super.spawnableFarFromPlayer(); return this; }
        @Override public Mob<T> dimensions(EntityDimensions value) { super.dimensions(value); return this; }
        @Override public Mob<T> trackable(int range, int rate) { super.trackable(range, rate); return this; }
        @Override public Mob<T> trackable(int range, int rate, boolean force) { super.trackable(range, rate, force); return this; }
        @Override public Mob<T> trackRangeChunks(int range) { super.trackRangeChunks(range); return this; }
        @Override public Mob<T> trackRangeBlocks(int range) { super.trackRangeBlocks(range); return this; }
        @Override public Mob<T> trackedUpdateRate(int rate) { super.trackedUpdateRate(rate); return this; }
        @Override public Mob<T> forceTrackedVelocityUpdates(boolean value) { super.forceTrackedVelocityUpdates(value); return this; }
        @Override public Mob<T> specificSpawnBlocks(Block... values) { super.specificSpawnBlocks(values); return this; }
        @Override public Mob<T> requires(FeatureFlag... values) { super.requires(values); return this; }
        @Override public Mob<T> defaultAttributes(Supplier<DefaultAttributeContainer.Builder> value) { super.defaultAttributes(value); return this; }
        @Override public Mob<T> spawnRestriction(SpawnLocation location, Heightmap.Type heightmap, SpawnRestriction.SpawnPredicate<T> predicate) { spawnLocation = location; spawnHeightmap = heightmap; spawnPredicate = predicate; return this; }
        @Override public EntityType<T> build(RegistryKey<EntityType<?>> key) { return super.build(key); }
    }
}
