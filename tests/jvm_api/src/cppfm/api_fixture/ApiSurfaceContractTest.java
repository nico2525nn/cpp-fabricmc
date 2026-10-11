package cppfm.api_fixture;

import com.mojang.brigadier.CommandDispatcher;
import com.mojang.brigadier.arguments.StringArgumentType;
import com.mojang.brigadier.builder.LiteralArgumentBuilder;
import com.mojang.brigadier.suggestion.Suggestions;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
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
import net.minecraft.command.argument.EntityArgumentType;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.command.CommandManager;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.ActionResult;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.NativeAccess;
import net.minecraft.text.Text;
import net.minecraft.world.GameRules;
import cppfm.api.ModEvents;
import cppfm.bridge.CppModRuntime;
import cppfm.bridge.WrapperCache;
import net.fabricmc.fabric.api.networking.v1.PlayerLookup;

/** Executable compatibility fixture for Fabric API/events/commands surface. */
public final class ApiSurfaceContractTest {
    private static int checks;
    private static final AtomicInteger FAILED_BOOTSTRAP_TICKS = new AtomicInteger();
    private static final List<String> ENTRYPOINT_ORDER = new CopyOnWriteArrayList<>();

    public static final class FailingBootstrapMod implements net.fabricmc.api.ModInitializer {
        @Override public void onInitialize() {
            net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents.START_SERVER_TICK
                .register(server -> FAILED_BOOTSTRAP_TICKS.incrementAndGet());
            throw new IllegalStateException("intentional entrypoint fixture failure");
        }
    }

    public static final class MainOrderMod implements net.fabricmc.api.ModInitializer {
        @Override public void onInitialize() { ENTRYPOINT_ORDER.add("main"); }
    }

    public static final class ServerOrderMod
            implements net.fabricmc.api.DedicatedServerModInitializer {
        @Override public void onInitializeServer() { ENTRYPOINT_ORDER.add("server"); }
    }

    private static final class NativeLivingEntityProbe extends net.minecraft.entity.LivingEntity {
        private float authoritativeHealth;
        private int healthWrites;

        NativeLivingEntityProbe(float health) {
            super(0xCAFE);
            authoritativeHealth = health;
        }

        @Override public float getHealth() { return authoritativeHealth; }
        @Override public void setHealth(float value) {
            healthWrites++;
            authoritativeHealth = value;
        }
    }

    private static final class UnboundNativeLivingEntityProbe
            extends net.minecraft.entity.LivingEntity {
        UnboundNativeLivingEntityProbe() { super(0xCAFF); }
    }

    private static final class LocalLivingEntityProbe extends net.minecraft.entity.LivingEntity {
        LocalLivingEntityProbe() {
            super(net.minecraft.entity.EntityType.UNKNOWN, null);
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 1 && "--bootstrap-contracts".equals(args[0])) {
            verifyBootstrapContracts();
            System.out.println("JVM bootstrap contract fixture: " + checks + " PASS");
            return;
        }
        verifyBootstrapContracts();
        ApiSurfaceFixture fixture = new ApiSurfaceFixture();
        fixture.onInitialize();

        check(net.minecraft.entity.mob.WaterCreatureEntity.class
                    .isAssignableFrom(net.minecraft.entity.passive.FishEntity.class) &&
                net.minecraft.entity.mob.PathAwareEntity.class
                    .isAssignableFrom(net.minecraft.entity.passive.FishEntity.class) &&
                !net.minecraft.entity.passive.AnimalEntity.class
                    .isAssignableFrom(net.minecraft.entity.passive.FishEntity.class),
            "FishEntity preserves the Vanilla WaterCreatureEntity hierarchy");

