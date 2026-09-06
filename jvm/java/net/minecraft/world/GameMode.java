package net.minecraft.world;

import net.minecraft.text.Text;

/** Canonical 1.21.4 game-mode values used by command arguments and players. */
public enum GameMode {
    SURVIVAL("survival", 0),
    CREATIVE("creative", 1),
    ADVENTURE("adventure", 2),
    SPECTATOR("spectator", 3);

    public static final GameMode DEFAULT = SURVIVAL;
    public static final int UNKNOWN = -1;

    private final String name;
    private final int id;

    GameMode(String name, int id) { this.name = name; this.id = id; }
    public int getId() { return id; }
    public String getName() { return name; }
    public Text getSimpleTranslatableName() { return Text.translatable("selectWorld.gameMode." + name); }
    public Text getTranslatableName() { return Text.translatable("gameMode." + name); }
    public boolean isCreative() { return this == CREATIVE; }
    public boolean isBlockBreakingRestricted() { return this == ADVENTURE || this == SPECTATOR; }
    public boolean isSurvivalLike() { return this == SURVIVAL || this == ADVENTURE; }

    public static GameMode byName(String name) { return byName(name, null); }
    public static GameMode byName(String name, GameMode fallback) {
        if (name != null) for (GameMode mode : values()) if (mode.name.equalsIgnoreCase(name)) return mode;
        return fallback;
    }
    public static GameMode getOrNull(int id) { for (GameMode mode : values()) if (mode.id == id) return mode; return null; }
    public static GameMode byId(int id) { GameMode mode = getOrNull(id); return mode == null ? DEFAULT : mode; }
    public static boolean isValid(int id) { return getOrNull(id) != null; }
}
