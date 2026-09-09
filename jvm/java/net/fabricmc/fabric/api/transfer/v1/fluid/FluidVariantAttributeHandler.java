package net.fabricmc.fabric.api.transfer.v1.fluid;

import java.util.Optional;
import net.minecraft.sound.SoundEvent;
import net.minecraft.text.Text;
import net.minecraft.world.World;

/** Common attributes for a fluid variant. */
public interface FluidVariantAttributeHandler {
    default Text getName(FluidVariant variant) {
        return Text.translatable("fluid." + (variant == null || variant.getFluid() == null
            ? "empty" : variant.getFluid().getId()));
    }
    default Optional<SoundEvent> getFillSound(FluidVariant variant) { return Optional.empty(); }
    default Optional<SoundEvent> getEmptySound(FluidVariant variant) { return Optional.empty(); }
    default int getLuminance(FluidVariant variant) { return 0; }
    default int getTemperature(FluidVariant variant) { return FluidConstants.WATER_TEMPERATURE; }
    default int getViscosity(FluidVariant variant, World world) { return FluidConstants.WATER_VISCOSITY; }
    default boolean isLighterThanAir(FluidVariant variant) { return false; }
}