        MinecraftServer startingServer = MinecraftServer.of(NativeAccess.serverHandle());
        ServerWorld startingWorld = ServerWorld.of(0L, startingServer);
        check(startingServer.getPlayerManager() == null,
            "player manager is unavailable during SERVER_STARTING");
        check(startingWorld.getPlayers().isEmpty() && PlayerLookup.all(startingServer).isEmpty(),
            "player lookup APIs are empty before world services load");
        check(EntityArgumentType.players().parse(new com.mojang.brigadier.StringReader("@a"))
                .getEntities(new net.minecraft.server.command.ServerCommandSource(null, startingServer))
                .isEmpty(), "player selectors are safe before world services load");

        check(CppModRuntime.bootstrap("/tmp/cppfm-api-fixture-no-mods", "/tmp/cppfm-api-config"), "runtime bootstrap fallback");
        // The real lifecycle reaches SERVER_STARTED only after the native
        // world load has made the server's world and player services live.
        CppModRuntime.onServerWorldsLoaded();
        check(ApiSurfaceFixture.STARTED.get() == 1, "lifecycle invoker after world load");
        MinecraftServer server = MinecraftServer.of(NativeAccess.serverHandle());
        testLivingEntityHealthBoundary();
        CppModRuntime.onServerTick(7L);
        check(ApiSurfaceFixture.TICK_START.get() == 1, "server tick start dispatch");
        check(ApiSurfaceFixture.TICK_END.get() == 1, "server tick end dispatch");

        CppModRuntime.onPlayerJoin(1L);
        CppModRuntime.onPlayerQuit(1L);
        check(ApiSurfaceFixture.PLAYER_JOIN.get() == 1, "player join dispatch");
        check(ApiSurfaceFixture.PLAYER_LEAVE.get() == 1, "player leave dispatch");
        check(ApiSurfaceFixture.ENTITY_LOAD.get() == 1,
            "player entity load dispatch");
        check(ApiSurfaceFixture.ENTITY_UNLOAD.get() == 1,
            "player entity unload dispatch");
        Entity unknownWrapper = Entity.of(2L);
        check(CppModRuntime.onMobSpawn(2L, 0.0, 0.0, 0.0, 0), "spawn allowed");
        CppModRuntime.onMobLoad(2L, 0);
        check(ApiSurfaceFixture.ENTITY_LOAD.get() == 2,
            "entity load dispatch includes player and inserted mob");
        check(unknownWrapper != null &&
                !(unknownWrapper instanceof net.minecraft.entity.LivingEntity) &&
                ApiSurfaceFixture.MOB_WRAPPER_LOAD.get() == 0 &&
                ApiSurfaceFixture.LOADED_MOB_WRAPPER.get() == null,
            "unknown handles use a non-living base wrapper without JNI type data");
        CppModRuntime.onMobUnload(2L, 0);
        check(ApiSurfaceFixture.ENTITY_UNLOAD.get() == 2,
            "entity unload dispatch includes the removed mob");
        check(Entity.of(2L) != unknownWrapper,
            "entity unload evicts the strong Java wrapper after listener dispatch");
        testRejectedMobSpawnEvictsWrappers();
        check(!CppModRuntime.onEntityDamage(1L, 0L, 8.0f, "generic"), "damage cancellation");
        check(CppModRuntime.onEntityDamage(1L, 0L, 2.0f, "generic"), "damage allow");
        checks += DamageEventsContractTest.run();
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
        net.minecraft.world.World originalEntityWorld = net.minecraft.world.World.of(0L);
        net.minecraft.entity.decoration.ArmorStandEntity unregisteredEntity =
            new net.minecraft.entity.decoration.ArmorStandEntity(
                net.minecraft.entity.EntityType.ARMOR_STAND, originalEntityWorld);
        int entityLoadCountBeforeUnsupportedSpawn = ApiSurfaceFixture.ENTITY_LOAD.get();
        check(!world.spawnEntity(unregisteredEntity),
            "Java-only entity spawn fails until native registration exists");
        check(unregisteredEntity.getWorld() == originalEntityWorld &&
                ApiSurfaceFixture.ENTITY_LOAD.get() == entityLoadCountBeforeUnsupportedSpawn,
            "failed Java-only spawn does not mutate world or emit load lifecycle");
        BlockPos pos = new BlockPos(3, -60, 4);
        check(world.setBlockState(pos, Blocks.STONE.getDefaultState()), "world write");
        check(world.getBlockState(pos).isOf(Blocks.STONE), "world read");
        check(world.isChunkLoaded(0, 0) && !world.isChunkLoaded(2, 2),
            "world chunk-loaded query is coordinate-specific");
        ServerPlayerEntity player = ServerPlayerEntity.of(1L);
        check(player.getWorld() instanceof ServerWorld,
            "server player world resolves to the ServerWorld wrapper type");
        check(player.getWorld() == player.getServerWorld()
                && player.getEntityWorld() == player.getServerWorld(),
            "player world accessors preserve the canonical ServerWorld identity");
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
                && FabricLoader.matchesVersion("1.21.4", "<1.20 || >=1.21")
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

