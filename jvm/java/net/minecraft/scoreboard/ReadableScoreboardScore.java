package net.minecraft.scoreboard;

/** Read-only score view. */
public interface ReadableScoreboardScore {
    default Object getNumberFormat() { return null; }
    default int getScore() { return 0; }
    default boolean isLocked() { return false; }
    default Object getFormattedScore(Object fallbackFormat) { return null; }
}
