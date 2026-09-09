package net.minecraft.entity;

import java.util.Objects;
import java.util.function.BiFunction;
import java.util.function.Supplier;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.util.Identifier;
import net.minecraft.world.World;

/** Lightweight entity type descriptor; native entities remain authoritative. */
public class EntityType<T extends Entity> implements net.minecraft.util.TypeFilter<Entity, T> {
    @FunctionalInterface
    public interface EntityFactory<T extends Entity> {
        T create(EntityType<T> type, World world);
    }
    public static final EntityType<Entity> UNKNOWN = new EntityType<>(Identifier.of("minecraft", "unknown"), () -> null, 0.6f, 1.8f);
    public static final EntityType<Entity> PLAYER = new EntityType<>(Identifier.of("minecraft", "player"), () -> null, 0.6f, 1.8f);

    // Vanilla's 1.21.4 registry constants are part of the named ABI.  The
    // native registry owns the actual entity factories; these descriptors let
    // server-side Fabric mods resolve and compare the same keys while their
    // native counterpart remains authoritative for simulation.
    public static final EntityType<Entity> BREEZE_WIND_CHARGE = vanilla("breeze_wind_charge");
    public static final EntityType<Entity> SNIFFER = vanilla("sniffer");
    public static final EntityType<Entity> INTERACTION = vanilla("interaction");
    public static final EntityType<Entity> LIGHTNING_BOLT = vanilla("lightning_bolt");
    public static final EntityType<Entity> TURTLE = vanilla("turtle");
    public static final EntityType<Entity> END_CRYSTAL = vanilla("end_crystal");
    public static final EntityType<Entity> TROPICAL_FISH = vanilla("tropical_fish");
    public static final EntityType<Entity> GLOW_ITEM_FRAME = vanilla("glow_item_frame");
    public static final EntityType<Entity> GLOW_SQUID = vanilla("glow_squid");
    public static final EntityType<Entity> PARROT = vanilla("parrot");
    public static final EntityType<Entity> PILLAGER = vanilla("pillager");
    public static final EntityType<Entity> MAGMA_CUBE = vanilla("magma_cube");
    public static final EntityType<Entity> FISHING_BOBBER = vanilla("fishing_bobber");
    public static final EntityType<Entity> BAT = vanilla("bat");
    public static final EntityType<Entity> SHULKER = vanilla("shulker");
    public static final EntityType<Entity> GHAST = vanilla("ghast");
    public static final EntityType<Entity> SHULKER_BULLET = vanilla("shulker_bullet");
    public static final EntityType<Entity> HOGLIN = vanilla("hoglin");
    public static final EntityType<Entity> CHICKEN = vanilla("chicken");
    public static final EntityType<Entity> FIREWORK_ROCKET = vanilla("firework_rocket");
    public static final EntityType<Entity> WITHER_SKULL = vanilla("wither_skull");
    public static final EntityType<Entity> ARMOR_STAND = vanilla("armor_stand");
    public static final EntityType<Entity> COMMAND_BLOCK_MINECART = vanilla("command_block_minecart");
    public static final EntityType<Entity> SKELETON = vanilla("skeleton");
    public static final EntityType<Entity> RAVAGER = vanilla("ravager");
    public static final EntityType<Entity> SPECTRAL_ARROW = vanilla("spectral_arrow");
    public static final EntityType<Entity> ENDERMITE = vanilla("endermite");
    public static final EntityType<Entity> GOAT = vanilla("goat");
    public static final EntityType<Entity> DRAGON_FIREBALL = vanilla("dragon_fireball");
    public static final EntityType<Entity> CHEST_MINECART = vanilla("chest_minecart");
    public static final EntityType<Entity> TRIDENT = vanilla("trident");
    public static final EntityType<Entity> CAMEL = vanilla("camel");
    public static final EntityType<Entity> PAINTING = vanilla("painting");
    public static final EntityType<Entity> LLAMA_SPIT = vanilla("llama_spit");
    public static final EntityType<Entity> SILVERFISH = vanilla("silverfish");
    public static final EntityType<Entity> ARROW = vanilla("arrow");
    public static final EntityType<Entity> DROWNED = vanilla("drowned");
    public static final EntityType<Entity> ENDER_DRAGON = vanilla("ender_dragon");
    public static final EntityType<Entity> VINDICATOR = vanilla("vindicator");
    public static final EntityType<Entity> SQUID = vanilla("squid");
    public static final EntityType<Entity> SHEEP = vanilla("sheep");
    public static final EntityType<Entity> GUARDIAN = vanilla("guardian");
    public static final EntityType<Entity> WITHER = vanilla("wither");
    public static final EntityType<Entity> WANDERING_TRADER = vanilla("wandering_trader");
    public static final EntityType<Entity> TRADER_LLAMA = vanilla("trader_llama");
    public static final EntityType<Entity> EGG = vanilla("egg");
    public static final EntityType<Entity> WITCH = vanilla("witch");
    public static final EntityType<Entity> SPAWNER_MINECART = vanilla("spawner_minecart");
    public static final EntityType<Entity> MOOSHROOM = vanilla("mooshroom");
    public static final EntityType<Entity> PANDA = vanilla("panda");
    public static final EntityType<Entity> IRON_GOLEM = vanilla("iron_golem");
    public static final EntityType<Entity> RABBIT = vanilla("rabbit");
    public static final EntityType<Entity> WARDEN = vanilla("warden");
    public static final EntityType<Entity> CAT = vanilla("cat");
    public static final EntityType<Entity> LEASH_KNOT = vanilla("leash_knot");
    public static final EntityType<Entity> HORSE = vanilla("horse");
    public static final EntityType<Entity> SNOW_GOLEM = vanilla("snow_golem");
    public static final EntityType<Entity> ZOMBIE_HORSE = vanilla("zombie_horse");
    public static final EntityType<Entity> POTION = vanilla("potion");
    public static final EntityType<Entity> CREEPER = vanilla("creeper");
    public static final EntityType<Entity> SMALL_FIREBALL = vanilla("small_fireball");
    public static final EntityType<Entity> JUNGLE_BOAT = vanilla("jungle_boat");
    public static final EntityType<Entity> OAK_BOAT = vanilla("oak_boat");
    public static final EntityType<Entity> OAK_CHEST_BOAT = vanilla("oak_chest_boat");
    public static final EntityType<Entity> MANGROVE_BOAT = vanilla("mangrove_boat");
    public static final EntityType<Entity> JUNGLE_CHEST_BOAT = vanilla("jungle_chest_boat");
    public static final EntityType<Entity> SPRUCE_BOAT = vanilla("spruce_boat");
    public static final EntityType<Entity> ITEM_FRAME = vanilla("item_frame");
    public static final EntityType<Entity> ACACIA_CHEST_BOAT = vanilla("acacia_chest_boat");
    public static final EntityType<Entity> EXPERIENCE_ORB = vanilla("experience_orb");
    public static final EntityType<Entity> PIGLIN_BRUTE = vanilla("piglin_brute");
    public static final EntityType<Entity> BAMBOO_CHEST_RAFT = vanilla("bamboo_chest_raft");
    public static final EntityType<Entity> SPRUCE_CHEST_BOAT = vanilla("spruce_chest_boat");
    public static final EntityType<Entity> POLAR_BEAR = vanilla("polar_bear");
    public static final EntityType<Entity> BAMBOO_RAFT = vanilla("bamboo_raft");
    public static final EntityType<Entity> PIGLIN = vanilla("piglin");
    public static final EntityType<Entity> STRIDER = vanilla("strider");
    public static final EntityType<Entity> DARK_OAK_BOAT = vanilla("dark_oak_boat");
    public static final EntityType<Entity> ACACIA_BOAT = vanilla("acacia_boat");
    public static final EntityType<Entity> DARK_OAK_CHEST_BOAT = vanilla("dark_oak_chest_boat");
    public static final EntityType<Entity> MANGROVE_CHEST_BOAT = vanilla("mangrove_chest_boat");
    public static final EntityType<Entity> FOX = vanilla("fox");
    public static final EntityType<Entity> CREAKING = vanilla("creaking");
    public static final EntityType<Entity> SLIME = vanilla("slime");
    public static final EntityType<Entity> PALE_OAK_BOAT = vanilla("pale_oak_boat");
    public static final EntityType<Entity> PALE_OAK_CHEST_BOAT = vanilla("pale_oak_chest_boat");
    public static final EntityType<Entity> TNT = vanilla("tnt");
    public static final EntityType<Entity> EXPERIENCE_BOTTLE = vanilla("experience_bottle");
    public static final EntityType<Entity> EYE_OF_ENDER = vanilla("eye_of_ender");
    public static final EntityType<Entity> PUFFERFISH = vanilla("pufferfish");
    public static final EntityType<Entity> DONKEY = vanilla("donkey");
    public static final EntityType<Entity> SNOWBALL = vanilla("snowball");
    public static final EntityType<Entity> ILLUSIONER = vanilla("illusioner");
    public static final EntityType<Entity> FIREBALL = vanilla("fireball");
    public static final EntityType<Entity> BEE = vanilla("bee");
    public static final EntityType<Entity> EVOKER_FANGS = vanilla("evoker_fangs");
    public static final EntityType<Entity> MARKER = vanilla("marker");
    public static final EntityType<Entity> VEX = vanilla("vex");
    public static final EntityType<Entity> MULE = vanilla("mule");
    public static final EntityType<Entity> HOPPER_MINECART = vanilla("hopper_minecart");
    public static final EntityType<Entity> BIRCH_CHEST_BOAT = vanilla("birch_chest_boat");
    public static final EntityType<Entity> CHERRY_BOAT = vanilla("cherry_boat");
    public static final EntityType<Entity> BIRCH_BOAT = vanilla("birch_boat");
    public static final EntityType<Entity> ZOMBIE = vanilla("zombie");
    public static final EntityType<Entity> CHERRY_CHEST_BOAT = vanilla("cherry_chest_boat");
    public static final EntityType<Entity> ITEM = vanilla("item");
    public static final EntityType<Entity> ZOMBIFIED_PIGLIN = vanilla("zombified_piglin");
    public static final EntityType<Entity> WOLF = vanilla("wolf");
    public static final EntityType<Entity> TNT_MINECART = vanilla("tnt_minecart");
    public static final EntityType<Entity> OMINOUS_ITEM_SPAWNER = vanilla("ominous_item_spawner");
    public static final EntityType<Entity> ZOMBIE_VILLAGER = vanilla("zombie_villager");
    public static final EntityType<Entity> ITEM_DISPLAY = vanilla("item_display");
    public static final EntityType<Entity> TEXT_DISPLAY = vanilla("text_display");
    public static final EntityType<Entity> DOLPHIN = vanilla("dolphin");
    public static final EntityType<Entity> COW = vanilla("cow");
    public static final EntityType<Entity> ELDER_GUARDIAN = vanilla("elder_guardian");
    public static final EntityType<Entity> FALLING_BLOCK = vanilla("falling_block");
    public static final EntityType<Entity> FURNACE_MINECART = vanilla("furnace_minecart");
    public static final EntityType<Entity> BLOCK_DISPLAY = vanilla("block_display");
    public static final EntityType<Entity> AREA_EFFECT_CLOUD = vanilla("area_effect_cloud");
    public static final EntityType<Entity> CAVE_SPIDER = vanilla("cave_spider");
    public static final EntityType<Entity> OCELOT = vanilla("ocelot");
    public static final EntityType<Entity> ENDER_PEARL = vanilla("ender_pearl");
    public static final EntityType<Entity> BOGGED = vanilla("bogged");
    public static final EntityType<Entity> ZOGLIN = vanilla("zoglin");
    public static final EntityType<Entity> SKELETON_HORSE = vanilla("skeleton_horse");
    public static final EntityType<Entity> WITHER_SKELETON = vanilla("wither_skeleton");
    public static final EntityType<Entity> SALMON = vanilla("salmon");
    public static final EntityType<Entity> LLAMA = vanilla("llama");
    public static final EntityType<Entity> SPIDER = vanilla("spider");
    public static final EntityType<Entity> VILLAGER = vanilla("villager");
    public static final EntityType<Entity> PHANTOM = vanilla("phantom");
    public static final EntityType<Entity> HUSK = vanilla("husk");
    public static final EntityType<Entity> COD = vanilla("cod");
    public static final EntityType<Entity> BREEZE = vanilla("breeze");
    public static final EntityType<Entity> WIND_CHARGE = vanilla("wind_charge");
    public static final EntityType<Entity> ALLAY = vanilla("allay");
    public static final EntityType<Entity> AXOLOTL = vanilla("axolotl");
    public static final EntityType<Entity> FROG = vanilla("frog");
    public static final EntityType<Entity> BLAZE = vanilla("blaze");
    public static final EntityType<Entity> ARMADILLO = vanilla("armadillo");
    public static final EntityType<Entity> STRAY = vanilla("stray");
    public static final EntityType<Entity> ENDERMAN = vanilla("enderman");
    public static final EntityType<Entity> EVOKER = vanilla("evoker");
    public static final EntityType<Entity> GIANT = vanilla("giant");
    public static final EntityType<Entity> MINECART = vanilla("minecart");
    public static final EntityType<Entity> PIG = vanilla("pig");
    public static final EntityType<Entity> TADPOLE = vanilla("tadpole");
    private final Identifier id;
    private final Supplier<? extends T> factory;
    private final BiFunction<EntityType<T>, World, ? extends T> worldFactory;
    private final float width;
    private final float height;
    private SpawnGroup spawnGroup = SpawnGroup.MISC;
    private boolean alwaysUpdateVelocity;
    private boolean canPotentiallyExecuteCommands;
    private net.minecraft.entity.attribute.DefaultAttributeContainer defaultAttributes;

