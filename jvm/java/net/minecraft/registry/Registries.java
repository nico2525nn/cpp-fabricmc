package net.minecraft.registry;

import com.mojang.serialization.Lifecycle;
import java.util.Map;
import net.minecraft.block.Block;
import net.minecraft.block.Blocks;
import net.minecraft.item.Item;
import net.minecraft.item.Items;
import net.minecraft.util.Identifier;
import net.minecraft.entity.EntityType;
import net.minecraft.fluid.Fluid;
import net.minecraft.fluid.Fluids;
import net.minecraft.sound.SoundEvent;
import net.minecraft.world.biome.Biome;
import net.minecraft.enchantment.Enchantment;
import net.minecraft.world.gen.structure.Structure;
import net.minecraft.loot.LootTable;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** Process-local registry views backed by Java identity, not native IDs. */
public final class Registries {
    private Registries() {}

    private static <T> Registry<T> registry(String path) {
        return new SimpleRegistry<>(RegistryKey.of(RegistryKeys.ROOT,
            Identifier.of("minecraft", path)));
    }

    private static <T> DefaultedRegistry<T> defaultedRegistry(String path, String defaultId) {
        return new SimpleDefaultedRegistry<>(defaultId,
            RegistryKey.of(RegistryKeys.ROOT, Identifier.of("minecraft", path)),
            Lifecycle.stable(), false);
    }

    /**
     * Keeps the vanilla bootstrap entry point available to Fabric's registry
     * access widener.  Static field initialization performs the embedded
     * registry bootstrap; this method is intentionally idempotent just like
     * the vanilla call site.
     */
    public static void init() { }

    /** Called by Fabric Registry Sync after vanilla registry bootstrap. */
    public static void freezeRegistries() { }

    /** Registry-of-registries.  Fabric's accessor contract requires MutableRegistry. */
    public static final MutableRegistry<Registry<?>> ROOT = new SimpleRegistry<>(RegistryKeys.ROOT);
    public static final Registry<Registry<?>> REGISTRIES = ROOT;
    public static final Logger LOGGER = LoggerFactory.getLogger(Registries.class);

