package net.fabricmc.fabric.api.event.player;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.util.ActionResult;
import net.minecraft.util.Hand;
import net.minecraft.util.hit.EntityHitResult;
import net.minecraft.world.World;

@FunctionalInterface
public interface UseEntityCallback {
    ActionResult interact(PlayerEntity player, World world, Hand hand, Entity entity, EntityHitResult hitResult);
    Event<UseEntityCallback> EVENT = EventFactory.createArrayBacked(UseEntityCallback.class, callbacks -> (player, world, hand, entity, hit) -> {
        for (UseEntityCallback callback : callbacks) { ActionResult result = callback.interact(player, world, hand, entity, hit); if (result != null && result != ActionResult.PASS) return result; }
        return ActionResult.PASS;
    });
    static void clear() { EVENT.clear(); }
}
