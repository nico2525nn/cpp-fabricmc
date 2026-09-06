package net.minecraft.util;

import net.minecraft.block.MapColor;

/**
 * The 1.21.4 named shadow for Minecraft's dye colour enum (intermediary
 * {@code class_1767}).  The enum order and lower-case string form are part of
 * the vanilla ABI: Carpet uses {@link #values()} while constructing its
 * counter command tree.
 */
public enum DyeColor implements StringIdentifiable {
    WHITE("white", 0, 0xF0F0F0, 0xFFFFFF),
    ORANGE("orange", 1, 0xEB8844, 0xD87F33),
    MAGENTA("magenta", 2, 0xC24FBD, 0xB24CD8),
    LIGHT_BLUE("light_blue", 3, 0x6689D3, 0x6699D8),
    YELLOW("yellow", 4, 0xDECF2A, 0xE5E533),
    LIME("lime", 5, 0x41CD34, 0x7FCC19),
    PINK("pink", 6, 0xD88198, 0xF27FA5),
    GRAY("gray", 7, 0x434343, 0x4C4C4C),
    LIGHT_GRAY("light_gray", 8, 0xABABAB, 0x999999),
    CYAN("cyan", 9, 0x287697, 0x4C7F99),
    PURPLE("purple", 10, 0x7B2FBE, 0x7F3FB2),
    BLUE("blue", 11, 0x253192, 0x334CB2),
    BROWN("brown", 12, 0x51301A, 0x664C33),
    GREEN("green", 13, 0x3B511A, 0x667F33),
    RED("red", 14, 0xB3312C, 0xB24C4C),
    BLACK("black", 15, 0x1E1B1B, 0x191919);

    /** Stable id-indexed view used by vanilla and Fabric mods. */
    public static final java.util.List<DyeColor> VALUES =
        java.util.List.of(values());

    private final String name;
    private final int id;
    private final int textureDiffuseColor;
    private final int fireworkColor;
    private final MapColor mapColor;
    private final MapColor terracottaColor;

    DyeColor(String name, int id, int textureDiffuseColor, int fireworkColor) {
        this.name = name;
        this.id = id;
        this.textureDiffuseColor = textureDiffuseColor;
        this.fireworkColor = fireworkColor;
        this.mapColor = new MapColor(id);
        this.terracottaColor = new MapColor(id);
    }

    public int getId() { return id; }
    public String getName() { return name; }
    public int getTextureDiffuseColor() { return textureDiffuseColor; }
    public MapColor getMapColor() { return mapColor; }
    public MapColor getTerracottaColor() { return terracottaColor; }
    public int getFireworkColor() { return fireworkColor; }
    public int getEntityColor() { return fireworkColor; }

    @Override public String asString() { return name; }
    @Override public String toString() { return name; }
    public String getSerializedName() { return name; }

    public static DyeColor byId(int id) {
        return id >= 0 && id < VALUES.size() ? VALUES.get(id) : WHITE;
    }

    public static DyeColor byName(String name, DyeColor fallback) {
        if (name != null) {
            for (DyeColor color : values()) {
                if (color.name.equals(name)) return color;
            }
        }
        return fallback;
    }

    public static DyeColor byFireworkColor(int color) {
        for (DyeColor dyeColor : values()) {
            if (dyeColor.fireworkColor == color) return dyeColor;
        }
        return null;
    }
}