    // The field descriptors below intentionally mirror Yarn 1.21.4.  Several
    // Fabric modules touch this catalog during bootstrap even when they never
    // register a value in the corresponding registry.
    public static final Registry<Object> LOOT_SCORE_PROVIDER_TYPE = registry("loot_score_provider_type");
    public static final Registry<Object> ENCHANTMENT_ENTITY_EFFECT_TYPE = registry("enchantment_entity_effect_type");
    public static final Registry<Object> DATA_COMPONENT_TYPE = registry("data_component_type");
    public static final Registry<Object> TREE_DECORATOR_TYPE = registry("tree_decorator_type");
    public static final Registry<Object> HEIGHT_PROVIDER_TYPE = registry("height_provider_type");
    public static final Registry<Object> STAT_TYPE = registry("stat_type");
    public static final Registry<Object> BLOCK_ENTITY_TYPE = registry("block_entity_type");
    public static final Registry<Object> MAP_DECORATION_TYPE = registry("map_decoration_type");
    public static final Registry<Object> BLOCK_STATE_PROVIDER_TYPE = registry("block_state_provider_type");
    public static final Registry<Object> RECIPE_SERIALIZER = registry("recipe_serializer");
    public static final Registry<Object> LOOT_NBT_PROVIDER_TYPE = registry("loot_nbt_provider_type");
    public static final Registry<Object> ENCHANTMENT_LEVEL_BASED_VALUE_TYPE = registry("enchantment_level_based_value_type");
    public static final Registry<Object> DECORATED_POT_PATTERN = registry("decorated_pot_pattern");
    public static final Registry<Object> PARTICLE_TYPE = registry("particle_type");
    public static final Registry<Object> CRITERION = registry("criterion");
    public static final Registry<Object> FROG_VARIANT = registry("frog_variant");
    public static final Registry<Object> ROOT_PLACER_TYPE = registry("root_placer_type");
    public static final Registry<Object> RECIPE_TYPE = registry("recipe_type");
    public static final Registry<Object> COMMAND_ARGUMENT_TYPE = registry("command_argument_type");
    public static final Registry<Object> INT_PROVIDER_TYPE = registry("int_provider_type");
    public static final Registry<Object> PLACEMENT_MODIFIER_TYPE = registry("placement_modifier_type");
    public static final Registry<Object> LOOT_NUMBER_PROVIDER_TYPE = registry("loot_number_provider_type");
    public static final Registry<Object> ENCHANTMENT_VALUE_EFFECT_TYPE = registry("enchantment_value_effect_type");
    public static final Registry<Object> POSITION_SOURCE_TYPE = registry("position_source_type");
    public static final Registry<Object> CAT_VARIANT = registry("cat_variant");
    public static final Registry<Object> SCREEN_HANDLER = registry("screen_handler");
    public static final Map<?, ?> DEFAULT_ENTRIES = Map.of();
    public static final Registry<Object> TRUNK_PLACER_TYPE = registry("trunk_placer_type");
    public static final Registry<Object> LOOT_CONDITION_TYPE = registry("loot_condition_type");
    public static final Registry<Object> ENCHANTMENT_LOCATION_BASED_EFFECT_TYPE = registry("enchantment_location_based_effect_type");
    public static final Registry<Object> MATERIAL_RULE = registry("material_rule");
    public static final Registry<Object> STRUCTURE_TYPE = registry("structure_type");
    public static final Registry<Object> ATTRIBUTE = registry("attribute");
    public static final Registry<Object> CONSUME_EFFECT_TYPE = registry("consume_effect_type");
    public static final Registry<Object> POS_RULE_TEST = registry("pos_rule_test");
    public static final Registry<Object> POOL_ALIAS_BINDING = registry("pool_alias_binding");
    public static final Registry<Object> RULE_TEST = registry("rule_test");
    public static final Registry<Object> LOOT_POOL_ENTRY_TYPE = registry("loot_pool_entry_type");
    public static final Registry<Object> STRUCTURE_PROCESSOR = registry("structure_processor");
    public static final Registry<Object> STATUS_EFFECT = registry("mob_effect");
    public static final Registry<Object> LOOT_FUNCTION_TYPE = registry("loot_function_type");
    public static final Registry<Object> MATERIAL_CONDITION = registry("material_condition");
    public static final Registry<Object> STRUCTURE_POOL_ELEMENT = registry("structure_pool_element");
    public static final Registry<Object> FOLIAGE_PLACER_TYPE = registry("foliage_placer_type");
    public static final Registry<Object> STRUCTURE_PIECE = registry("structure_piece");
    public static final Registry<Object> CHUNK_GENERATOR = registry("chunk_generator");
    public static final Registry<Object> STRUCTURE_PLACEMENT = registry("structure_placement");
    public static final Registry<Object> ENCHANTMENT_PROVIDER_TYPE = registry("enchantment_provider_type");
    public static final Registry<Object> RULE_BLOCK_ENTITY_MODIFIER = registry("rule_block_entity_modifier");
    public static final Registry<Object> FEATURE = registry("configured_feature");
    public static final Registry<Object> ACTIVITY = registry("activity");
    public static final Registry<Object> DENSITY_FUNCTION_TYPE = registry("density_function_type");
    public static final Registry<SoundEvent> SOUND_EVENT = registry("sound_event");
    public static final Registry<Object> POINT_OF_INTEREST_TYPE = registry("point_of_interest_type");
    public static final Registry<Object> SLOT_DISPLAY = registry("slot_display");
    public static final Registry<Object> BIOME_SOURCE = registry("worldgen/biome_source");
    public static final Registry<Biome> BIOME = registry("worldgen/biome");
    public static final Registry<Enchantment> ENCHANTMENT = registry("enchantment");
    public static final Registry<Structure> STRUCTURE = registry("worldgen/structure");
    public static final Registry<LootTable> LOOT_TABLE = registry("loot_table");
    public static final Registry<Object> ENCHANTMENT_EFFECT_COMPONENT_TYPE = registry("enchantment_effect_component_type");
    public static final Registry<Object> ENTITY_SUB_PREDICATE_TYPE = registry("entity_sub_predicate_type");
    public static final Registry<Object> ITEM_SUB_PREDICATE_TYPE = registry("item_sub_predicate_type");
    public static final Registry<Object> SCHEDULE = registry("schedule");
    public static final Registry<Object> ITEM_GROUP = registry("item_group");
    public static final Registry<Object> FEATURE_SIZE_TYPE = registry("feature_size_type");
    public static final Registry<Object> CARVER = registry("configured_carver");
    public static final Registry<Object> CUSTOM_STAT = registry("custom_stat");
    public static final Registry<Object> FLOAT_PROVIDER_TYPE = registry("float_provider_type");
    public static final Registry<Object> RECIPE_DISPLAY = registry("recipe_display");
    public static final Registry<Object> RECIPE_BOOK_CATEGORY = registry("recipe_book_category");
    public static final Registry<Object> POTION = registry("potion");
    public static final Registry<Object> BLOCK_TYPE = registry("block_type");
    public static final Registry<Object> NUMBER_FORMAT_TYPE = registry("number_format_type");
    public static final Registry<Object> BLOCK_PREDICATE_TYPE = registry("block_predicate_type");

