package net.minecraft.fluid;

import java.util.Optional;
import net.minecraft.sound.SoundEvent;
import net.minecraft.util.Identifier;

public class Fluid {
    private final Identifier id;
    public Fluid(Identifier id) { this.id = id == null ? Identifier.of("minecraft", "empty") : id; }
    public Identifier getId() { return id; }
    public net.minecraft.registry.entry.RegistryEntry<Fluid> getRegistryEntry() {
        return net.minecraft.registry.Registries.FLUID.getEntry(this).orElse(null);
    }
    public boolean isStill(FluidState state) { return true; }
    public FluidState getDefaultState() { return new FluidState(this, true); }
    /** Canonical 1.21.4 sound hook used by FluidDrainable and Fabric APIs. */
    public Optional<SoundEvent> getBucketFillSound() { return Optional.empty(); }
}
