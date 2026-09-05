package cppfm.shadowabi;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.block.AbstractBlock;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.command.CommandRegistryAccess;
import net.minecraft.component.type.FoodComponent;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.registry.RegistryEntry;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.server.MinecraftServer;
import net.minecraft.text.Text;
import net.minecraft.util.Identifier;
import net.minecraft.util.Formatting;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.World;
import net.minecraft.world.border.WorldBorder;
import net.minecraft.world.border.WorldBorderStage;
import net.minecraft.world.dimension.DimensionType;

/** Compile/runtime fixture for the canonical shadow package and ABI surface. */
public final class ShadowAbiFixture {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    public static void main(String[] args) throws Throwable {
        check(Block.class.getSuperclass() == AbstractBlock.class, "Block inheritance");
        check(World.class.getInterfaces().length == 3, "World interface count");
        check(World.class.getInterfaces()[0] == net.minecraft.world.BlockView.class,
            "World BlockView interface");

        Field settings = AbstractBlock.class.getDeclaredField("settings");
        check(Modifier.isProtected(settings.getModifiers()) && Modifier.isFinal(settings.getModifiers()),
            "AbstractBlock.settings visibility");
        Constructor<Block> blockConstructor = Block.class.getConstructor(AbstractBlock.Settings.class);
        AbstractBlock.Settings blockSettings = AbstractBlock.Settings.create()
            .sounds(new net.minecraft.sound.BlockSoundGroup("stone"))
            .pistonBehavior(net.minecraft.block.piston.PistonBehavior.NORMAL);
        Block block = blockConstructor.newInstance(blockSettings);
        check(block.getSettings() == blockSettings, "settings identity");
        check(block.getDefaultState().getBlock() == block, "default block state");
        check(block.asItem() instanceof net.minecraft.item.BlockItem, "canonical BlockItem");

        Item.Settings itemSettings = new Item.Settings()
            .food(new FoodComponent.Builder().nutrition(4).saturationModifier(0.3f).build())
            .rarity(net.minecraft.util.Rarity.RARE);
        Item item = new Item(Identifier.of("cppfm", "fixture_item"), itemSettings);
        ItemStack stack = new ItemStack(item);
        check(item.getCanonicalFoodComponent().nutrition() == 4, "food component");
        check(item.getCanonicalRarity(stack) == net.minecraft.util.Rarity.RARE, "rarity");
        check(item.getCanonicalUseAction(stack) == net.minecraft.item.consume.UseAction.EAT, "use action");
        check(Text.literal("fixture").formatted(Formatting.GREEN).getString().equals("fixture"),
            "canonical formatting");

        World world = World.of(1L);
        check(World.class.getMethod("getWorldBorder").getReturnType() == WorldBorder.class,
            "WorldBorder descriptor");
        check(World.class.getMethod("getDimension").getReturnType() == DimensionType.class,
            "DimensionType descriptor");
        WorldBorder border = world.getWorldBorder();
        border.setCenter(0.0, 0.0);
        border.setSize(32.0);
        check(border.contains(new BlockPos(0, 64, 0)), "border BlockPos");
        check(border.contains(new Vec3d(0.0, 64.0, 0.0)), "border Vec3d");
        check(border.getStage() == WorldBorderStage.STATIONARY, "border stage");

        RegistryKey<net.minecraft.registry.Registry<Item>> itemRegistry = RegistryKeys.ITEM;
        TagKey<Item> tag = TagKey.of(itemRegistry, Identifier.of("cppfm", "fixture"));
        RegistryEntry<Item> entry = RegistryEntry.of(new RegistryKey<>(Identifier.of("cppfm", "fixture_item")), item);
        check(entry.value() == item && tag.id().equals(Identifier.of("cppfm", "fixture")),
            "canonical registry ABI");
        check(new ItemStack(entry).getItem() == item, "canonical registry constructor");

        DamageSource source = new DamageSource("fixture");
        check("fixture".equals(source.getName()) && source.getSource() == null, "damage source");
        check(new CommandRegistryAccess() != null, "command registry access");
        check(new net.minecraft.network.packet.s2c.common.CustomPayloadS2CPacket(
            Identifier.of("cppfm", "fixture"), new PacketByteBuf()).getChannel()
            .equals(Identifier.of("cppfm", "fixture")), "common packet package");

        MethodHandles.publicLookup().findVirtual(
            Block.class, "getDefaultState", MethodType.methodType(BlockState.class));
        MethodHandles.publicLookup().findVirtual(
            MinecraftServer.class, "getTicks", MethodType.methodType(int.class));
        check(FabricLoader.getInstance().isModLoaded("minecraft"), "loader minecraft container");
        check(FabricLoader.getInstance().getModContainer("minecraft").isPresent(),
            "loader mod container");
        check(FabricLoader.getInstance().getMappingResolver().getCurrentRuntimeNamespace()
            .equals("named"), "named runtime namespace");
        System.out.println("SHADOW ABI FIXTURE PASS");
    }
}