    private static void verifyBootstrapContracts() throws IOException {
        Path temporaryRoot = Files.createTempDirectory("cppfm-jvm-bootstrap-contract-");
        FAILED_BOOTSTRAP_TICKS.set(0);
        ENTRYPOINT_ORDER.clear();
        try {
            Path dependencyMods = temporaryRoot.resolve("dependency-failure");
            writeMod(dependencyMods, "dependency_failure",
                "{\"id\":\"dependency_failure\",\"version\":\"1.0.0\"," +
                "\"depends\":{\"minecraft\":\">=99.0.0\"}}");
            check(!CppModRuntime.bootstrap(dependencyMods.toString(),
                    temporaryRoot.resolve("config").toString()),
                "dependency validation failure aborts bootstrap");
            check(!FabricLoader.getInstance().isModLoaded("dependency_failure"),
                "dependency failure leaves no published mod metadata");

            Path entrypointFailureMods = temporaryRoot.resolve("entrypoint-failure");
            writeMod(entrypointFailureMods, "entrypoint_failure",
                "{\"id\":\"entrypoint_failure\",\"version\":\"1.0.0\"," +
                "\"entrypoints\":{\"main\":\"" +
                    FailingBootstrapMod.class.getName() + "\"}}");
            check(!CppModRuntime.bootstrap(entrypointFailureMods.toString(),
                    temporaryRoot.resolve("config").toString()),
                "entrypoint initialization failure aborts bootstrap without strict mode");
            check(!FabricLoader.getInstance().isModLoaded("entrypoint_failure")
                    && FabricLoader.getInstance().getEntrypoints("main", Object.class).isEmpty(),
                "entrypoint failure rolls back loader metadata and entrypoints");
            CppModRuntime.onServerTick(1L);
            check(FAILED_BOOTSTRAP_TICKS.get() == 0,
                "failed entrypoint callback is not dispatched after rollback");

            Path validMods = temporaryRoot.resolve("valid-mods");
            writeMod(validMods, "a_server_fixture",
                "{\"id\":\"a_server_fixture\",\"version\":\"1.0.0\"," +
                "\"entrypoints\":{\"server\":\"" + ServerOrderMod.class.getName() + "\"}}");
            writeMod(validMods, "z_main_fixture",
                "{\"id\":\"z_main_fixture\",\"version\":\"1.0.0\"," +
                "\"depends\":{\"minecraft\":[\"<1.21.0\",\">=1.21.0\"]}," +
                "\"entrypoints\":{\"main\":\"" + MainOrderMod.class.getName() + "\"}}");
            check(CppModRuntime.bootstrap(validMods.toString(),
                    temporaryRoot.resolve("config").toString()),
                "version-candidate array accepts any matching alternative");
            check(ENTRYPOINT_ORDER.equals(List.of("main", "server")),
                "all main entrypoints run before server entrypoints across mods");
            CppModRuntime.onServerTick(2L);
            check(FAILED_BOOTSTRAP_TICKS.get() == 0,
                "rollback removes callbacks registered by a failed initializer");
        } finally {
            try {
                CppModRuntime.shutdown();
            } finally {
                deleteTree(temporaryRoot);
            }
        }
    }

