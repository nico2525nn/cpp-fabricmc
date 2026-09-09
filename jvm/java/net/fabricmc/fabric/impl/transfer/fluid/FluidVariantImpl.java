package net.fabricmc.fabric.impl.transfer.fluid;

import java.util.Objects;
import net.fabricmc.fabric.api.transfer.v1.fluid.FluidVariant;
import net.minecraft.component.ComponentChanges;
import net.minecraft.component.ComponentMap;
import net.minecraft.fluid.Fluid;

/** Identity/value implementation for FluidVariant. */
public final class FluidVariantImpl implements FluidVariant {
    private final Fluid fluid;
    private final ComponentChanges components;
    public FluidVariantImpl(Fluid fluid, ComponentChanges components) {
        this.fluid = fluid == null ? net.minecraft.fluid.Fluids.EMPTY : fluid;
        this.components = components == null ? ComponentChanges.EMPTY : components;
    }
    @Override public boolean isBlank() { return fluid == net.minecraft.fluid.Fluids.EMPTY; }
    @Override public Fluid getObject() { return fluid; }
    @Override public ComponentChanges getComponents() { return components; }
    @Override public ComponentMap getComponentMap() { return components.toAddedRemovedPair().added(); }
    @Override public FluidVariant withComponentChanges(ComponentChanges changes) { return new FluidVariantImpl(fluid, changes); }
    @Override public boolean equals(Object other) {
        return other instanceof FluidVariant value && fluid == value.getFluid() && components.equals(value.getComponents());
    }
    @Override public int hashCode() { return Objects.hash(System.identityHashCode(fluid), components); }
    @Override public String toString() { return "FluidVariant[" + fluid + "," + components + "]"; }
}
