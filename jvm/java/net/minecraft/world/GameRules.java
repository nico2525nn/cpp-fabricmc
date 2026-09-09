package net.minecraft.world;

import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.arguments.BoolArgumentType;
import com.mojang.brigadier.arguments.IntegerArgumentType;
import com.mojang.brigadier.builder.RequiredArgumentBuilder;
import com.mojang.brigadier.context.CommandContext;
import java.util.Collections;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.BiConsumer;
import java.util.function.Function;
import java.util.function.Supplier;
import java.util.stream.Stream;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.resource.featuretoggle.FeatureSet;
import net.minecraft.server.MinecraftServer;

/**
 * The server-side 1.21.4 game-rule contract.
 *
 * <p>The native server owns the authoritative game-rule values. This class
 * is the JVM-facing registry and value facade used by Fabric entrypoints and
 * mixins. It retains vanilla's typed {@code Type}/{@code Rule} relationship,
 * including bounded integer rules and the Fabric double/enum extensions.</p>
 */
public final class GameRules {
    private static final Map<Key<?>, Type<?>> RULE_TYPES = new ConcurrentHashMap<>();
    private final Map<Key<?>, Rule<?>> rules = new ConcurrentHashMap<>();
    private final FeatureSet enabledFeatures;

    public static final int DEFAULT_RANDOM_TICK_SPEED = 3;

