package net.minecraft.state.property;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Optional;

public final class IntProperty extends Property<Integer> {
    private final int min, max;
    private IntProperty(String name, int min, int max) { super(name); if (min > max) throw new IllegalArgumentException("min > max"); this.min = min; this.max = max; }
    public static IntProperty of(String name, int min, int max) { return new IntProperty(name, min, max); }
    public int getMin() { return min; }
    public int getMax() { return max; }
    @Override public Collection<Integer> getValues() { ListBuilder builder = new ListBuilder(); for (int i = min; i <= max; i++) builder.add(i); return builder.values; }
    @Override public Optional<Integer> parse(String value) { try { int parsed = Integer.parseInt(value); return parsed < min || parsed > max ? Optional.empty() : Optional.of(parsed); } catch (RuntimeException ignored) { return Optional.empty(); } }
    private static final class ListBuilder { final ArrayList<Integer> values = new ArrayList<>(); void add(int value) { values.add(value); } }
}
