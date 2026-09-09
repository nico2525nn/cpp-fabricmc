package net.fabricmc.fabric.api.entity.event.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.block.BlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.entity.player.PlayerEntity.SleepFailureReason;
import net.minecraft.util.ActionResult;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.Vec3d;

/** Sleep hooks with the neutral-result semantics used by Fabric 1.21.4. */
public final class EntitySleepEvents {
    private EntitySleepEvents() { }

    @FunctionalInterface public interface AllowSleeping {
        SleepFailureReason allowSleep(PlayerEntity player, BlockPos sleepingPos);
    }
    @FunctionalInterface public interface StartSleeping {
        void onStartSleeping(Entity entity, BlockPos sleepingPos);
    }
    @FunctionalInterface public interface StopSleeping {
        void onStopSleeping(Entity entity, BlockPos sleepingPos);
    }
    @FunctionalInterface public interface AllowBed {
        ActionResult allowBed(Entity entity, BlockPos sleepingPos, BlockState state, boolean vanillaResult);
    }
    @FunctionalInterface public interface AllowSleepTime {
        ActionResult allowSleepTime(PlayerEntity player, BlockPos sleepingPos, boolean vanillaResult);
    }
    @FunctionalInterface public interface AllowNearbyMonsters {
        ActionResult allowNearbyMonsters(PlayerEntity player, BlockPos sleepingPos, boolean vanillaResult);
    }
    @FunctionalInterface public interface AllowResettingTime {
        boolean allowResettingTime(PlayerEntity player);
    }
    @FunctionalInterface public interface ModifySleepingDirection {
        Direction modifySleepDirection(Entity entity, BlockPos sleepingPos, Direction direction);
    }
    @FunctionalInterface public interface AllowSettingSpawn {
        boolean allowSettingSpawn(PlayerEntity player, BlockPos sleepingPos);
    }
    @FunctionalInterface public interface SetBedOccupationState {
        boolean setBedOccupationState(Entity entity, BlockPos sleepingPos, BlockState state,
                                      boolean occupied);
    }
    @FunctionalInterface public interface ModifyWakeUpPosition {
        Vec3d modifyWakeUpPosition(Entity entity, BlockPos sleepingPos, BlockState state,
                                   Vec3d wakeUpPosition);
    }

    public static final Event<AllowSleeping> ALLOW_SLEEPING = EventFactory.createArrayBacked(
        AllowSleeping.class, callbacks -> (player, pos) -> {
            for (AllowSleeping callback : callbacks) {
                SleepFailureReason result = callback.allowSleep(player, pos);
                if (result != null) return result;
            }
            return null;
        });
    public static final Event<StartSleeping> START_SLEEPING = EventFactory.createArrayBacked(
        StartSleeping.class, callbacks -> (entity, pos) -> {
            for (StartSleeping callback : callbacks) callback.onStartSleeping(entity, pos);
        });
    public static final Event<StopSleeping> STOP_SLEEPING = EventFactory.createArrayBacked(
        StopSleeping.class, callbacks -> (entity, pos) -> {
            for (StopSleeping callback : callbacks) callback.onStopSleeping(entity, pos);
        });
    public static final Event<AllowBed> ALLOW_BED = EventFactory.createArrayBacked(
        AllowBed.class, callbacks -> (entity, pos, state, vanilla) -> firstActionResult(
            callbacks, callback -> callback.allowBed(entity, pos, state, vanilla)));
    public static final Event<AllowSleepTime> ALLOW_SLEEP_TIME = EventFactory.createArrayBacked(
        AllowSleepTime.class, callbacks -> (player, pos, vanilla) -> firstActionResult(
            callbacks, callback -> callback.allowSleepTime(player, pos, vanilla)));
    public static final Event<AllowNearbyMonsters> ALLOW_NEARBY_MONSTERS = EventFactory.createArrayBacked(
        AllowNearbyMonsters.class, callbacks -> (player, pos, vanilla) -> firstActionResult(
            callbacks, callback -> callback.allowNearbyMonsters(player, pos, vanilla)));
    public static final Event<AllowResettingTime> ALLOW_RESETTING_TIME = EventFactory.createArrayBacked(
        AllowResettingTime.class, callbacks -> player -> {
            for (AllowResettingTime callback : callbacks)
                if (!callback.allowResettingTime(player)) return false;
            return true;
        });
    public static final Event<ModifySleepingDirection> MODIFY_SLEEPING_DIRECTION = EventFactory.createArrayBacked(
        ModifySleepingDirection.class, callbacks -> (entity, pos, direction) -> {
            for (ModifySleepingDirection callback : callbacks)
                direction = callback.modifySleepDirection(entity, pos, direction);
            return direction;
        });
    public static final Event<AllowSettingSpawn> ALLOW_SETTING_SPAWN = EventFactory.createArrayBacked(
        AllowSettingSpawn.class, callbacks -> (player, pos) -> {
            for (AllowSettingSpawn callback : callbacks)
                if (!callback.allowSettingSpawn(player, pos)) return false;
            return true;
        });
    public static final Event<SetBedOccupationState> SET_BED_OCCUPATION_STATE = EventFactory.createArrayBacked(
        SetBedOccupationState.class, callbacks -> (entity, pos, state, occupied) -> {
            for (SetBedOccupationState callback : callbacks)
                if (callback.setBedOccupationState(entity, pos, state, occupied)) return true;
            return false;
        });
    public static final Event<ModifyWakeUpPosition> MODIFY_WAKE_UP_POSITION = EventFactory.createArrayBacked(
        ModifyWakeUpPosition.class, callbacks -> (entity, pos, state, wakeUp) -> {
            for (ModifyWakeUpPosition callback : callbacks)
                wakeUp = callback.modifyWakeUpPosition(entity, pos, state, wakeUp);
            return wakeUp;
        });

    private static <T> ActionResult firstActionResult(T[] callbacks,
                                                       java.util.function.Function<? super T, ActionResult> call) {
        for (T callback : callbacks) {
            ActionResult result = call.apply(callback);
            if (result != null && result != ActionResult.PASS) return result;
        }
        return ActionResult.PASS;
    }

    public static void clear() {
        ALLOW_SLEEPING.clear(); START_SLEEPING.clear(); STOP_SLEEPING.clear(); ALLOW_BED.clear();
        ALLOW_SLEEP_TIME.clear(); ALLOW_NEARBY_MONSTERS.clear(); ALLOW_RESETTING_TIME.clear();
        MODIFY_SLEEPING_DIRECTION.clear(); ALLOW_SETTING_SPAWN.clear(); SET_BED_OCCUPATION_STATE.clear();
        MODIFY_WAKE_UP_POSITION.clear();
    }
}
