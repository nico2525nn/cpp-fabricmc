package net.minecraft.text;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import com.mojang.serialization.Codec;
import com.mojang.serialization.DataResult;
import net.minecraft.util.Formatting;

/** Immutable RGB color value used by the 1.21.4 text/style ABI. */
public final class TextColor {
    private static final String RGB_PREFIX = "#";
    private static final Map<Formatting, TextColor> FORMATTING_TO_COLOR = new HashMap<>();
    private static final Map<String, TextColor> BY_NAME = new HashMap<>();
    public static final Codec<TextColor> CODEC = new Codec<>() { };

    private final String name;
    private final int rgb;

    static {
        for (Formatting formatting : Formatting.values()) {
            if (!formatting.isColor()) continue;
            TextColor color = new TextColor(formatting.getColorValue(), formatting.getName());
            FORMATTING_TO_COLOR.put(formatting, color);
            BY_NAME.put(formatting.getName(), color);
        }
    }

    public TextColor(int rgb) { this(rgb, null); }
    public TextColor(int rgb, String name) {
        this.rgb = rgb & 0x00ffffff;
        this.name = name == null || name.isEmpty() ? null : name.toLowerCase(Locale.ROOT);
    }

    public static TextColor fromFormatting(Formatting formatting) {
        return formatting == null ? null : FORMATTING_TO_COLOR.get(formatting);
    }

    public static TextColor method_27722(Formatting formatting) { return fromFormatting(formatting); }

    public static TextColor fromRgb(int rgb) { return new TextColor(rgb); }

    public static DataResult<TextColor> parse(String value) {
        if (value == null) return DataResult.error(() -> "color is null");
        String normalized = value.toLowerCase(Locale.ROOT);
        TextColor named = BY_NAME.get(normalized);
        if (named != null) return DataResult.success(named);
        if (normalized.length() == 7 && normalized.charAt(0) == '#') {
            try { return DataResult.success(new TextColor(Integer.parseInt(normalized.substring(1), 16))); }
            catch (NumberFormatException ignored) { }
        }
        return DataResult.error(() -> "invalid color: " + value);
    }

    public int getRgb() { return rgb; }
    public String getName() { return name; }
    public String getHexCode() { return String.format(Locale.ROOT, "#%06x", rgb); }

    public static String method_27720(TextColor textColor) {
        return textColor == null ? "" : textColor.name == null ? textColor.getHexCode() : textColor.name;
    }

    @Override public boolean equals(Object other) {
        return other instanceof TextColor color && rgb == color.rgb && Objects.equals(name, color.name);
    }
    @Override public int hashCode() { return Objects.hash(name, rgb); }
    @Override public String toString() { return name == null ? getHexCode() : name; }
}
