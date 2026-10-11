package net.fabricmc.fabric.api.event.lifecycle.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EquipmentSlot;
import net.minecraft.entity.LivingEntity;
import net.minecraft.item.ItemStack;
import net.minecraft.server.world.ServerWorld;

public final class ServerEntityEvents {
    private ServerEntityEvents() {}
    @FunctionalInterface public interface Load { void onLoad(Entity entity, ServerWorld world); }
    @FunctionalInterface public interface Unload { void onUnload(Entity entity, ServerWorld world); }
    @FunctionalInterface public interface EquipmentChange {
        void onChange(LivingEntity livingEntity, EquipmentSlot equipmentSlot,
                      ItemStack previousStack, ItemStack currentStack);
    }
    public static final Event<Load> ENTITY_LOAD = EventFactory.createArrayBacked(
        Load.class, callbacks -> (entity, world) -> {
            for (Load callback : callbacks) callback.onLoad(entity, world);
        });
    public static final Event<Unload> ENTITY_UNLOAD = EventFactory.createArrayBacked(
        Unload.class, callbacks -> (entity, world) -> {
            for (Unload callback : callbacks) callback.onUnload(entity, world);
        });
    /** Official Fabric name; LOAD remains as a source-compatible alias. */
    public static final Event<Load> LOAD = ENTITY_LOAD;
    /** Official Fabric name; UNLOAD remains as a source-compatible alias. */
    public static final Event<Unload> UNLOAD = ENTITY_UNLOAD;
    public static final Event<EquipmentChange> EQUIPMENT_CHANGE = EventFactory.createArrayBacked(
        EquipmentChange.class, callbacks -> (entity, slot, previous, current) -> {
            for (EquipmentChange callback : callbacks)
                callback.onChange(entity, slot, previous, current);
        });
    public static void clear() { LOAD.clear(); UNLOAD.clear(); EQUIPMENT_CHANGE.clear(); }
}
