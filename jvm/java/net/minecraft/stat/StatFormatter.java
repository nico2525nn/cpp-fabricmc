package net.minecraft.stat;

import java.text.DecimalFormat;
import java.util.Locale;

/** Vanilla 1.21.4 formatter used by {@link Stat}. */
@FunctionalInterface
public interface StatFormatter {
    StatFormatter DEFAULT = Integer::toString;

    StatFormatter DISTANCE = value -> String.format(Locale.ROOT, "%.2f km", value / 100.0);
    StatFormatter DIVIDE_BY_TEN = value -> String.format(Locale.ROOT, "%.1f", value / 10.0);
    StatFormatter TIME = value -> {
        int seconds = value / 20;
        if (seconds < 60) return seconds + "s";
        int minutes = seconds / 60;
        if (minutes < 60) return minutes + "m";
        return (minutes / 60) + "h";
    };

    /** Compatibility constructor shape used by a few reflective consumers. */
    static StatFormatter decimal(DecimalFormat format) {
        return value -> format == null ? Integer.toString(value) : format.format(value);
    }

    String format(int value);
}
