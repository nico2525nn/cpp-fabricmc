package net.minecraft.item;

import java.util.Objects;
import java.util.concurrent.atomic.AtomicInteger;
import net.minecraft.block.BlockState;
import net.minecraft.registry.RegistryEntry;
import net.minecraft.text.Text;
import net.minecraft.util.ActionResult;
import net.minecraft.util.Identifier;
import net.minecraft.util.TypedActionResult;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;
import net.minecraft.entity.player.PlayerEntity;

public class Item {
    private static final AtomicInteger NEXT_CUSTOM_ID = new AtomicInteger(10000);
    private final Identifier id;
    private final int rawState;
    private final Settings settings;

    public static class Settings {
        private int maxCount = 64;
        private int maxDamage;
        private boolean fireproof;
        private Rarity rarity = Rarity.COMMON;
        private FoodComponent food;
        private Item recipeRemainder;
        public Settings maxCount(int count) { if (count < 1) throw new IllegalArgumentException("maxCount must be positive"); maxCount = count; maxDamage = 0; return this; }
        public Settings maxDamage(int damage) { if (damage < 1) throw new IllegalArgumentException("maxDamage must be positive"); maxDamage = damage; maxCount = 1; return this; }
        public Settings fireproof() { fireproof = true; return this; }
        public Settings rarity(Rarity value) { rarity = value == null ? Rarity.COMMON : value; return this; }
        public Settings food(FoodComponent value) { food = value; return this; }
        public Settings recipeRemainder(Item value) { recipeRemainder = value; return this; }
        public <T> Settings component(net.minecraft.component.DataComponentType<T> type, T value) { return this; }
        public Settings attributeModifiers(Object modifiers) { return this; }
        public int maxCount() { return maxCount; }
        public int maxDamage() { return maxDamage; }
        public boolean isFireproof() { return fireproof; }
        public Rarity rarity() { return rarity; }
        public FoodComponent food() { return food; }
        public Item recipeRemainder() { return recipeRemainder; }
    }

    public Item() { this(Identifier.of("minecraft", "air"), 0, new Settings()); }
    public Item(Identifier id) { this(id, 0, new Settings()); }
    public Item(Identifier id, int rawState) { this(id, rawState, new Settings()); }
    public Item(Identifier id, Settings settings) { this(id, NEXT_CUSTOM_ID.getAndIncrement(), settings); }
    private Item(Identifier id, int rawState, Settings settings) {
        this.id = id == null ? Identifier.of("minecraft", "air") : id;
        this.rawState = Math.max(0, rawState);
        this.settings = settings == null ? new Settings() : settings;
    }
    public Item(Settings settings) { this(Identifier.of("cppfm", "custom_item_" + NEXT_CUSTOM_ID.get()), settings); }
    public static Item fromRaw(int rawId, String name) {
        Identifier id = Identifier.tryParse(name == null || name.isEmpty() ? "minecraft:air" : name);
        if (id == null) id = Identifier.of("minecraft", "air");
        Item registered = net.minecraft.registry.Registries.ITEM.get(id);
        return registered == null ? new Item(id, rawId) : registered;
    }
    public Identifier getId() { return id; }
    public int getRawState() { return rawState; }
    public int getRawId() { return rawState; }
    public Settings getSettings() { return settings; }
    public int getMaxCount() { return settings.maxCount(); }
    public int getMaxDamage() { return settings.maxDamage(); }
    public boolean isDamageable() { return getMaxDamage() > 0; }
    public boolean isFood() { return settings.food() != null; }
    public FoodComponent getFoodComponent() { return settings.food(); }
    public Item getRecipeRemainder() { return settings.recipeRemainder(); }
    public Rarity getRarity(ItemStack stack) { return settings.rarity(); }
    public String getTranslationKey() { return "item." + id.getNamespace() + "." + id.getPath().replace('/', '.'); }
    public Text getName(ItemStack stack) { return Text.translatable(getTranslationKey()); }
    public ItemStack getDefaultStack() { return new ItemStack(this); }
    public TypedActionResult<ItemStack> use(World world, PlayerEntity user, net.minecraft.util.Hand hand) {
        return TypedActionResult.pass(user == null ? ItemStack.EMPTY : user.getStackInHand(hand));
    }
    public TypedActionResult<ItemStack> useOnBlock(net.minecraft.item.ItemUsageContext context) {
        return TypedActionResult.pass(context == null ? ItemStack.EMPTY : context.getStack());
    }
    public ActionResult place(net.minecraft.item.ItemPlacementContext context) { return ActionResult.PASS; }
    public void inventoryTick(ItemStack stack, World world, net.minecraft.entity.Entity entity, int slot, boolean selected) { }
    public UseAction getUseAction(ItemStack stack) { return isFood() ? UseAction.EAT : UseAction.NONE; }
    public int getMaxUseTime(ItemStack stack) { return isFood() ? 32 : 0; }
    public boolean isOf(Item other) { return this == other || (other != null && id.equals(other.id)); }
    public RegistryEntry<Item> getRegistryEntry() { return net.minecraft.registry.Registries.ITEM.getEntry(this).orElse(null); }
    @Override public boolean equals(Object other) { return other instanceof Item item && id.equals(item.id); }
    @Override public int hashCode() { return Objects.hash(id); }
    @Override public String toString() { return id.toString(); }
}
