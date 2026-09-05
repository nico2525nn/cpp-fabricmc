package cppfm.api;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;

import net.minecraft.entity.Entity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;

/**
 * Stable cppfm-owned extension points.  Fabric API adapters delegate to the
 * same lists, so a mod can use either the familiar Fabric surface or this
 * small versioned surface when it only needs server events.
 */
public final class ModEvents {
    private ModEvents() {}

    public static final class Event<T> {
        private final List<T> listeners = new ArrayList<>();
        public synchronized void register(T listener) { listeners.add(listener); }
        public synchronized List<T> snapshot() { return List.copyOf(listeners); }
        public synchronized void clear() { listeners.clear(); }
    }

    @FunctionalInterface public interface Chat {
        /** Return a replacement message; return null to cancel. */
        String onChat(ServerPlayerEntity player, String message);
    }
    @FunctionalInterface public interface Command {
        /** Return a replacement command; return null to cancel. */
        String onCommand(ServerPlayerEntity player, String command);
    }
    @FunctionalInterface public interface BlockBreak {
        boolean onBlockBreak(ServerPlayerEntity player, ServerWorld world,
                             net.minecraft.util.math.BlockPos pos,
                             net.minecraft.block.BlockState state);
    }
    @FunctionalInterface public interface BlockPlace {
        boolean onBlockPlace(ServerPlayerEntity player, ServerWorld world,
                             net.minecraft.util.math.BlockPos pos,
                             net.minecraft.block.BlockState state);
    }
    @FunctionalInterface public interface BlockClicked {
        boolean onBlockClicked(ServerPlayerEntity player, ServerWorld world,
                               net.minecraft.util.math.BlockPos pos,
                               net.minecraft.block.BlockState state, int face);
    }
    @FunctionalInterface public interface EntityDamage {
        boolean onEntityDamage(Entity victim, float amount, String cause);
    }
    @FunctionalInterface public interface MobSpawn {
        boolean onMobSpawn(Entity entity, ServerWorld world, double x, double y, double z);
    }
    @FunctionalInterface public interface Tick {
        void onTick(MinecraftServer server, long tick);
    }

    public static final Event<Chat> CHAT = new Event<>();
    public static final Event<Command> COMMAND = new Event<>();
    public static final Event<BlockBreak> BLOCK_BREAK = new Event<>();
    public static final Event<BlockPlace> BLOCK_PLACE = new Event<>();
    public static final Event<BlockClicked> BLOCK_CLICKED = new Event<>();
    public static final Event<EntityDamage> ENTITY_DAMAGE = new Event<>();
    public static final Event<MobSpawn> MOB_SPAWN = new Event<>();
    public static final Event<Tick> TICK = new Event<>();

    public static void clearAll() {
        CHAT.clear();
        COMMAND.clear();
        BLOCK_BREAK.clear();
        BLOCK_PLACE.clear();
        BLOCK_CLICKED.clear();
        ENTITY_DAMAGE.clear();
        MOB_SPAWN.clear();
        TICK.clear();
    }
}
