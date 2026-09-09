package net.fabricmc.fabric.api.transfer.v1.fluid.base;

import java.util.Objects;
import net.fabricmc.fabric.api.transfer.v1.fluid.FluidVariant;
import net.fabricmc.fabric.api.transfer.v1.storage.StoragePreconditions;
import net.fabricmc.fabric.api.transfer.v1.storage.base.SingleVariantStorage;

/** Single-variant storage specialization for fluids. */
public abstract class SingleFluidStorage extends SingleVariantStorage<FluidVariant> {
    public static SingleFluidStorage withFixedCapacity(long capacity, Runnable onChange) {
        StoragePreconditions.notNegative(capacity); Objects.requireNonNull(onChange, "onChange");
        return new SingleFluidStorage() {
            @Override protected long getCapacity(FluidVariant variant) { return capacity; }
            @Override protected void onFinalCommit() { onChange.run(); }
        };
    }
    @Override protected final FluidVariant getBlankVariant() { return FluidVariant.blank(); }
    public void readNbt(net.minecraft.nbt.NbtCompound nbt, net.minecraft.registry.RegistryWrapper.WrapperLookup lookup) {
        SingleVariantStorage.readNbt(this, FluidVariant.CODEC, FluidVariant::blank, nbt, lookup);
    }
    public void writeNbt(net.minecraft.nbt.NbtCompound nbt, net.minecraft.registry.RegistryWrapper.WrapperLookup lookup) {
        SingleVariantStorage.writeNbt(this, FluidVariant.CODEC, nbt, lookup);
    }
}
