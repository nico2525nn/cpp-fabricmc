package net.minecraft.world.gen.densityfunction;

/** Coordinate-to-density contract used by world-generation extensions. */
public interface DensityFunction {
    default double sample(NoisePos pos) { return 0.0; }
    default DensityFunction apply(DensityFunctionVisitor visitor) {
        return visitor == null ? this : visitor.apply(this);
    }
    default void fill(double[] densities, EachApplier applier) {
        if (densities == null || applier == null) return;
        for (int i = 0; i < densities.length; i++) densities[i] = sample(applier.at(i));
    }
    default double minValue() { return -1.0; }
    default double maxValue() { return 1.0; }

    interface EachApplier {
        default void fill(double[] densities, DensityFunction densityFunction) {
            if (densityFunction != null) densityFunction.fill(densities, this);
        }
        NoisePos at(int index);
    }

    interface NoisePos {
        int blockX();
        int blockY();
        int blockZ();
        default Object getBlender() { return null; }
    }

    interface DensityFunctionVisitor {
        DensityFunction apply(DensityFunction densityFunction);
        default Object apply(Object densityFunction) { return densityFunction; }
    }
}
