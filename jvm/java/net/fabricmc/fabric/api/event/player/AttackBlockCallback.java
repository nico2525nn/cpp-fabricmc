package net.fabricmc.fabric.api.event.player;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.util.ActionResult;
import net.minecraft.util.Hand;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.world.World;

@FunctionalInterface
public interface AttackBlockCallback {
    ActionResult interact(PlayerEntity player, World world, Hand hand, BlockPos pos, Direction direction);
    /** Compatibility adapter for the pre-1.21.4 local shadow signature. */
    default ActionResult interact(PlayerEntity player, World world, Hand hand, BlockHitResult hitResult) {
        if (hitResult == null) return interact(player, world, hand, null, Direction.DOWN);
        return interact(player, world, hand, hitResult.getBlockPos(), hitResult.getSide());
    }
    Event<AttackBlockCallback> EVENT = new Event<>(CppModRuntime::registerAttackBlock, AttackBlockCallback.class, callbacks -> (player, world, hand, pos, direction) -> {
        for (AttackBlockCallback callback : callbacks) { ActionResult result = callback.interact(player, world, hand, pos, direction); if (result != null && result != ActionResult.PASS) return result; }
        return ActionResult.PASS;
    });
    public static void clear() { EVENT.clear(); }
}
