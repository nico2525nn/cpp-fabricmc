package net.fabricmc.fabric.api.object.builder.v1.entity;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.vehicle.AbstractMinecartEntity;

public final class MinecartComparatorLogicRegistry {
    private static final Map<EntityType<?>, MinecartComparatorLogic<?>> LOGIC = new ConcurrentHashMap<>();
    private MinecartComparatorLogicRegistry() { }
    @SuppressWarnings("unchecked") public static MinecartComparatorLogic<AbstractMinecartEntity> getCustomComparatorLogic(EntityType<?> type) {
        return (MinecartComparatorLogic<AbstractMinecartEntity>) LOGIC.get(type);
    }
    public static <T extends AbstractMinecartEntity> void register(EntityType<T> type, MinecartComparatorLogic<? super T> logic) {
        if (type != null && logic != null) LOGIC.put(type, logic);
    }
}
