package net.minecraft.text;

import java.util.Arrays;

/** Plain text representation; styling is intentionally lossless only at the boundary. */
public final class Text {
    private final String value;
    private Text(String value) { this.value = value == null ? "" : value; }
    public static Text literal(String value) { return new Text(value); }
    public static Text translatable(String key, Object... args) {
        return new Text(args == null || args.length == 0 ? key : key + " " + Arrays.toString(args));
    }
    public String getString() { return value; }
    public String getContent() { return value; }
    @Override public String toString() { return value; }
}
