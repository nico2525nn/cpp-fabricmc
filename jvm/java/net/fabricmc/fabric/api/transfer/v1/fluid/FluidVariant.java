package net.fabricmc.fabric.api.transfer.v1.fluid;

import com.mojang.serialization.Codec;
import net.fabricmc.fabric.api.transfer.v1.storage.TransferVariant;
import net.minecraft.component.ComponentChanges;
import net.minecraft.fluid.Fluid;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.registry.entry.RegistryEntry;

/** Immutable still-fluid plus component metadata. */
public interface FluidVariant extends TransferVariant<Fluid> {
    Codec<FluidVariant> CODEC = new Codec<>() { };
    PacketCodec<RegistryByteBuf, FluidVariant> PACKET_CODEC = PacketCodec.ofLegacy((buffer, value) -> { }, buffer -> blank());
    static FluidVariant blank() { return of(net.minecraft.fluid.Fluids.EMPTY); }
    static FluidVariant of(Fluid fluid) { return of(fluid, ComponentChanges.EMPTY); }
    static FluidVariant of(Fluid fluid, ComponentChanges components) {
        return new net.fabricmc.fabric.impl.transfer.fluid.FluidVariantImpl(
            fluid == null ? net.minecraft.fluid.Fluids.EMPTY : fluid,
            components == null ? ComponentChanges.EMPTY : components);
    }
    default Fluid getFluid() { return getObject(); }
    default RegistryEntry<Fluid> getRegistryEntry() { return getFluid().getRegistryEntry(); }
    @Override FluidVariant withComponentChanges(ComponentChanges changes);
}
