package net.fabricmc.fabric.api.event.player;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.util.ActionResult;
import net.minecraft.util.Hand;
import net.minecraft.world.World;

@FunctionalInterface
public interface UseItemCallback {
    ActionResult interact(PlayerEntity player, World world, Hand hand);
    Event<UseItemCallback> EVENT = EventFactory.createArrayBacked(UseItemCallback.class, callbacks -> (player, world, hand) -> {
        for (UseItemCallback callback : callbacks) { ActionResult result = callback.interact(player, world, hand); if (result != null && result != ActionResult.PASS) return result; }
        return ActionResult.PASS;
    });
    static void clear() { EVENT.clear(); }
}