    public static final Key<BooleanRule> DO_MOB_SPAWNING = new Key<>("doMobSpawning", Category.SPAWNING);
    public static final Key<BooleanRule> ANNOUNCE_ADVANCEMENTS = new Key<>("announceAdvancements", Category.CHAT);
    public static final Key<BooleanRule> DO_MOB_LOOT = new Key<>("doMobLoot", Category.DROPS);
    public static final Key<BooleanRule> DO_MOB_GRIEFING = new Key<>("mobGriefing", Category.MOBS);
    public static final Key<BooleanRule> REDUCED_DEBUG_INFO = new Key<>("reducedDebugInfo", Category.MISC);
    public static final Key<IntRule> MAX_ENTITY_CRAMMING = new Key<>("maxEntityCramming", Category.MOBS);
    public static final Key<BooleanRule> DO_TILE_DROPS = new Key<>("doTileDrops", Category.DROPS);
    public static final Key<BooleanRule> KEEP_INVENTORY = new Key<>("keepInventory", Category.PLAYER);
    public static final Key<BooleanRule> DISABLE_RAIDS = new Key<>("disableRaids", Category.MOBS);
    public static final Key<BooleanRule> DO_IMMEDIATE_RESPAWN = new Key<>("doImmediateRespawn", Category.PLAYER);
    public static final Key<BooleanRule> DO_LIMITED_CRAFTING = new Key<>("doLimitedCrafting", Category.PLAYER);
    public static final Key<BooleanRule> DO_WEATHER_CYCLE = new Key<>("doWeatherCycle", Category.UPDATES);
    public static final Key<IntRule> MAX_COMMAND_CHAIN_LENGTH = new Key<>("maxCommandChainLength", Category.MISC);
    public static final Key<BooleanRule> DISABLE_ELYTRA_MOVEMENT_CHECK = new Key<>("disableElytraMovementCheck", Category.PLAYER);
    public static final Key<BooleanRule> FREEZE_DAMAGE = new Key<>("freezeDamage", Category.MOBS);
    public static final Key<BooleanRule> SPECTATORS_GENERATE_CHUNKS = new Key<>("spectatorsGenerateChunks", Category.PLAYER);
    public static final Key<IntRule> SPAWN_RADIUS = new Key<>("spawnRadius", Category.PLAYER);
    public static final Key<BooleanRule> SEND_COMMAND_FEEDBACK = new Key<>("sendCommandFeedback", Category.CHAT);
    public static final Key<BooleanRule> UNIVERSAL_ANGER = new Key<>("universalAnger", Category.MOBS);
    public static final Key<BooleanRule> FORGIVE_DEAD_PLAYERS = new Key<>("forgiveDeadPlayers", Category.MOBS);
    public static final Key<BooleanRule> DO_TRADER_SPAWNING = new Key<>("doTraderSpawning", Category.SPAWNING);
    public static final Key<BooleanRule> DROWNING_DAMAGE = new Key<>("drowningDamage", Category.PLAYER);
    public static final Key<BooleanRule> FALL_DAMAGE = new Key<>("fallDamage", Category.PLAYER);
    public static final Key<BooleanRule> FIRE_DAMAGE = new Key<>("fireDamage", Category.PLAYER);
    public static final Key<BooleanRule> DO_INSOMNIA = new Key<>("doInsomnia", Category.SPAWNING);
    public static final Key<BooleanRule> DO_PATROL_SPAWNING = new Key<>("doPatrolSpawning", Category.SPAWNING);
    public static final Key<BooleanRule> LOG_ADMIN_COMMANDS = new Key<>("logAdminCommands", Category.CHAT);
    public static final Key<BooleanRule> SHOW_DEATH_MESSAGES = new Key<>("showDeathMessages", Category.CHAT);
    public static final Key<BooleanRule> NATURAL_REGENERATION = new Key<>("naturalRegeneration", Category.PLAYER);
    public static final Key<BooleanRule> DO_DAYLIGHT_CYCLE = new Key<>("doDaylightCycle", Category.UPDATES);
    public static final Key<BooleanRule> DO_ENTITY_DROPS = new Key<>("doEntityDrops", Category.DROPS);
    public static final Key<BooleanRule> COMMAND_BLOCK_OUTPUT = new Key<>("commandBlockOutput", Category.CHAT);
    public static final Key<IntRule> SPAWN_CHUNK_RADIUS = new Key<>("spawnChunkRadius", Category.MISC);
    public static final Key<IntRule> RANDOM_TICK_SPEED = new Key<>("randomTickSpeed", Category.UPDATES);
    public static final Key<BooleanRule> DO_FIRE_TICK = new Key<>("doFireTick", Category.UPDATES);
    public static final Key<BooleanRule> GLOBAL_SOUND_EVENTS = new Key<>("globalSoundEvents", Category.MISC);
    public static final Key<IntRule> MINECART_MAX_SPEED = new Key<>("minecartMaxSpeed", Category.MISC);
    public static final Key<BooleanRule> MOB_EXPLOSION_DROP_DECAY = new Key<>("mobExplosionDropDecay", Category.DROPS);
    public static final Key<BooleanRule> BLOCK_EXPLOSION_DROP_DECAY = new Key<>("blockExplosionDropDecay", Category.DROPS);
    public static final Key<BooleanRule> LAVA_SOURCE_CONVERSION = new Key<>("lavaSourceConversion", Category.UPDATES);
    public static final Key<BooleanRule> WATER_SOURCE_CONVERSION = new Key<>("waterSourceConversion", Category.UPDATES);
    public static final Key<IntRule> SNOW_ACCUMULATION_HEIGHT = new Key<>("snowAccumulationHeight", Category.UPDATES);
    public static final Key<BooleanRule> TNT_EXPLOSION_DROP_DECAY = new Key<>("tntExplosionDropDecay", Category.DROPS);
    public static final Key<IntRule> PLAYERS_SLEEPING_PERCENTAGE = new Key<>("playersSleepingPercentage", Category.PLAYER);
    public static final Key<BooleanRule> DISABLE_PLAYER_MOVEMENT_CHECK = new Key<>("disablePlayerMovementCheck", Category.PLAYER);
    public static final Key<BooleanRule> DO_WARDEN_SPAWNING = new Key<>("doWardenSpawning", Category.SPAWNING);
    public static final Key<BooleanRule> DO_VINES_SPREAD = new Key<>("doVinesSpread", Category.UPDATES);
    public static final Key<IntRule> MAX_COMMAND_FORK_COUNT = new Key<>("maxCommandForkCount", Category.MISC);
    public static final Key<BooleanRule> PROJECTILES_CAN_BREAK_BLOCKS = new Key<>("projectilesCanBreakBlocks", Category.MOBS);
    public static final Key<IntRule> PLAYERS_NETHER_PORTAL_DEFAULT_DELAY = new Key<>("playersNetherPortalDefaultDelay", Category.PLAYER);
    public static final Key<IntRule> PLAYERS_NETHER_PORTAL_CREATIVE_DELAY = new Key<>("playersNetherPortalCreativeDelay", Category.PLAYER);
    public static final Key<BooleanRule> ENDER_PEARLS_VANISH_ON_DEATH = new Key<>("enderPearlsVanishOnDeath", Category.PLAYER);
    public static final Key<IntRule> COMMAND_MODIFICATION_BLOCK_LIMIT = new Key<>("commandModificationBlockLimit", Category.MISC);