    private static EntityType<Entity> vanilla(String path) {
        return new EntityType<>(Identifier.ofVanilla(path), () -> null, 0.6f, 1.8f);
    }

    public EntityType(Identifier id, Supplier<? extends T> factory, float width, float height) {
        this.id = id == null ? Identifier.of("minecraft", "unknown") : id;
        this.factory = factory == null ? () -> null : factory;
        this.worldFactory = null;
        this.width = width; this.height = height;
    }
    public EntityType(Identifier id, BiFunction<EntityType<T>, World, ? extends T> factory, float width, float height) {
        this.id = id == null ? Identifier.of("minecraft", "unknown") : id;
        this.factory = null;
        this.worldFactory = factory;
        this.width = width; this.height = height;
    }
    public Identifier getId() { return id; }
    /** Static registry-id helper exposed by the 1.21.4 Yarn ABI. */
    public static Identifier getId(EntityType<?> type) {
        return type == null ? null : type.getId();
    }
    /** EntityType is itself the vanilla TypeFilter for entities of this type. */
    @Override public Class<? extends Entity> getBaseClass() { return Entity.class; }
    /** Return the entity only when its registered type is this descriptor. */
    @Override @SuppressWarnings("unchecked")
    public T downcast(Entity entity) {
        return entity != null && equals(entity.getType()) ? (T) entity : null;
    }
    public static EntityType<?> byId(String id) {
        Identifier key = Identifier.tryParse(id);
        if (key == null) return UNKNOWN;
        EntityType<?> value = net.minecraft.registry.Registries.ENTITY_TYPE.get(key);
        return value == null ? UNKNOWN : value;
    }
    public static java.util.Optional<EntityType<?>> get(String id) {
        Identifier key = Identifier.tryParse(id);
        if (key == null) return java.util.Optional.empty();
        EntityType<?> value = net.minecraft.registry.Registries.ENTITY_TYPE.get(key);
        return java.util.Optional.ofNullable(value == null ? null : value);
    }

