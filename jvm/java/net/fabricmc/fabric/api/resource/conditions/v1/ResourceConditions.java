package net.fabricmc.fabric.api.resource.conditions.v1;

import java.util.Arrays;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.registry.Registry;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.RegistryOps;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.resource.featuretoggle.FeatureFlag;
import net.minecraft.util.Identifier;

/** Built-in boolean conditions used by Fabric resource-loader. */
public final class ResourceConditions {
    public static final String CONDITIONS_KEY = "fabric:load_conditions";
    public static final String OVERLAYS_KEY = "fabric:load_conditions_overlays";
    private static final Map<Identifier, ResourceConditionType<?>> REGISTERED_CONDITIONS =
        new ConcurrentHashMap<>();

    private ResourceConditions() { }

    public static void register(ResourceConditionType<?> type) {
        if (type != null) REGISTERED_CONDITIONS.put(type.id(), type);
    }

    public static ResourceConditionType<?> getConditionType(Identifier id) {
        return REGISTERED_CONDITIONS.get(id);
    }

    public static ResourceCondition alwaysTrue() {
        return condition("always_true", ignored -> true);
    }

    public static ResourceCondition not(ResourceCondition condition) {
        Objects.requireNonNull(condition, "condition");
        return condition("not", context -> !condition.test(context));
    }

    public static ResourceCondition and(ResourceCondition... conditions) {
        ResourceCondition[] values = conditions == null ? new ResourceCondition[0] : conditions.clone();
        return condition("and", context -> Arrays.stream(values)
            .filter(Objects::nonNull).allMatch(value -> value.test(context)));
    }

    public static ResourceCondition or(ResourceCondition... conditions) {
        ResourceCondition[] values = conditions == null ? new ResourceCondition[0] : conditions.clone();
        return condition("or", context -> Arrays.stream(values)
            .filter(Objects::nonNull).anyMatch(value -> value.test(context)));
    }

    public static ResourceCondition allModsLoaded(String... mods) {
        String[] values = mods == null ? new String[0] : mods.clone();
        return condition("all_mods_loaded", ignored -> Arrays.stream(values)
            .allMatch(mod -> mod != null && FabricLoader.getInstance().isModLoaded(mod)));
    }

    public static ResourceCondition anyModsLoaded(String... mods) {
        String[] values = mods == null ? new String[0] : mods.clone();
        return condition("any_mod_loaded", ignored -> Arrays.stream(values)
            .anyMatch(mod -> mod != null && FabricLoader.getInstance().isModLoaded(mod)));
    }

    @SafeVarargs
    public static <T> ResourceCondition tagsPopulated(TagKey<T>... tags) {
        return condition("tags_populated", ignored -> tags != null && tags.length > 0);
    }

    @SafeVarargs
    public static <T> ResourceCondition tagsPopulated(
            RegistryKey<? extends Registry<T>> registry, TagKey<T>... tags) {
        return tagsPopulated(tags);
    }

    public static ResourceCondition featuresEnabled(Identifier... features) {
        return condition("features_enabled", ignored -> features != null && features.length > 0);
    }

    public static ResourceCondition featuresEnabled(FeatureFlag... features) {
        return condition("features_enabled", ignored -> features != null && features.length > 0);
    }

    @SafeVarargs
    public static <T> ResourceCondition registryContains(RegistryKey<T>... keys) {
        return condition("registry_contains", ignored -> keys != null && keys.length > 0);
    }

    public static <T> ResourceCondition registryContains(
            RegistryKey<? extends Registry<T>> registry, Identifier... ids) {
        return condition("registry_contains", ignored -> ids != null && ids.length > 0);
    }

    private static ResourceCondition condition(String id, java.util.function.Predicate<RegistryOps.RegistryInfoGetter> predicate) {
        ResourceConditionType<ResourceCondition> type = ResourceConditionType.create(
            Identifier.of("fabric", id), new com.mojang.serialization.MapCodec<>() { });
        return new ResourceCondition() {
            @Override public ResourceConditionType<?> getType() { return type; }
            @Override public boolean test(RegistryOps.RegistryInfoGetter context) {
                return predicate.test(context);
            }
        };
    }
}