    static {
        registerBoolean(DO_MOB_SPAWNING, true);
        registerBoolean(ANNOUNCE_ADVANCEMENTS, true);
        registerBoolean(DO_MOB_LOOT, true);
        registerBoolean(DO_MOB_GRIEFING, true);
        registerBoolean(REDUCED_DEBUG_INFO, false);
        registerInt(MAX_ENTITY_CRAMMING, 24);
        registerBoolean(DO_TILE_DROPS, true);
        registerBoolean(KEEP_INVENTORY, false);
        registerBoolean(DISABLE_RAIDS, false);
        registerBoolean(DO_IMMEDIATE_RESPAWN, false);
        registerBoolean(DO_LIMITED_CRAFTING, false);
        registerBoolean(DO_WEATHER_CYCLE, true);
        registerInt(MAX_COMMAND_CHAIN_LENGTH, 65536);
        registerBoolean(DISABLE_ELYTRA_MOVEMENT_CHECK, false);
        registerBoolean(FREEZE_DAMAGE, true);
        registerBoolean(SPECTATORS_GENERATE_CHUNKS, true);
        registerInt(SPAWN_RADIUS, 10);
        registerBoolean(SEND_COMMAND_FEEDBACK, true);
        registerBoolean(UNIVERSAL_ANGER, false);
        registerBoolean(FORGIVE_DEAD_PLAYERS, true);
        registerBoolean(DO_TRADER_SPAWNING, true);
        registerBoolean(DROWNING_DAMAGE, true);
        registerBoolean(FALL_DAMAGE, true);
        registerBoolean(FIRE_DAMAGE, true);
        registerBoolean(DO_INSOMNIA, true);
        registerBoolean(DO_PATROL_SPAWNING, true);
        registerBoolean(LOG_ADMIN_COMMANDS, true);
        registerBoolean(SHOW_DEATH_MESSAGES, true);
        registerBoolean(NATURAL_REGENERATION, true);
        registerBoolean(DO_DAYLIGHT_CYCLE, true);
        registerBoolean(DO_ENTITY_DROPS, true);
        registerBoolean(COMMAND_BLOCK_OUTPUT, true);
        registerInt(SPAWN_CHUNK_RADIUS, 2);
        registerInt(RANDOM_TICK_SPEED, DEFAULT_RANDOM_TICK_SPEED);
        registerBoolean(DO_FIRE_TICK, true);
        registerBoolean(GLOBAL_SOUND_EVENTS, true);
        registerInt(MINECART_MAX_SPEED, 8);
        registerBoolean(MOB_EXPLOSION_DROP_DECAY, true);
        registerBoolean(BLOCK_EXPLOSION_DROP_DECAY, true);
        registerBoolean(LAVA_SOURCE_CONVERSION, false);
        registerBoolean(WATER_SOURCE_CONVERSION, true);
        registerInt(SNOW_ACCUMULATION_HEIGHT, 1);
        registerBoolean(TNT_EXPLOSION_DROP_DECAY, true);
        registerInt(PLAYERS_SLEEPING_PERCENTAGE, 100);
        registerBoolean(DISABLE_PLAYER_MOVEMENT_CHECK, false);
        registerBoolean(DO_WARDEN_SPAWNING, true);
        registerBoolean(DO_VINES_SPREAD, true);
        registerInt(MAX_COMMAND_FORK_COUNT, 65536);
        registerBoolean(PROJECTILES_CAN_BREAK_BLOCKS, true);
        registerInt(PLAYERS_NETHER_PORTAL_DEFAULT_DELAY, 80);
        registerInt(PLAYERS_NETHER_PORTAL_CREATIVE_DELAY, 0);
        registerBoolean(ENDER_PEARLS_VANISH_ON_DEATH, false);
        registerInt(COMMAND_MODIFICATION_BLOCK_LIMIT, 32768);
    }

    private static void registerBoolean(Key<BooleanRule> key, boolean initial) {
        RULE_TYPES.put(key, BooleanRule.create(initial));
    }

    private static void registerInt(Key<IntRule> key, int initial) {
        RULE_TYPES.put(key, IntRule.create(initial));
    }

