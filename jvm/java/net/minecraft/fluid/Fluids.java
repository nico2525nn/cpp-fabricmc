package net.minecraft.fluid;

import net.minecraft.util.Identifier;

public final class Fluids {
    private Fluids() {}
    public static final Fluid EMPTY = new Fluid(Identifier.of("minecraft", "empty"));
    public static final Fluid WATER = new Fluid(Identifier.of("minecraft", "water"));
    public static final Fluid LAVA = new Fluid(Identifier.of("minecraft", "lava"));
}
