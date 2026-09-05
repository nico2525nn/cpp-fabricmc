package net.fabricmc.fabric.api.event.player;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.block.BlockState;
import net.minecraft.block.entity.BlockEntity;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;

public final class PlayerBlockBreakEvents {
    private PlayerBlockBreakEvents() {}
    @FunctionalInterface public interface Before {
        boolean beforeBlockBreak(World world, PlayerEntity player, BlockPos pos,
                                 BlockState state, BlockEntity blockEntity);
    }
    public static final Event<Before> BEFORE = new Event<>(CppModRuntime::registerBeforeBreak);
    @FunctionalInterface public interface After {
        void afterBlockBreak(net.minecraft.world.World world, PlayerEntity player, BlockPos pos, BlockState state, BlockEntity blockEntity);
    }
    public static final Event<After> AFTER = new Event<>(After.class, callbacks -> (world, player, pos, state, blockEntity) -> {
        for (After callback : callbacks) callback.afterBlockBreak(world, player, pos, state, blockEntity);
    });
    public static final Event<After> CANCELED = new Event<>(After.class, callbacks -> (world, player, pos, state, blockEntity) -> {
        for (After callback : callbacks) callback.afterBlockBreak(world, player, pos, state, blockEntity);
    });
    public static void clear() { BEFORE.clear(); AFTER.clear(); CANCELED.clear(); }
}
