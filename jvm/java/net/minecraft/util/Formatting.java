package net.minecraft.util;

/** Canonical 1.21.4 text formatting enum. */
public enum Formatting {
    BLACK(0x000000), DARK_BLUE(0x0000AA), DARK_GREEN(0x00AA00), DARK_AQUA(0x00AAAA),
    DARK_RED(0xAA0000), DARK_PURPLE(0xAA00AA), GOLD(0xFFAA00), GRAY(0xAAAAAA),
    DARK_GRAY(0x555555), BLUE(0x5555FF), GREEN(0x55FF55), AQUA(0x55FFFF),
    RED(0xFF5555), LIGHT_PURPLE(0xFF55FF), YELLOW(0xFFFF55), WHITE(0xFFFFFF),
    OBFUSCATED(-1), BOLD(-1), STRIKETHROUGH(-1), UNDERLINE(-1), ITALIC(-1), RESET(-1);

    private final Integer colorValue;
    Formatting(Integer colorValue) { this.colorValue = colorValue < 0 ? null : colorValue; }
    public Integer getColorValue() { return colorValue; }
    public String getName() { return name().toLowerCase(java.util.Locale.ROOT); }
    public boolean isColor() { return colorValue != null; }
    public boolean isModifier() { return !isColor() && this != RESET; }
    public static Formatting byName(String name) {
        if (name == null) return null;
        try { return valueOf(name.toUpperCase(java.util.Locale.ROOT)); }
        catch (IllegalArgumentException ignored) { return null; }
    }
}
