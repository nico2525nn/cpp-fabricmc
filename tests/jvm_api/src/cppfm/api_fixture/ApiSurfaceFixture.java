package cppfm.api_fixture;

import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.command.v2.CommandRegistrationCallback;
import net.fabricmc.fabric.api.entity.event.v1.ServerEntityEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.event.player.UseBlockCallback;
import net.fabricmc.fabric.api.message.v1.ServerMessageEvents;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;
import net.minecraft.entity.DamageSource;
import net.minecraft.entity.LivingEntity;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.message.MessageType;
import net.minecraft.network.message.SignedMessage;
import net.minecraft.server.command.CommandManager;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.util.ActionResult;
import net.minecraft.util.Hand;
import net.minecraft.util.Identifier;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.world.World;
import net.minecraft.network.packet.CustomPayload;

/** A no-native fixture that exercises the public server-side API contracts. */
public final class ApiSurfaceFixture implements ModInitializer {
    public static final AtomicInteger STARTED = new AtomicInteger();
    public static final AtomicInteger TICK_START = new AtomicInteger();
    public static final AtomicInteger TICK_END = new AtomicInteger();
    public static final AtomicInteger PLAYER_JOIN = new AtomicInteger();
    public static final AtomicInteger PLAYER_LEAVE = new AtomicInteger();
    public static final AtomicInteger ENTITY_LOAD = new AtomicInteger();
    public static final AtomicInteger COMMAND_VALUE = new AtomicInteger();
    public static final AtomicInteger PAYLOAD_RECEIVED = new AtomicInteger();
    public static final AtomicReference<String> CHAT = new AtomicReference<>();
    public static final Identifier CHANNEL = Identifier.of("cppfm_api", "probe");
    public static final CustomPayload.Id<ProbePayload> PAYLOAD_ID = new CustomPayload.Id<>(CHANNEL);

    @Override public void onInitialize() {
        ServerLifecycleEvents.SERVER_STARTED.register(server -> STARTED.incrementAndGet());
        ServerTickEvents.START_SERVER_TICK.register(server -> TICK_START.incrementAndGet());
        ServerTickEvents.END_SERVER_TICK.register(server -> TICK_END.incrementAndGet());
        ServerPlayerEvents.JOIN.register((handler, player) -> PLAYER_JOIN.incrementAndGet());
        ServerPlayerEvents.LEAVE.register((handler, player) -> PLAYER_LEAVE.incrementAndGet());
        ServerEntityEvents.LOAD.register((entity, world) -> ENTITY_LOAD.incrementAndGet());
        ServerLivingEntityEvents.ALLOW_DAMAGE.register((entity, source, amount) -> amount < 5.0f);
        UseBlockCallback.EVENT.register((player, world, hand, hit) -> ActionResult.CONSUME);
        ServerMessageEvents.ALLOW_CHAT_MESSAGE.register((message, sender, params) -> {
            CHAT.set(message.getContent());
            return false;
        });
        ServerPlayNetworking.registerGlobalReceiver(PAYLOAD_ID, (payload, context) -> {
            PAYLOAD_RECEIVED.incrementAndGet();
            if (payload.value() != 42) throw new IllegalStateException("payload value");
        });
        ServerPlayNetworking.registerGlobalReceiver(CHANNEL, (server, player, handler, buffer, sender) -> {
            PAYLOAD_RECEIVED.incrementAndGet();
            if (buffer.readInt() != 42) throw new IllegalStateException("raw payload value");
        });
        CommandRegistrationCallback.EVENT.register((dispatcher, registry, environment) ->
            dispatcher.register(CommandManager.<ServerCommandSource>literal("api_fixture")
                .then(CommandManager.<ServerCommandSource, Integer>argument("value",
                    com.mojang.brigadier.arguments.IntegerArgumentType.integer(1, 9))
                    .executes(context -> {
                        COMMAND_VALUE.set(com.mojang.brigadier.arguments.IntegerArgumentType.getInteger(context, "value"));
                        return 42;
                    }))));
    }

    public static final class ProbePayload implements CustomPayload {
        private final int value;
        public ProbePayload(int value) { this.value = value; }
        public int value() { return value; }
        @Override public Id<? extends CustomPayload> getId() { return PAYLOAD_ID; }
        public void write(PacketByteBuf buffer) { buffer.writeInt(value); }
    }
}
