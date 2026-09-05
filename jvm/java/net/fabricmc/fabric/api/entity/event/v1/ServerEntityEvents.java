package net.fabricmc.fabric.api.entity.event.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EquipmentSlot;
import net.minecraft.entity.LivingEntity;
import net.minecraft.item.ItemStack;
import net.minecraft.server.world.ServerWorld;

/** Entity load/unload hooks backed by the native spawn boundary. */
public final class ServerEntityEvents {
    private ServerEntityEvents() { }
    @FunctionalInterface public interface Load { void onLoad(Entity entity, ServerWorld world); }
    @FunctionalInterface public interface Unload { void onUnload(Entity entity, ServerWorld world); }
    @FunctionalInterface public interface EquipmentChange {
        void onChange(LivingEntity entity, EquipmentSlot slot, ItemStack previousStack, ItemStack currentStack);
    }

    public static final Event<Load> LOAD = new Event<>(CppModRuntime::registerEntityLoad,
        Load.class, callbacks -> (entity, world) -> {
            for (Load callback : callbacks) callback.onLoad(entity, world);
        });
    public static final Event<Unload> UNLOAD = new Event<>(CppModRuntime::registerEntityUnload,
        Unload.class, callbacks -> (entity, world) -> {
            for (Unload callback : callbacks) callback.onUnload(entity, world);
        });
    /** Official Fabric names; LOAD/UNLOAD remain source-compatible aliases. */
    public static final Event<Load> ENTITY_LOAD = LOAD;
    public static final Event<Unload> ENTITY_UNLOAD = UNLOAD;
    public static final Event<EquipmentChange> EQUIPMENT_CHANGE = EventFactory.createArrayBacked(
        EquipmentChange.class, callbacks -> (entity, slot, previous, current) -> {
            for (EquipmentChange callback : callbacks) callback.onChange(entity, slot, previous, current);
        });
    public static void clear() { LOAD.clear(); UNLOAD.clear(); EQUIPMENT_CHANGE.clear(); }
}
