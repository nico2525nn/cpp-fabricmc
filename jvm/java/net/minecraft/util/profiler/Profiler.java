package net.minecraft.util.profiler;

/** Lightweight profiler token used by the 1.21.4 resource reload ABI. */
public class Profiler {
    private static final Profiler INSTANCE = new Profiler();

    public static Profiler get() { return INSTANCE; }
}