    private static void writeMod(Path modsDirectory, String directoryName, String metadata)
            throws IOException {
        Path modDirectory = modsDirectory.resolve(directoryName);
        Files.createDirectories(modDirectory);
        Files.writeString(modDirectory.resolve("fabric.mod.json"), metadata,
            StandardCharsets.UTF_8);
    }

    private static void deleteTree(Path root) throws IOException {
        try (var paths = Files.walk(root)) {
            for (Path path : paths.sorted(java.util.Comparator.reverseOrder()).toList())
                Files.deleteIfExists(path);
        }
    }

    private static void testLivingEntityHealthBoundary() throws Exception {
        NativeLivingEntityProbe nativeEntity = new NativeLivingEntityProbe(5.0f);
        AtomicInteger nativeDamageCallbacks = new AtomicInteger();
        net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.ALLOW_DAMAGE
            .register((entity, source, amount) -> {
                if (entity == nativeEntity) nativeDamageCallbacks.incrementAndGet();
                return true;
            });

        check(!nativeEntity.damage(
                new net.minecraft.entity.damage.DamageSource("generic"), 2.0f),
            "native-backed Java damage fails closed without a C++ damage entrypoint");
        check(nativeEntity.authoritativeHealth == 5.0f && nativeEntity.healthWrites == 0,
            "native-backed Java damage does not write shadow/authoritative health");
        check(nativeDamageCallbacks.get() == 0,
            "native-backed Java damage does not dispatch callbacks for unapplied damage");

        nativeEntity.heal(2.0f);
        check(nativeEntity.authoritativeHealth == 7.0f && nativeEntity.healthWrites == 1,
            "native-backed heal bases its write on the authoritative health getter");

        net.minecraft.entity.LivingEntity unboundNativeEntity =
            new UnboundNativeLivingEntityProbe();
        java.lang.reflect.Field shadowHealth =
            net.minecraft.entity.LivingEntity.class.getDeclaredField("health");
        shadowHealth.setAccessible(true);
        float shadowBefore = shadowHealth.getFloat(unboundNativeEntity);
        unboundNativeEntity.setHealth(9.0f);
        check(shadowHealth.getFloat(unboundNativeEntity) == shadowBefore,
            "native-backed setHealth never mutates the Java shadow field");

        LocalLivingEntityProbe localEntity = new LocalLivingEntityProbe();
        localEntity.setHealth(5.0f);
        localEntity.heal(2.0f);
        check(localEntity.getHealth() == 7.0f,
            "Java-only heal continues to use local health");
        check(localEntity.damage(
                new net.minecraft.entity.damage.DamageSource("generic"), 1.0f) &&
                localEntity.getHealth() == 6.0f,
            "Java-only damage continues to mutate local health");
    }

    private static void testRejectedMobSpawnEvictsWrappers() {
        final long firstHandle = 0xB000L;
        final int rejectedSpawns = 32;
        AtomicReference<Entity> rejectedWrapper = new AtomicReference<>();
        ModEvents.MOB_SPAWN.register((entity, world, x, y, z) -> {
            long handle = entity.nativeHandle();
            if (handle >= firstHandle && handle < firstHandle + rejectedSpawns) {
                rejectedWrapper.set(entity);
                return false;
            }
            return true;
        });
        for (int i = 0; i < rejectedSpawns; i++) {
            long handle = firstHandle + i;
            check(!CppModRuntime.onMobSpawn(handle, 0.0, 0.0, 0.0, 0),
                "mob spawn callback rejects native handle");
            Entity rejected = rejectedWrapper.getAndSet(null);
            check(rejected != null && rejected.nativeHandle() == handle,
                "rejected spawn callback receives its native wrapper");
            try {
                check(Entity.of(handle) != rejected,
                    "rejected spawn wrapper is evicted from the strong cache");
            } finally {
                WrapperCache.remove(handle);
            }
        }
    }
}
