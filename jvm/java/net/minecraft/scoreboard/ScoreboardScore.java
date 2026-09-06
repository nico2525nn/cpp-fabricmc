package net.minecraft.scoreboard;

import net.minecraft.text.Text;

/** Minimal native-independent score value. */
public class ScoreboardScore implements ScoreAccess, ReadableScoreboardScore {
    private int score;
    private boolean locked;
    private Text displayText = Text.empty();

    @Override public int getScore() { return score; }
    @Override public void setScore(int value) { score = value; }
    @Override public boolean isLocked() { return locked; }
    @Override public void lock() { locked = true; }
    @Override public void unlock() { locked = false; }
    @Override public void setDisplayText(Text text) { displayText = text == null ? Text.empty() : text; }
    @Override public Text getDisplayText() { return displayText; }
}
