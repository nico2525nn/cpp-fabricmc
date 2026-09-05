package cppfm.api_fixture;

import com.mojang.brigadier.CommandDispatcher;
import com.mojang.brigadier.arguments.IntegerArgumentType;
import com.mojang.brigadier.arguments.StringArgumentType;
import com.mojang.brigadier.builder.LiteralArgumentBuilder;
import com.mojang.brigadier.suggestion.Suggestions;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import net.fabricmc.fabric.api.command.v2.CommandRegistrationCallback;
import net.fabricmc.fabric.api.entity.event.v1.ServerEntityEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.event.player.PlayerBlockBreakEvents;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;
import net.fabricmc.fabric.api.registry.RegistryEntryAddedCallback;
import net.minecraft.block.Blocks;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.player.PlayerInventory;
import net.minecraft.item.ItemStack;
import net.minecraft.item.Items;
import net.minecraft.network.message.MessageType;
import net.minecraft.network.message.SignedMessage;
import net.minecraft.network.packet.CustomPayloadS2CPacket;
import net.minecraft.registry.Registry;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.command.CommandManager;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.ActionResult;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.world.World;
import cppfm.bridge.CppModRuntime;

/** Executable compatibility fixture for Fabric API/events/commands surface. */
public final class ApiSurfaceContractTest {
    private static int checks;

    public static void main(String[] args) throws Exception {
        ApiSurfaceFixture fixture = new ApiSurfaceFixture();
        fixture.onInitialize();

        MinecraftServer server = MinecraftServer.of(0L);
        ServerLifecycleEvents.SERVER_STARTED.invoker().onServerStarted(server);
        check(ApiSurfaceFixture.STARTED.get() == 1, "lifecycle invoker");

        check(CppModRuntime.bootstrap("/tmp/cppfm-api-fixture-no-mods", "/tmp/cppfm-api-config"), "runtime bootstrap fallback");
        CppModRuntime.onServerTick(7L);
        check(ApiSurfaceFixture.TICK_START.get() == 1, "server tick start dispatch");
        check(ApiSurfaceFixture.TICK_END.get() == 1, "server tick end dispatch");

        CppModRuntime.onPlayerJoin(1L);
        CppModRuntime.onPlayerQuit(1L);
        check(ApiSurfaceFixture.PLAYER_JOIN.get() == 1, "player join dispatch");
        check(ApiSurfaceFixture.PLAYER_LEAVE.get() == 1, "player leave dispatch");
        check(CppModRuntime.onMobSpawn(1L, 0.0, 0.0, 0.0), "spawn allowed and load dispatched");
        check(ApiSurfaceFixture.ENTITY_LOAD.get() == 2, "entity load dispatch includes player and spawn");
        check(!CppModRuntime.onEntityDamage(1L, 0L, 8.0f, "generic"), "damage cancellation");
        check(CppModRuntime.onEntityDamage(1L, 0L, 2.0f, "generic"), "damage allow");
        check(!CppModRuntime.onBlockPlace(1L, 1, 0, 1, 1), "ActionResult consume cancellation");
        check(CppModRuntime.onChat(1L, "blocked") == null, "chat cancellation");
        check("blocked".equals(ApiSurfaceFixture.CHAT.get()), "chat callback input");

        CommandDispatcher<ServerCommandSource> dispatcher = server.getCommandManager().getDispatcher();
        CommandRegistrationCallback.EVENT.invoker().register(dispatcher,
            new net.minecraft.server.command.CommandRegistryAccess(),
            CommandManager.RegistrationEnvironment.DEDICATED);
        int commandResult = dispatcher.execute("api_fixture 7", new ServerCommandSource(null, server));
        check(commandResult == 42 && ApiSurfaceFixture.COMMAND_VALUE.get() == 7, "command execute");
        dispatcher.register(LiteralArgumentBuilder.<ServerCommandSource>literal("api_suggest")
            .then(CommandManager.<ServerCommandSource, String>argument("value", StringArgumentType.word())
                .suggests((context, builder) -> builder.suggest("alpha").suggest("beta").buildFuture())
                .executes(context -> 3)));
        Suggestions suggestions = dispatcher.getCompletionSuggestions(
            dispatcher.parse("api_suggest ", new ServerCommandSource(null, server))).join();
        check(suggestions.getList().stream().anyMatch(value -> value.getText().equals("alpha")), "command suggestions");
        check(dispatcher.hasCommand("api_fixture 7"), "command tree lookup");

        ServerWorld world = ServerWorld.of(0L, server);
        BlockPos pos = new BlockPos(3, -60, 4);
        check(world.setBlockState(pos, Blocks.STONE.getDefaultState()), "world write");
        check(world.getBlockState(pos).isOf(Blocks.STONE), "world read");
        ServerPlayerEntity player = ServerPlayerEntity.of(1L);
        PlayerInventory inventory = new PlayerInventory(0L);
        inventory.setStack(0, new ItemStack(Items.DIAMOND, 4));
        check(inventory.getStack(0).isOf(Items.DIAMOND) && inventory.getStack(0).getCount() == 4, "zero-handle inventory");
        check(world.getEntitiesByType(net.minecraft.entity.EntityType.UNKNOWN, new Box(-10, -70, -10, 10, 10, 10), e -> true).isEmpty(), "empty entity query");

        Registry<Object> registry = new Registry<>();
        AtomicInteger added = new AtomicInteger();
        RegistryEntryAddedCallback.event(registry).register((rawId, id, value) -> {
            check(rawId == 0 && id.equals(Identifier.of("cppfm_api", "entry")), "registry callback metadata");
            added.incrementAndGet();
        });
        Object entry = new Object();
        Registry.register(registry, Identifier.of("cppfm_api", "entry"), entry);
        check(added.get() == 1 && registry.getRawId(entry) == 0, "registry registration");

        ServerPlayNetworking.receive(server, player, player.getNetworkHandler(),
            new ApiSurfaceFixture.ProbePayload(42));
        check(ApiSurfaceFixture.PAYLOAD_RECEIVED.get() == 1, "payload receive callback/context");
        CppModRuntime.onPluginMessage(1L, 1, ApiSurfaceFixture.CHANNEL.toString(),
            new net.minecraft.network.PacketByteBuf().writeInt(42).toByteArray());
        check(ApiSurfaceFixture.PAYLOAD_RECEIVED.get() == 2, "native plugin message boundary");
        ServerPlayNetworking.send(player, new ApiSurfaceFixture.ProbePayload(42));
        check(ServerPlayNetworking.getOutbound(player).size() == 1, "payload send queue");
        check(((CustomPayloadS2CPacket) ServerPlayNetworking.getOutbound(player).get(0)).getData().readInt() == 42,
            "payload encoding");

        PlayerBlockBreakEvents.CANCELED.register((w, p, blockPos, state, entity) -> { });
        CppModRuntime.onBlockBreakResult(1L, 1, 2, 3, 1, false);
        CppModRuntime.shutdown();
        System.out.println("JVM API fixture: " + checks + " PASS");
    }

    private static void check(boolean condition, String name) {
        checks++;
        if (!condition) throw new AssertionError(name);
    }
}