    // These registries are DefaultedRegistry in the official descriptor.
    public static final DefaultedRegistry<Object> SENSOR_TYPE = defaultedRegistry("sensor", "minecraft:dummy");
    public static final DefaultedRegistry<Object> VILLAGER_TYPE = defaultedRegistry("villager_type", "minecraft:plains");
    public static final DefaultedRegistry<Object> VILLAGER_PROFESSION = defaultedRegistry("villager_profession", "minecraft:none");
    public static final DefaultedRegistry<Object> GAME_EVENT = defaultedRegistry("game_event", "minecraft:step");
    public static final DefaultedRegistry<EntityType<?>> ENTITY_TYPE = defaultedRegistry("entity_type", "minecraft:pig");
    public static final DefaultedRegistry<Item> ITEM = defaultedRegistry("item", "minecraft:air");
    public static final DefaultedRegistry<Block> BLOCK = defaultedRegistry("block", "minecraft:air");
    public static final DefaultedRegistry<Object> CHUNK_STATUS = defaultedRegistry("chunk_status", "minecraft:empty");
    public static final DefaultedRegistry<Object> MEMORY_MODULE_TYPE = defaultedRegistry("memory_module_type", "minecraft:dummy");
    public static final DefaultedRegistry<Fluid> FLUID = defaultedRegistry("fluid", "minecraft:empty");

    static {
        registerIfAbsent(BLOCK, Identifier.of("minecraft", "air"), Blocks.AIR);
        registerIfAbsent(ITEM, Identifier.of("minecraft", "air"), Items.AIR);
        for (Block block : Blocks.values()) registerIfAbsent(BLOCK, block.getId(), block);
        for (Item item : Items.values()) registerIfAbsent(ITEM, item.getId(), item);
        registerIfAbsent(ENTITY_TYPE, Identifier.of("minecraft", "player"), EntityType.PLAYER);
        registerIfAbsent(FLUID, Identifier.of("minecraft", "empty"), Fluids.EMPTY);
        registerIfAbsent(FLUID, Identifier.of("minecraft", "water"), Fluids.WATER);
        registerIfAbsent(FLUID, Identifier.of("minecraft", "lava"), Fluids.LAVA);
        registerIfAbsent(ROOT, RegistryKeys.BLOCK.getValue(), BLOCK);
        registerIfAbsent(ROOT, RegistryKeys.ITEM.getValue(), ITEM);
        registerIfAbsent(ROOT, RegistryKeys.ENTITY_TYPE.getValue(), ENTITY_TYPE);
        registerIfAbsent(ROOT, RegistryKeys.FLUID.getValue(), FLUID);
        registerIfAbsent(ROOT, RegistryKeys.BIOME.getValue(), BIOME);
        registerIfAbsent(ROOT, RegistryKeys.ENCHANTMENT.getValue(), ENCHANTMENT);
        registerIfAbsent(ROOT, RegistryKeys.STRUCTURE.getValue(), STRUCTURE);
        registerIfAbsent(ROOT, RegistryKeys.LOOT_TABLE.getValue(), LOOT_TABLE);
    }
    private static <T> void registerIfAbsent(Registry<T> registry, Identifier id, T value) {
        if (!registry.containsId(id)) Registry.register(registry, id, value);
    }
    public static Registry<?> byKey(RegistryKey<?> key) {
        if (RegistryKeys.BLOCK.equals(key)) return BLOCK;
        if (RegistryKeys.ITEM.equals(key)) return ITEM;
        if (RegistryKeys.ENTITY_TYPE.equals(key)) return ENTITY_TYPE;
        if (RegistryKeys.FLUID.equals(key)) return FLUID;
        if (RegistryKeys.BIOME.equals(key)) return BIOME;
        if (RegistryKeys.ENCHANTMENT.equals(key)) return ENCHANTMENT;
        if (RegistryKeys.STRUCTURE.equals(key)) return STRUCTURE;
        if (RegistryKeys.LOOT_TABLE.equals(key)) return LOOT_TABLE;
        if (RegistryKeys.ROOT.equals(key)) return ROOT;
        if (key != null) {
            Registry<?> registered = ROOT.get(key.getValue());
            if (registered != null) return registered;
        }
        return null;
    }
}