    public GameRules() { this(FeatureSet.EMPTY); }

    /** 1.21.4 constructor used by server-rule bootstrap and Access Widener tests. */
    public GameRules(Map<?, ?> initialValues, FeatureSet features) {
        this(features);
        if (initialValues != null) {
            for (Map.Entry<?, ?> entry : initialValues.entrySet()) {
                if (entry.getKey() instanceof Key<?> key && entry.getValue() instanceof Rule<?> rule)
                    rules.put(key, rule);
            }
        }
    }

    /** Vanilla's feature-filtered constructor. */
    public GameRules(FeatureSet features) {
        enabledFeatures = features == null ? FeatureSet.EMPTY : features;
        for (Map.Entry<Key<?>, Type<?>> entry : RULE_TYPES.entrySet()) {
            if (entry.getValue().getRequiredFeatures().isSubsetOf(enabledFeatures))
                rules.put(entry.getKey(), createRule(entry.getValue()));
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private static Rule<?> createRule(Type<?> type) {
        return ((Type) type).createRule();
    }

    /** Fabric and vanilla registration hook. */
    public static <T extends Rule<T>> Key<T> register(String name, Category category, Type<T> type) {
        Objects.requireNonNull(name, "name");
        Objects.requireNonNull(type, "type");
        Key<T> key = new Key<>(name, category);
        RULE_TYPES.put(key, type);
        return key;
    }

    public static Map<Key<?>, Type<?>> getRuleTypes() {
        return Collections.unmodifiableMap(RULE_TYPES);
    }

    public FeatureSet getEnabledFeatures() { return enabledFeatures; }

    @SuppressWarnings("unchecked")
    public <T extends Rule<T>> T get(Key<T> key) {
        if (key == null) return null;
        Rule<?> value = rules.get(key);
        if (value == null) {
            Type<?> type = RULE_TYPES.get(key);
            if (type == null || !type.getRequiredFeatures().isSubsetOf(enabledFeatures)) return null;
            value = createRule(type);
            Rule<?> existing = rules.putIfAbsent(key, value);
            if (existing != null) value = existing;
        }
        return (T) value;
    }

    public boolean getBoolean(Key<BooleanRule> key) {
        BooleanRule rule = get(key);
        return rule != null && rule.get();
    }

    public int getInt(Key<IntRule> key) {
        IntRule rule = get(key);
        return rule == null ? 0 : rule.get();
    }

    /** Visit all enabled registered rules. */
    public void accept(Visitor visitor) {
        if (visitor == null) return;
        for (Map.Entry<Key<?>, Type<?>> entry : RULE_TYPES.entrySet()) {
            if (!entry.getValue().getRequiredFeatures().isSubsetOf(enabledFeatures)) continue;
            visitor.visit(entry.getKey(), entry.getValue());
            entry.getValue().accept(visitor, entry.getKey());
        }
    }

    public GameRules copy() {
        GameRules copy = new GameRules(enabledFeatures);
        for (Map.Entry<Key<?>, Rule<?>> entry : rules.entrySet())
            copy.rules.put(entry.getKey(), entry.getValue().copy());
        return copy;
    }

    public Stream<Map.Entry<Key<?>, Rule<?>>> streamAllRules() {
        return rules.entrySet().stream();
    }

    /** Compact NBT representation used by save/load fixtures. */
    public NbtCompound toNbt() {
        NbtCompound result = new NbtCompound();
        for (Map.Entry<Key<?>, Rule<?>> entry : rules.entrySet())
            result.putString(entry.getKey().getName(), entry.getValue().serialize());
        return result;
    }

    /** The vanilla categories are value objects, not an enum. */
    public static final class Category {
        public static final Category MOBS = new Category("mobs");
        public static final Category PLAYER = new Category("player");
        public static final Category UPDATES = new Category("updates");
        public static final Category CHAT = new Category("chat");
        public static final Category SPAWNING = new Category("spawning");
        public static final Category DROPS = new Category("drops");
        public static final Category MISC = new Category("misc");
        /** Compatibility alias retained for an early intermediary snapshot. */
        public static final Category UPDATES_UNUSED = UPDATES;
        private final String name;
        private Category(String name) { this.name = name; }
        public String getCategory() { return name; }
        @Override public boolean equals(Object other) {
            return other instanceof Category category && name.equals(category.name);
        }
        @Override public int hashCode() { return name.hashCode(); }
        @Override public String toString() { return name; }
    }

    public static final class Key<T extends Rule<T>> {
        private final String name;
        private final Category category;
        public Key(String name) { this(name, Category.MISC); }
        public Key(String name, Category category) {
            this.name = Objects.requireNonNull(name, "name");
            this.category = category == null ? Category.MISC : category;
        }
        /** Compatibility alias used by older source-level stubs. */
        public String id() { return name; }
        public String getName() { return name; }
        public Category getCategory() { return category; }
        public String getTranslationKey() { return "gamerule." + name; }
        @Override public boolean equals(Object other) {
            return other instanceof Key<?> key && name.equals(key.name);
        }
        @Override public int hashCode() { return name.hashCode(); }
        @Override public String toString() { return name; }
    }

    public interface Visitor {
        default void visit(Key<?> key, Type<?> type) { }
        default void visitBoolean(Key<?> key, Type<?> type) { }
        default void visitInt(Key<?> key, Type<?> type) { }
    }

    @FunctionalInterface
    public interface Acceptor {
        void call(Visitor visitor, Key<?> key, Type<?> type);
    }

    /** The factory/argument descriptor stored for each registered rule. */
    public static final class Type<T extends Rule<T>> {
        private final Supplier<?> argumentType;
        private final Function<Type<T>, T> ruleFactory;
        private final BiConsumer<MinecraftServer, T> changeCallback;
        private final Acceptor ruleAcceptor;
        private final FeatureSet requiredFeatures;

        public Type() {
            this(() -> null, ignored -> null, (server, rule) -> { },
                (visitor, key, type) -> visitor.visit(key, type), FeatureSet.EMPTY);
        }

        @SuppressWarnings("unchecked")
        public Type(Supplier<?> argumentType, Function<?, ?> ruleFactory,
                    BiConsumer<?, ?> changeCallback, Acceptor ruleAcceptor,
                    FeatureSet requiredFeatures) {
            this.argumentType = argumentType == null ? () -> null : argumentType;
            this.ruleFactory = ruleFactory == null
                ? ignored -> null : (Function<Type<T>, T>) ruleFactory;
            this.changeCallback = changeCallback == null
                ? (server, rule) -> { } : (BiConsumer<MinecraftServer, T>) changeCallback;
            this.ruleAcceptor = ruleAcceptor == null
                ? (visitor, key, type) -> visitor.visit(key, type) : ruleAcceptor;
            this.requiredFeatures = requiredFeatures == null ? FeatureSet.EMPTY : requiredFeatures;
        }

        public FeatureSet getRequiredFeatures() { return requiredFeatures; }

        @SuppressWarnings({"rawtypes", "unchecked"})
        public RequiredArgumentBuilder argument(String name) {
            Object argument = argumentType.get();
            return argument instanceof ArgumentType type
                ? RequiredArgumentBuilder.argument(name, type)
                : RequiredArgumentBuilder.argument(name, null);
        }

        public void accept(Visitor visitor, Key<?> key) {
            ruleAcceptor.call(visitor, key, this);
        }

        public T createRule() { return ruleFactory.apply(this); }
        public void onChanged(MinecraftServer server, T rule) { changeCallback.accept(server, rule); }
    }

    public abstract static class Rule<T extends Rule<T>> {
        protected final Type<T> type;

        protected Rule(Type<T> type) { this.type = type; }

        @SuppressWarnings("unchecked")
        public T getThis() { return (T) this; }
        public Type<T> getType() { return type; }
        public abstract T copy();
        public void deserialize(String value) { }
        public String serialize() { return toString(); }
        public int getCommandResult() { return 0; }

        public void setFromArgument(CommandContext<?> context, String name) {
            if (context != null) deserialize(String.valueOf(context.getArgumentRaw(name)));
        }

        public void set(CommandContext<?> context, String name) {
            setFromArgument(context, name);
        }

        public void setValue(T rule, MinecraftServer server) { }

        public void changed(MinecraftServer server) {
            if (type != null) type.onChanged(server, getThis());
        }
    }

    public static final class BooleanRule extends Rule<BooleanRule> {
        private boolean value;

        public BooleanRule(boolean value) { this(null, value); }
        public BooleanRule(Type<BooleanRule> type, boolean value) {
            super(type);
            this.value = value;
        }
        public boolean get() { return value; }
        public void set(boolean value, MinecraftServer server) {
            if (this.value == value) return;
            this.value = value;
            changed(server);
        }
        /** Source compatibility for the previous lightweight facade. */
        public void set(boolean value, Object server) { this.value = value; }
        @Override public void deserialize(String value) { this.value = Boolean.parseBoolean(value); }
        @Override public String serialize() { return Boolean.toString(value); }
        @Override public int getCommandResult() { return value ? 1 : 0; }
        @Override public void setValue(BooleanRule rule, MinecraftServer server) {
            if (rule != null) set(rule.value, server);
        }
        public static Type<BooleanRule> create(boolean initialValue) {
            return create(initialValue, (server, rule) -> { });
        }
        public static Type<BooleanRule> create(boolean initialValue,
                                                BiConsumer<MinecraftServer, BooleanRule> callback) {
            return new Type<>(BoolArgumentType::bool,
                (Function<Type<BooleanRule>, BooleanRule>) type -> new BooleanRule(type, initialValue),
                callback,
                (visitor, key, type) -> visitor.visitBoolean(key, type), FeatureSet.EMPTY);
        }
        @Override public BooleanRule copy() { return new BooleanRule(type, value); }
        @Override public String toString() { return serialize(); }
    }

    public static final class IntRule extends Rule<IntRule> {
        private int value;
        private final int minimum;
        private final int maximum;

        public IntRule(int value) { this(null, value); }
        public IntRule(Type<IntRule> type, int value) {
            this(type, value, Integer.MIN_VALUE, Integer.MAX_VALUE);
        }
        public IntRule(Type<IntRule> type, int value, int minimum, int maximum) {
            super(type);
            if (minimum > maximum) throw new IllegalArgumentException("minimum > maximum");
            this.value = clamp(value, minimum, maximum);
            this.minimum = minimum;
            this.maximum = maximum;
        }
        private static int clamp(int value, int minimum, int maximum) {
            return Math.max(minimum, Math.min(maximum, value));
        }
        public int get() { return value; }
        public void set(int value, MinecraftServer server) {
            int next = clamp(value, minimum, maximum);
            if (this.value == next) return;
            this.value = next;
            changed(server);
        }
        /** Source compatibility for the previous lightweight facade. */
        public void set(int value, Object server) { this.value = clamp(value, minimum, maximum); }
        public void setValue(int value) { this.value = clamp(value, minimum, maximum); }
        @Override public void deserialize(String value) { setValue(parseInt(value)); }
        @Override public String serialize() { return Integer.toString(value); }
        @Override public int getCommandResult() { return value; }
        public int parseInt(String input) {
            try { return Integer.parseInt(input); }
            catch (RuntimeException failure) { return this.value; }
        }
        public boolean validateAndSet(String input) {
            try {
                int parsed = Integer.parseInt(input);
                if (parsed < minimum || parsed > maximum) return false;
                value = parsed;
                return true;
            } catch (RuntimeException failure) {
                return false;
            }
        }
        @Override public void setValue(IntRule rule, MinecraftServer server) {
            if (rule != null) set(rule.value, server);
        }
        public static Type<IntRule> create(int initialValue) {
            return create(initialValue, Integer.MIN_VALUE, Integer.MAX_VALUE,
                FeatureSet.EMPTY, (server, rule) -> { });
        }
        public static Type<IntRule> create(int initialValue,
                                           BiConsumer<MinecraftServer, IntRule> callback) {
            return create(initialValue, Integer.MIN_VALUE, Integer.MAX_VALUE,
                FeatureSet.EMPTY, callback);
        }
        public static Type<IntRule> create(int initialValue, int minimum, int maximum,
                                           FeatureSet requiredFeatures,
                                           BiConsumer<MinecraftServer, IntRule> callback) {
            return new Type<>(() -> IntegerArgumentType.integer(minimum, maximum),
                (Function<Type<IntRule>, IntRule>) type ->
                    new IntRule(type, initialValue, minimum, maximum),
                callback,
                (visitor, key, type) -> visitor.visitInt(key, type), requiredFeatures);
        }
        @Override public IntRule copy() { return new IntRule(type, value, minimum, maximum); }
        @Override public String toString() { return serialize(); }
    }
}
