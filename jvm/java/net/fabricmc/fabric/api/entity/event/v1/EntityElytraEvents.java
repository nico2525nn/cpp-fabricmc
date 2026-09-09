package net.fabricmc.fabric.api.entity.event.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.entity.LivingEntity;

/** Server/common Elytra extension points from Fabric API 1.21.4. */
public final class EntityElytraEvents {
    private EntityElytraEvents() { }

    @FunctionalInterface
    public interface Allow { boolean allowElytraFlight(LivingEntity entity); }

    @FunctionalInterface
    public interface Custom { boolean useCustomElytra(LivingEntity entity, boolean isFallFlying); }

    public static final Event<Allow> ALLOW = EventFactory.createArrayBacked(Allow.class,
        callbacks -> entity -> {
            for (Allow callback : callbacks)
                if (!callback.allowElytraFlight(entity)) return false;
            return true;
        });

    public static final Event<Custom> CUSTOM = EventFactory.createArrayBacked(Custom.class,
        callbacks -> (entity, isFallFlying) -> {
            for (Custom callback : callbacks)
                if (callback.useCustomElytra(entity, isFallFlying)) return true;
            return false;
        });

    public static void clear() { ALLOW.clear(); CUSTOM.clear(); }
}
