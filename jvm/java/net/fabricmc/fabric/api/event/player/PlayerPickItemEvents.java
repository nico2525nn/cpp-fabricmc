package net.fabricmc.fabric.api.event.player;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.block.BlockState;
import net.minecraft.entity.Entity;
import net.minecraft.item.ItemStack;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.util.math.BlockPos;

public final class PlayerPickItemEvents {
    private PlayerPickItemEvents() { }
    @FunctionalInterface public interface PickItemFromBlock {
        ItemStack onPickItemFromBlock(ServerPlayerEntity player, BlockPos pos, BlockState state,
                                      boolean includeData);
    }
    @FunctionalInterface public interface PickItemFromEntity {
        ItemStack onPickItemFromEntity(ServerPlayerEntity player, Entity entity, boolean includeData);
    }
    public static final Event<PickItemFromBlock> BLOCK = EventFactory.createArrayBacked(
        PickItemFromBlock.class, callbacks -> (player, pos, state, includeData) -> {
            for (PickItemFromBlock callback : callbacks) {
                ItemStack result = callback.onPickItemFromBlock(player, pos, state, includeData);
                if (result != null) return result;
            }
            return null;
        });
    public static final Event<PickItemFromEntity> ENTITY = EventFactory.createArrayBacked(
        PickItemFromEntity.class, callbacks -> (player, entity, includeData) -> {
            for (PickItemFromEntity callback : callbacks) {
                ItemStack result = callback.onPickItemFromEntity(player, entity, includeData);
                if (result != null) return result;
            }
            return null;
        });
    public static void clear() { BLOCK.clear(); ENTITY.clear(); }
}
