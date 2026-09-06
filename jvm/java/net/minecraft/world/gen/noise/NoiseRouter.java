package net.minecraft.world.gen.noise;

import net.minecraft.world.gen.densityfunction.DensityFunction;

/**
 * The 1.21.4 noise-router record surface exposed to server-side mods.
 *
 * <p>The native server owns terrain generation.  This class deliberately keeps
 * the Java ABI and visitor semantics useful to mods without duplicating the
 * native density graph.</p>
 */
public record NoiseRouter(
    DensityFunction barrierNoise,
    DensityFunction fluidLevelFloodednessNoise,
    DensityFunction fluidLevelSpreadNoise,
    DensityFunction lavaNoise,
    DensityFunction temperature,
    DensityFunction vegetation,
    DensityFunction continents,
    DensityFunction erosion,
    DensityFunction depth,
    DensityFunction ridges,
    DensityFunction initialDensityWithoutJaggedness,
    DensityFunction finalDensity,
    DensityFunction veinToggle,
    DensityFunction veinRidged,
    DensityFunction veinGap
) {
    public NoiseRouter {
        barrierNoise = nonNull(barrierNoise);
        fluidLevelFloodednessNoise = nonNull(fluidLevelFloodednessNoise);
        fluidLevelSpreadNoise = nonNull(fluidLevelSpreadNoise);
        lavaNoise = nonNull(lavaNoise);
        temperature = nonNull(temperature);
        vegetation = nonNull(vegetation);
        continents = nonNull(continents);
        erosion = nonNull(erosion);
        depth = nonNull(depth);
        ridges = nonNull(ridges);
        initialDensityWithoutJaggedness = nonNull(initialDensityWithoutJaggedness);
        finalDensity = nonNull(finalDensity);
        veinToggle = nonNull(veinToggle);
        veinRidged = nonNull(veinRidged);
        veinGap = nonNull(veinGap);
    }

    public NoiseRouter apply(DensityFunction.DensityFunctionVisitor visitor) {
        if (visitor == null) return this;
        return new NoiseRouter(
            visit(visitor, barrierNoise),
            visit(visitor, fluidLevelFloodednessNoise),
            visit(visitor, fluidLevelSpreadNoise),
            visit(visitor, lavaNoise),
            visit(visitor, temperature),
            visit(visitor, vegetation),
            visit(visitor, continents),
            visit(visitor, erosion),
            visit(visitor, depth),
            visit(visitor, ridges),
            visit(visitor, initialDensityWithoutJaggedness),
            visit(visitor, finalDensity),
            visit(visitor, veinToggle),
            visit(visitor, veinRidged),
            visit(visitor, veinGap)
        );
    }

    private static DensityFunction visit(
        DensityFunction.DensityFunctionVisitor visitor, DensityFunction value) {
        DensityFunction result = visitor.apply(value);
        return result == null ? value : result;
    }

    private static DensityFunction nonNull(DensityFunction value) {
        return value == null ? new DensityFunction() { } : value;
    }
}
