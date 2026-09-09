package net.fabricmc.fabric.api.object.builder.v1.entity;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.attribute.DefaultAttributeContainer;

/** Registry of default attribute containers for custom living entity types. */
public final class FabricDefaultAttributeRegistry {
    private static final Map<EntityType<?>, DefaultAttributeContainer> ATTRIBUTES = new ConcurrentHashMap<>();
    private FabricDefaultAttributeRegistry() { }
    public static void register(EntityType<? extends LivingEntity> type, DefaultAttributeContainer.Builder builder) {
        if (type != null && builder != null) ATTRIBUTES.put(type, builder.build());
    }
    public static void register(EntityType<? extends LivingEntity> type, DefaultAttributeContainer container) {
        if (type != null && container != null) ATTRIBUTES.put(type, container);
    }
    public static DefaultAttributeContainer get(EntityType<?> type) { return ATTRIBUTES.get(type); }
}
