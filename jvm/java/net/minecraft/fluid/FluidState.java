package net.minecraft.fluid;

public final class FluidState {
    private final Fluid fluid;
    private final boolean still;
    public FluidState(Fluid fluid, boolean still) { this.fluid = fluid; this.still = still; }
    public Fluid getFluid() { return fluid; }
    public boolean isStill() { return still; }
    public float getHeight() { return still ? 1.0f : 0.9f; }
}
