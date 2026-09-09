package net.fabricmc.fabric.api.resource.conditions.v1;

import com.mojang.serialization.Codec;
import java.util.List;
import net.minecraft.registry.RegistryOps;

/** A condition evaluated while loading a resource/data file. */
public interface ResourceCondition {
    Codec<ResourceCondition> CODEC = new Codec<>() { };
    Codec<List<ResourceCondition>> LIST_CODEC = CODEC.listOf();
    Codec<ResourceCondition> CONDITION_CODEC = CODEC;

    ResourceConditionType<?> getType();
    boolean test(RegistryOps.RegistryInfoGetter registryInfoGetter);
}