    /** Vanilla builder surface used by Fabric's entity-type builder API. */
    public static class Builder<T extends Entity> {
        private final EntityFactory<T> factory;
        private final SpawnGroup spawnGroup;
        private EntityDimensions dimensions = EntityDimensions.changing(0.6f, 1.8f);
        private boolean alwaysUpdateVelocity;
        private boolean canPotentiallyExecuteCommands;
        private boolean disableSummon;
        private boolean disableSaving;
        private boolean fireImmune;
        private boolean spawnableFarFromPlayer;
        private int trackingRange = 5;
        private int updateRate = 3;
        private boolean forceVelocityUpdates;
        private net.minecraft.entity.attribute.DefaultAttributeContainer defaultAttributes;

        public Builder(EntityFactory<T> factory, SpawnGroup spawnGroup) {
            this.factory = factory; this.spawnGroup = spawnGroup;
        }
        public static <T extends Entity> Builder<T> create(EntityFactory<T> factory, SpawnGroup group) {
            return new Builder<>(factory, group);
        }
        public Builder<T> dimensions(EntityDimensions value) { if (value != null) dimensions = value; return this; }
        public Builder<T> alwaysUpdateVelocity(boolean value) { alwaysUpdateVelocity = value; return this; }
        public Builder<T> canPotentiallyExecuteCommands(boolean value) { canPotentiallyExecuteCommands = value; return this; }
        public Builder<T> disableSummon() { disableSummon = true; return this; }
        public Builder<T> disableSaving() { disableSaving = true; return this; }
        public Builder<T> fireImmune() { fireImmune = true; return this; }
        public Builder<T> spawnableFarFromPlayer() { spawnableFarFromPlayer = true; return this; }
        public Builder<T> trackable(int range, int rate) { trackingRange = range; updateRate = rate; return this; }
        public Builder<T> trackable(int range, int rate, boolean forceVelocity) { trackingRange = range; updateRate = rate; forceVelocityUpdates = forceVelocity; return this; }
        public Builder<T> trackRangeChunks(int range) { trackingRange = range * 16; return this; }
        public Builder<T> trackRangeBlocks(int range) { trackingRange = range; return this; }
        public Builder<T> trackedUpdateRate(int rate) { updateRate = rate; return this; }
        public Builder<T> forceTrackedVelocityUpdates(boolean value) { forceVelocityUpdates = value; return this; }
        public Builder<T> defaultAttributes(net.minecraft.entity.attribute.DefaultAttributeContainer.Builder value) { defaultAttributes = value == null ? null : value.build(); return this; }
        public EntityType<T> build(net.minecraft.registry.RegistryKey<EntityType<?>> key) {
            Identifier id = key == null ? Identifier.of("cppfm", "unregistered_entity") : key.getValue();
            EntityType<T> result = new EntityType<>(id, (type, world) -> factory == null ? null : factory.create(type, world), dimensions.width(), dimensions.height());
            result.spawnGroup = spawnGroup;
            result.alwaysUpdateVelocity = alwaysUpdateVelocity;
            result.canPotentiallyExecuteCommands = canPotentiallyExecuteCommands;
            result.defaultAttributes = defaultAttributes;
            return result;
        }
    }
    public float getWidth() { return width; }
    public float getHeight() { return height; }
    /** Yarn 1.21.4 EntityType hook used by server-side activation-range mods. */
    public boolean alwaysUpdateVelocity() { return alwaysUpdateVelocity; }
    /** Yarn 1.21.4 command-source capability hook used by server optimizers. */
    public boolean canPotentiallyExecuteCommands() { return canPotentiallyExecuteCommands; }
    public SpawnGroup getSpawnGroup() { return spawnGroup; }
    public net.minecraft.entity.attribute.DefaultAttributeContainer getDefaultAttributes() { return defaultAttributes; }
    public T create(World world) {
        T entity = worldFactory == null ? factory.get() : worldFactory.apply(this, world);
        if (entity != null && world != null) entity.setWorld(world);
        return entity;
    }
    public String getTranslationKey() { return "entity." + id.getNamespace() + "." + id.getPath().replace('/', '.'); }
    public RegistryEntry<EntityType<T>> getRegistryEntry() {
        @SuppressWarnings("unchecked") RegistryEntry<EntityType<T>> entry = (RegistryEntry<EntityType<T>>) (RegistryEntry<?>)
            net.minecraft.registry.Registries.ENTITY_TYPE.getEntry((EntityType<?>) this).orElse(null);
        return entry;
    }
    public net.minecraft.registry.entry.RegistryEntry<EntityType<T>> getCanonicalRegistryEntry() {
        return getRegistryEntry();
    }
    @Override public boolean equals(Object other) { return other instanceof EntityType<?> t && id.equals(t.id); }
    @Override public int hashCode() { return Objects.hash(id); }
    @Override public String toString() { return id.toString(); }
}
