package net.fabricmc.fabric.api.event.player;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.util.ActionResult;
import net.minecraft.util.Hand;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.world.World;

@FunctionalInterface
public interface AttackBlockCallback {
    ActionResult interact(PlayerEntity player, World world, Hand hand, BlockHitResult hitResult);
    Event<AttackBlockCallback> EVENT = new Event<>(CppModRuntime::registerAttackBlock, AttackBlockCallback.class, callbacks -> (player, world, hand, hitResult) -> {
        for (AttackBlockCallback callback : callbacks) { ActionResult result = callback.interact(player, world, hand, hitResult); if (result != null && result != ActionResult.PASS) return result; }
        return ActionResult.PASS;
    });
    public static void clear() { EVENT.clear(); }
}
