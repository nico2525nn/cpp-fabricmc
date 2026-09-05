package net.minecraft.fluid;

import net.minecraft.util.Identifier;

public class Fluid {
    private final Identifier id;
    public Fluid(Identifier id) { this.id = id == null ? Identifier.of("minecraft", "empty") : id; }
    public Identifier getId() { return id; }
    public boolean isStill(FluidState state) { return true; }
    public FluidState getDefaultState() { return new FluidState(this, true); }
}
