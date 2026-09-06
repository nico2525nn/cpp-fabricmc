package net.minecraft.scoreboard;

import net.minecraft.text.Text;

/** Mutable view of a scoreboard score. */
public interface ScoreAccess {
    default void setNumberFormat(Object numberFormat) { }
    default boolean isLocked() { return false; }
    default void resetScore() { }
    default Text getDisplayText() { return Text.empty(); }
    default int incrementScore() { return incrementScore(1); }
    default int incrementScore(int amount) { setScore(getScore() + amount); return getScore(); }
    default int getScore() { return 0; }
    default void setScore(int score) { }
    default void unlock() { }
    default void setDisplayText(Text text) { }
    default void lock() { }
}
