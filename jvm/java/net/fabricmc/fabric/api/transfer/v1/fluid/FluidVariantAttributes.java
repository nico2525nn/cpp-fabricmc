package net.fabricmc.fabric.api.transfer.v1.fluid;

import java.util.Objects;
import net.fabricmc.fabric.api.lookup.v1.custom.ApiProviderMap;
import net.minecraft.fluid.Fluid;
import net.minecraft.text.Text;
import net.minecraft.sound.SoundEvent;
import net.minecraft.world.World;

/** Registry and safe defaults for fluid variant attributes. */
public final class FluidVariantAttributes {
    private static final ApiProviderMap<Fluid, FluidVariantAttributeHandler> HANDLERS = ApiProviderMap.create();
    private static final FluidVariantAttributeHandler DEFAULT_HANDLER = new FluidVariantAttributeHandler() { };
    private static volatile boolean coloredVanillaFluidNames;
    private FluidVariantAttributes() { }
    public static void register(Fluid fluid, FluidVariantAttributeHandler handler) {
        Objects.requireNonNull(fluid, "fluid"); Objects.requireNonNull(handler, "handler");
        if (HANDLERS.putIfAbsent(fluid, handler) != null) throw new IllegalArgumentException("Duplicate handler: " + fluid);
    }
    public static void enableColoredVanillaFluidNames() { coloredVanillaFluidNames = true; }
    public static FluidVariantAttributeHandler getHandler(Fluid fluid) { return HANDLERS.get(fluid); }
    public static FluidVariantAttributeHandler getHandlerOrDefault(Fluid fluid) {
        FluidVariantAttributeHandler handler = HANDLERS.get(fluid);
        return handler == null ? DEFAULT_HANDLER : handler;
    }
    public static Text getName(FluidVariant variant) { return getHandlerOrDefault(variant.getFluid()).getName(variant); }
    public static SoundEvent getFillSound(FluidVariant variant) {
        return getHandlerOrDefault(variant.getFluid()).getFillSound(variant).orElse(new SoundEvent(net.minecraft.util.Identifier.of("minecraft", "item.bucket.fill")));
    }
    public static SoundEvent getEmptySound(FluidVariant variant) {
        return getHandlerOrDefault(variant.getFluid()).getEmptySound(variant).orElse(new SoundEvent(net.minecraft.util.Identifier.of("minecraft", "item.bucket.empty")));
    }
    public static int getLuminance(FluidVariant variant) {
        int value = getHandlerOrDefault(variant.getFluid()).getLuminance(variant);
        return value < 0 || value > 15 ? DEFAULT_HANDLER.getLuminance(variant) : value;
    }
    public static int getTemperature(FluidVariant variant) {
        int value = getHandlerOrDefault(variant.getFluid()).getTemperature(variant);
        return Math.max(0, value);
    }
    public static int getViscosity(FluidVariant variant, World world) {
        int value = getHandlerOrDefault(variant.getFluid()).getViscosity(variant, world);
        return value <= 0 ? DEFAULT_HANDLER.getViscosity(variant, world) : value;
    }
    public static boolean isLighterThanAir(FluidVariant variant) { return getHandlerOrDefault(variant.getFluid()).isLighterThanAir(variant); }
}
