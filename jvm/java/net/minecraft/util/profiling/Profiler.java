package net.minecraft.util.profiling;

/** Small profiling facade required by the 1.21.4 server-loop mixin surface. */
public final class Profiler {
    private static final ProfilerFiller INSTANCE = new ProfilerFiller() { };

    private Profiler() { }

    public static ProfilerFiller get() { return INSTANCE; }
}
