package cppfm.api_fixture;

import com.mojang.brigadier.CommandDispatcher;
import com.mojang.brigadier.arguments.StringArgumentType;
import com.mojang.brigadier.builder.LiteralArgumentBuilder;
import com.mojang.brigadier.suggestion.Suggestions;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import net.fabricmc.loader.api.FabricLoader;
import net.fabricmc.loader.api.ObjectShare;
import net.fabricmc.loader.api.Version;
import net.fabricmc.fabric.api.command.v2.CommandRegistrationCallback;
import net.fabricmc.fabric.api.attachment.v1.AttachmentRegistry;
import net.fabricmc.fabric.api.attachment.v1.AttachmentType;
import net.fabricmc.fabric.api.gamerule.v1.CustomGameRuleCategory;
import net.fabricmc.fabric.api.gamerule.v1.FabricGameRuleVisitor;
import net.fabricmc.fabric.api.gamerule.v1.GameRuleFactory;
import net.fabricmc.fabric.api.gamerule.v1.GameRuleRegistry;
import net.fabricmc.fabric.api.gamerule.v1.rule.DoubleRule;
import net.fabricmc.fabric.api.gamerule.v1.rule.EnumRule;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.player.PlayerBlockBreakEvents;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;
import net.fabricmc.fabric.api.registry.RegistryEntryAddedCallback;
import net.minecraft.block.Blocks;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.PlayerInventory;
import net.minecraft.item.ItemStack;
import net.minecraft.item.Items;
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
import net.minecraft.text.Text;
import net.minecraft.world.GameRules;
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
        ServerCommandSource commandSource = new ServerCommandSource(null, server);
        CommandRegistrationCallback.EVENT.invoker().register(dispatcher,
            new net.minecraft.server.command.CommandRegistryAccess(),
            CommandManager.RegistrationEnvironment.DEDICATED);
        int commandResult = dispatcher.execute("api_fixture 7", commandSource);
        check(commandResult == 42 && ApiSurfaceFixture.COMMAND_VALUE.get() == 7, "command execute");
        var redirectTarget = dispatcher.register(LiteralArgumentBuilder.<ServerCommandSource>literal("api_target")
            .executes(context -> 17));
        dispatcher.register(LiteralArgumentBuilder.<ServerCommandSource>literal("api_alias")
            .redirect(redirectTarget));
        check(dispatcher.execute("api_alias", commandSource) == 17, "command redirect");
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

        AttachmentType<Integer> attachment = AttachmentRegistry.createDefaulted(
            Identifier.of("cppfm_api", "counter"), () -> 7);
        Entity attachmentTarget = player;
        check(!attachmentTarget.hasAttached(attachment), "attachment starts empty");
        check(attachmentTarget.getAttachedOrCreate(attachment) == 7,
            "attachment default initializer");
        check(attachmentTarget.setAttached(attachment, 9) == 7
                && attachmentTarget.getAttached(attachment) == 9,
            "attachment set and get");
        check(attachmentTarget.modifyAttached(attachment, value -> value + 1) == 9
                && attachmentTarget.getAttached(attachment) == 10,
            "attachment modify");
        check(attachmentTarget.removeAttached(attachment) == 10
                && !attachmentTarget.hasAttached(attachment),
            "attachment remove");

        AtomicInteger ruleChanges = new AtomicInteger();
        GameRules.Type<GameRules.IntRule> intRuleType = GameRuleFactory.createIntRule(
            5, 0, 10, (changedServer, rule) -> ruleChanges.incrementAndGet());
        GameRules.Key<GameRules.IntRule> intRuleKey = GameRuleRegistry.register(
            "cppfm_api_int", GameRules.Category.UPDATES, intRuleType);
        GameRules rules = new GameRules();
        GameRules.IntRule intRule = rules.get(intRuleKey);
        check(intRule.get() == 5 && intRule.validateAndSet("8") && intRule.get() == 8,
            "bounded gamerule creation and validation");
        intRule.set(9, server);
        check(intRule.get() == 9 && ruleChanges.get() == 1,
            "gamerule change callback");

        GameRules.Type<DoubleRule> doubleRuleType = GameRuleFactory.createDoubleRule(1.5, 0.0, 3.0);
        GameRules.Key<DoubleRule> doubleRuleKey = GameRuleRegistry.register(
            "cppfm_api_double", GameRules.Category.MISC, doubleRuleType);
        DoubleRule doubleRule = rules.get(doubleRuleKey);
        check(doubleRule.get() == 1.5 && doubleRule.validate("2.25")
                && !doubleRule.validate("4.0"), "double gamerule range");
        check(doubleRule.method_20779().equals("1.5") && doubleRule.method_20781() == 1,
            "official intermediary double-rule ABI aliases");
        enum Mode { FAST, SAFE }
        GameRules.Type<EnumRule<Mode>> enumRuleType = GameRuleFactory.createEnumRule(Mode.FAST);
        GameRules.Key<EnumRule<Mode>> enumRuleKey = GameRuleRegistry.register(
            "cppfm_api_mode", new CustomGameRuleCategory(
                Identifier.of("cppfm_api", "mode"), Text.literal("API mode")), enumRuleType);
        EnumRule<Mode> enumRule = rules.get(enumRuleKey);
        enumRule.cycle();
        check(enumRule.get() == Mode.SAFE && enumRule.supports(Mode.FAST)
                && CustomGameRuleCategory.getCategory(enumRuleKey).isPresent()
                && GameRuleRegistry.hasRegistration("cppfm_api_mode"),
            "enum and custom-category gamerule");
        check(enumRule.method_20779().equals("safe") && enumRule.method_20781() == 1,
            "official intermediary enum-rule ABI aliases");
        AtomicInteger doubleVisits = new AtomicInteger();
        rules.accept(new FabricGameRuleVisitor() {
            @Override public void visitDouble(GameRules.Key<DoubleRule> key,
                                               GameRules.Type<DoubleRule> type) {
                if (key.equals(doubleRuleKey)) doubleVisits.incrementAndGet();
            }
        });
        check(doubleVisits.get() == 1, "extended gamerule visitor");

        Registry<Object> registry = new net.minecraft.registry.SimpleRegistry<>(null);
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

        System.setProperty("cppfm.loaded.mods", "api_surface");
        System.setProperty("cppfm.mod.version.api_surface", "1.2.3");
        FabricLoader loader = FabricLoader.getInstance();
        check(loader.isModLoaded("minecraft") && loader.isModLoaded("fabricloader"),
            "loader builtin ids");
        check(loader.isModLoaded("api_surface")
                && loader.getModContainer("api_surface").isPresent(), "loader metadata lookup");
        check(loader.getModContainer("minecraft").orElseThrow().getMetadata().getVersion()
                .compareTo(Version.parse("1.21.4")) == 0, "semantic version comparison");
        check(FabricLoader.matchesVersion("1.21.4", ">=1.21.0 <1.22")
                && !FabricLoader.matchesVersion("1.22.0", "1.21.x"), "version predicate");
        ObjectShare share = loader.getObjectShare();
        AtomicReference<Object> shared = new AtomicReference<>();
        share.whenAvailable("api_surface", (key, value) -> shared.set(value));
        check(share.put("api_surface", 9) == null && shared.get().equals(9)
                && share.putIfAbsent("api_surface", 10).equals(9), "object share");

        check(net.fabricmc.fabric.api.tag.convention.v1.ConventionalEntityTypeTags.BOSSES.id()
                .equals(Identifier.of("c", "bosses"))
                && net.fabricmc.fabric.api.tag.convention.v2.ConventionalEntityTypeTags.TELEPORTING_NOT_SUPPORTED.id()
                .equals(Identifier.of("c", "teleporting_not_supported")),
            "conventional entity-type tag catalog");
        check(net.fabricmc.fabric.api.networking.v1.PlayerLookup.class.getMethod(
                "around", ServerWorld.class, net.minecraft.util.math.Vec3i.class, double.class) != null,
            "PlayerLookup integer-vector overload");
        check(net.fabricmc.fabric.api.itemgroup.v1.FabricItemGroupEntries.class.getMethod(
                "addAfter", net.minecraft.item.ItemConvertible.class, java.util.Collection.class,
                net.minecraft.item.ItemGroup.StackVisibility.class) != null,
            "ItemGroup visibility overload");
        check(net.fabricmc.fabric.api.resource.SimpleResourceReloadListener.class.getMethod(
                "method_25931", net.minecraft.resource.ResourceReloader.Synchronizer.class,
                net.minecraft.resource.ResourceManager.class, java.util.concurrent.Executor.class,
                java.util.concurrent.Executor.class) != null,
            "resource reload intermediary overload");
        AtomicInteger packCloses = new AtomicInteger();
        net.minecraft.resource.ResourcePack pack = new net.minecraft.resource.ResourcePack() {
            private boolean closed;

            @Override
            public net.minecraft.resource.InputSupplier<java.io.InputStream> open(
                    net.minecraft.resource.ResourceType type, Identifier id) {
                if (closed) throw new IllegalStateException("pack already closed");
                return () -> new java.io.ByteArrayInputStream(new byte[] { 7 });
            }

            @Override
            public void close() {
                closed = true;
                packCloses.incrementAndGet();
            }
        };
        net.minecraft.resource.LifecycledResourceManager resourceManager =
            new net.minecraft.resource.LifecycledResourceManager(
                net.minecraft.resource.ResourceType.SERVER_DATA,
                java.util.List.of(pack));
        net.minecraft.resource.Resource firstResource = resourceManager.getResource(
            Identifier.of("cppfm_api", "probe.json")).orElseThrow();
        firstResource.close();
        net.minecraft.resource.Resource secondResource = resourceManager.getResource(
            Identifier.of("cppfm_api", "probe.json")).orElseThrow();
        try (java.io.InputStream stream = secondResource.getInputStream()) {
            check(stream.read() == 7, "resource close does not close shared pack");
        }
        resourceManager.close();
        check(packCloses.get() == 1, "resource manager owns pack close");
        check(net.fabricmc.fabric.api.util.Item2ObjectMap.class.getMethod(
                "get", net.minecraft.item.ItemConvertible.class) != null,
            "Item2ObjectMap ItemConvertible ABI");
        check(net.fabricmc.fabric.api.entity.FakePlayer.class.getMethod(
                "method_5731", net.minecraft.world.TeleportTarget.class) != null,
            "FakePlayer intermediary override aliases");

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
