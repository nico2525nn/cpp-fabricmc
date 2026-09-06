package net.minecraft.stat;

import java.util.Objects;
import net.minecraft.util.Identifier;

/** Immutable typed statistic key/value pair from the 1.21.4 named API. */
public final class Stat<T> {
    private final StatType<T> type;
    private final T value;
    private final StatFormatter formatter;

    public Stat(StatType<T> type, T value, StatFormatter formatter) {
        this.type = Objects.requireNonNull(type, "type");
        this.value = value;
        this.formatter = formatter == null ? StatFormatter.DEFAULT : formatter;
    }

    public StatType<T> getType() { return type; }
    public T getValue() { return value; }
    public String format(int value) { return formatter.format(value); }

    public String getName() {
        return getName(type, value);
    }

    public static <T> String getName(StatType<T> type, T value) {
        if (type == null || value == null) return String.valueOf(value);
        Identifier id = type.getRegistry().getId(value);
        return getName(id);
    }

    public static String getName(Identifier id) {
        return id == null ? "" : id.toString();
    }

    @Override public boolean equals(Object other) {
        if (this == other) return true;
        if (!(other instanceof Stat<?> stat)) return false;
        return type.equals(stat.type) && Objects.equals(value, stat.value);
    }

    @Override public int hashCode() { return Objects.hash(type, value); }

    @Override public String toString() { return getName(); }
}
